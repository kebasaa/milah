#include "app_controller.h"

#include "core/coverage.h"
#include "core/data_paths.h"
#include "core/lexicon.h"
#include "core/osis.h"
#include "core/project.h"
#include "core/recent_files.h"
#include "core/serialize.h"
#include "core/tokenize.h"
#include "project_storage.h"
#include "ui/dictionary_entry_dialog.h"
#include "ui/download_manuscripts_dialog.h"
#include "ui/manuscript_library_dialog.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonObject>
#include <QMessageBox>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QWidget>

#include <algorithm>

namespace milah {
namespace {

constexpr qint64 MaxOsisSize = 64 * 1024 * 1024;
constexpr int MaxUndoDepth = 50;

QString makeSourceId(SourceRole role, int index)
{
    return QStringLiteral("%1-%2-%3")
        .arg(sourceRoleToString(role),
             QString::number(QDateTime::currentMSecsSinceEpoch(), 36))
        .arg(index);
}

struct OpenedFile
{
    QString name;
    QString content;
};

/// Names a Combined word: its verse and its place among that verse's columns.
/// Both the notes and the interlinear wording are keyed this way, so a division
/// that shifts words along shifts what was said about them — see
/// shiftColumnKeys().
QString columnKey(const QString &verseId, int columnIndex)
{
    return QStringLiteral("%1:%2").arg(verseId).arg(columnIndex);
}

/// Moves a verse's per-column entries along when a column is inserted at or
/// removed from `at`, so what was written about a word stays with it. `delta`
/// is +1 for a division and -1 for a join; an entry on a removed column goes
/// with it.
QMap<QString, QString> shiftColumnKeys(
    const QMap<QString, QString> &notes,
    const QString &verseId,
    int at,
    int delta)
{
    const QString prefix = verseId + QLatin1Char(':');
    QMap<QString, QString> result;
    for (auto item = notes.constBegin(); item != notes.constEnd(); ++item) {
        if (!item.key().startsWith(prefix)) {
            result.insert(item.key(), item.value());
            continue;
        }

        bool numeric = false;
        const int column = QStringView(item.key()).mid(prefix.size()).toInt(&numeric);
        if (!numeric) {
            result.insert(item.key(), item.value());
            continue;
        }

        if (delta < 0 && column == at) {
            continue; // The column is going; so is what was said about it.
        }
        const int moved = column >= at ? column + delta : column;
        result.insert(columnKey(verseId, moved), item.value());
    }
    return result;
}

} // namespace

AppController::AppController(QWidget *dialogParent, QObject *parent)
    : QObject(parent)
    , m_dialogParent(dialogParent)
    , m_message(QStringLiteral("Load two or more manuscript OSIS files to begin."))
{
    // The lexicon is a file beside the executable rather than part of it, so it
    // can go missing. Without it the Strong's row is empty and the spelling
    // check stands down, neither of which is obvious. This is kept apart from
    // the status message, which the next action would overwrite; the window
    // shows it as a standing indicator instead.
    m_acceptedForms = AttestedForms::shared().keys();
    m_acceptedForms.unite(m_dictionary.keys());

    if (HebrewLexicon::shared().isEmpty()) {
        m_dataWarning = QStringLiteral(
                            "hebrew_lexicon.json was not found, so Strong's "
                            "numbers and the spelling check are unavailable.\n\n"
                            "Looked in:\n  %1")
                            .arg(dataSearchPaths().join(QStringLiteral("\n  ")));
    }
}

DocumentRefs AppController::manuscripts() const
{
    DocumentRefs result;
    for (const SourceDocument &source : m_sources) {
        if (source.role == SourceRole::Manuscript) {
            result.append(&source);
        }
    }
    return result;
}

DocumentRefs AppController::translations() const
{
    DocumentRefs result;
    for (const SourceDocument &source : m_sources) {
        if (source.role == SourceRole::Translation) {
            result.append(&source);
        }
    }
    return result;
}

QHash<QString, QString> AppController::associationMap() const
{
    QHash<QString, QString> map;
    for (const TranslationAssociation &association : m_associations) {
        map.insert(association.translationId, association.manuscriptId);
    }
    return map;
}

void AppController::setFilters(const ReviewFilters &filters)
{
    m_filters = filters;
    emit locationChanged();
}

CombinedDraft AppController::draftFor(const AlignedVerse &aligned) const
{
    const auto existing = m_combined.constFind(aligned.reference.id);
    if (existing != m_combined.constEnd()) {
        return existing.value();
    }
    return generateCombined(aligned, manuscripts(), referenceFor(aligned.reference.id));
}

bool AppController::matchesFilters(const AlignedVerse &aligned) const
{
    if (!m_filters.any()) {
        return true;
    }

    const auto stored = m_combined.constFind(aligned.reference.id);
    const bool hasDraft = stored != m_combined.constEnd();

    if (m_filters.ties && hasDraft) {
        for (const ConsensusColumn &column : stored.value().columns) {
            if (column.needsReview) {
                return true;
            }
        }
    }
    if (m_filters.manual && hasDraft && stored.value().manualText.has_value()) {
        return true;
    }
    if (m_filters.missing) {
        const DocumentRefs sources = manuscripts();
        for (const AlignmentColumn &column : aligned.columns) {
            for (const SourceDocument *source : sources) {
                if (!column.cell(source->id)) {
                    return true;
                }
            }
        }
    }
    if (m_filters.uncertain) {
        for (const TranslationSpan &span : m_translationSpans) {
            if (span.verseId == aligned.reference.id
                && span.confidence == SpanConfidence::Low) {
                return true;
            }
        }
    }

    return false;
}

void AppController::setMessage(const QString &message)
{
    if (m_message == message) {
        return;
    }
    m_message = message;
    emit messageChanged(m_message);
}

void AppController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

void AppController::reportError(const QString &fallback)
{
    setMessage(m_lastError.isEmpty() ? fallback : m_lastError);
    m_lastError.clear();
}

bool AppController::confirm(const QString &question)
{
    return QMessageBox::question(
               m_dialogParent,
               QStringLiteral("Milah"),
               question,
               QMessageBox::Yes | QMessageBox::No,
               QMessageBox::No)
        == QMessageBox::Yes;
}

bool AppController::hasManualEdits() const
{
    if (!m_columnSplits.isEmpty()) {
        return true;
    }
    for (const CombinedDraft &draft : m_combined) {
        if (draft.manualText.has_value()) {
            return true;
        }
    }
    return false;
}

namespace {

/// Whether any of these verses has an entry in a per-column map.
bool touchesVerses(
    const QMap<QString, QString> &entries,
    const QStringList &verseIds)
{
    for (const QString &verseId : verseIds) {
        const QString prefix = verseId + QLatin1Char(':');
        for (auto item = entries.constKeyValueBegin();
             item != entries.constKeyValueEnd();
             ++item) {
            if ((*item).first.startsWith(prefix)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool AppController::hasNotes(const QStringList &verseIds) const
{
    return touchesVerses(m_combinedNotes, verseIds);
}

bool AppController::hasInterlinearEdits(const QStringList &verseIds) const
{
    return touchesVerses(m_interlinearWords, verseIds);
}

bool AppController::hasManualEdits(const QStringList &verseIds) const
{
    for (const QString &verseId : verseIds) {
        // A divided word is the editor's work as much as a typed one, and
        // regenerating discards it, so it earns the same warning.
        if (m_columnSplits.contains(verseId)) {
            return true;
        }
        const auto draft = m_combined.constFind(verseId);
        if (draft != m_combined.constEnd() && draft->manualText.has_value()) {
            return true;
        }
    }
    return false;
}

QString AppController::suggestAssociation(
    const SourceDocument &translation,
    const DocumentRefs &manuscriptList) const
{
    const QString shelfmark =
        translation.metadata.identifiers.value(QStringLiteral("shelfmark"));
    if (!shelfmark.isEmpty()) {
        for (const SourceDocument *source : manuscriptList) {
            if (source->metadata.identifiers.value(QStringLiteral("shelfmark"))
                == shelfmark) {
                return source->id;
            }
        }
    }

    QString bestId;
    int bestOverlap = -1;
    for (const SourceDocument *source : manuscriptList) {
        int overlap = 0;
        for (const SourceVerse &verse : translation.verses) {
            if (source->hasVerse(verse.reference.id)) {
                overlap += 1;
            }
        }
        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            bestId = source->id;
        }
    }
    return bestId;
}

QMap<QString, CombinedDraft> AppController::buildCombined(
    const DocumentRefs &manuscriptList) const
{
    QMap<QString, CombinedDraft> result;
    // Every chapter any witness reaches, so a chapter only one of them covers
    // still gets a draft rather than being silently skipped.
    for (const LocationCoverage &covered : coveredLocations(manuscriptList)) {
        for (const QString &verseId :
             verseIdsAtLocation(manuscriptList, covered.location)) {
            result.insert(verseId, regeneratedDraft(verseId, manuscriptList));
        }
    }
    return result;
}

CombinedDraft AppController::regeneratedDraft(
    const QString &verseId,
    const DocumentRefs &manuscriptList) const
{
    // Each verse is read against its own reference, so a verse whose usual
    // witness is silent still yields an edition rather than a blank row.
    const QString reference = referenceFor(verseId);
    const AlignedVerse aligned = alignedFor(verseId, manuscriptList, reference);
    return generateCombined(aligned, manuscriptList, reference);
}

void AppController::rebuildLocations()
{
    m_locations = coveredLocations(manuscripts());
}

int AppController::indexOfLocation(const Location &location) const
{
    for (int index = 0; index < m_locations.size(); ++index) {
        if (m_locations.at(index).location == location) {
            return index;
        }
    }
    return -1;
}

AlignedVerse AppController::alignedFor(
    const QString &verseId,
    const DocumentRefs &sources,
    const QString &priorityId) const
{
    // The tables are already loaded -- the constructor forces them for the
    // spell check -- so naming them here costs nothing at startup.
    AlignmentOptions options;
    options.abbreviations = &AbbreviationTable::shared();
    options.lexicon = &HebrewLexicon::shared();
    options.attested = &AttestedForms::shared();

    return applyColumnSplits(
        alignVerse(verseId, sources, priorityId, options),
        m_columnSplits.value(verseId));
}

void AppController::rebuildAlignedVerses()
{
    m_alignedVerses.clear();
    if (m_location.has_value()) {
        const DocumentRefs sources = manuscripts();
        for (const QString &verseId : verseIdsAtLocation(sources, *m_location)) {
            m_alignedVerses.append(alignedFor(verseId, sources, referenceFor(verseId)));
        }
    }
    validateSelection();
}

void AppController::validateSelection()
{
    if (!m_selection.isValid()) {
        return;
    }
    for (const AlignedVerse &aligned : m_alignedVerses) {
        if (aligned.reference.id == m_selection.verseId
            && m_selection.columnIndex < aligned.columns.size()) {
            return;
        }
    }
    m_selection = WordSelection{};
    emit selectionChanged();
}

void AppController::selectWord(const QString &verseId, int columnIndex)
{
    WordSelection next;
    if (columnIndex >= 0) {
        next.verseId = verseId;
        next.columnIndex = columnIndex;
    }
    if (next == m_selection) {
        return;
    }
    m_selection = next;
    emit selectionChanged();
}

QList<ColumnNote> AppController::selectedNotes() const
{
    if (!m_selection.isValid()) {
        return {};
    }
    for (const AlignedVerse &aligned : m_alignedVerses) {
        if (aligned.reference.id != m_selection.verseId) {
            continue;
        }
        if (m_selection.columnIndex >= aligned.columns.size()) {
            return {};
        }
        return columnNotes(aligned.columns.at(m_selection.columnIndex), manuscripts());
    }
    return {};
}

QString AppController::selectedWord() const
{
    if (!m_selection.isValid()) {
        return {};
    }
    for (const AlignedVerse &aligned : m_alignedVerses) {
        if (aligned.reference.id != m_selection.verseId) {
            continue;
        }
        const CombinedDraft draft = draftFor(aligned);
        if (m_selection.columnIndex >= draft.columns.size()) {
            return {};
        }
        return draft.columns.at(m_selection.columnIndex).text.value_or(QString());
    }
    return {};
}

QList<int> AppController::associatedColumns(
    const QString &translationId,
    const AlignedVerse &aligned) const
{
    const QString manuscriptId = associationMap().value(translationId);

    QList<int> columns;
    if (!manuscriptId.isEmpty()) {
        for (int index = 0; index < aligned.columns.size(); ++index) {
            if (aligned.columns.at(index).cell(manuscriptId)) {
                columns.append(index);
            }
        }
    }

    // No manuscript named, or one that is silent for this verse: there is
    // nothing narrower to go by, so the translation spreads across the verse as
    // it always did.
    if (columns.isEmpty()) {
        for (int index = 0; index < aligned.columns.size(); ++index) {
            columns.append(index);
        }
    }
    return columns;
}

void AppController::refreshTranslationSpans()
{
    const DocumentRefs translationList = translations();
    if (!m_location.has_value() || translationList.isEmpty()
        || m_alignedVerses.isEmpty()) {
        return;
    }

    QSet<QString> verseIds;
    for (const AlignedVerse &aligned : m_alignedVerses) {
        verseIds.insert(aligned.reference.id);
    }
    QSet<QString> translationIds;
    for (const SourceDocument *translation : translationList) {
        translationIds.insert(translation->id);
    }

    // Spans for other chapters survive untouched; spans for this chapter are
    // regenerated only where none exist yet, so corrections are not lost.
    QList<TranslationSpan> next;
    for (const TranslationSpan &span : m_translationSpans) {
        if (!verseIds.contains(span.verseId)
            && translationIds.contains(span.translationId)) {
            next.append(span);
        }
    }

    for (const SourceDocument *translation : translationList) {
        for (const AlignedVerse &aligned : m_alignedVerses) {
            QList<TranslationSpan> existing;
            for (const TranslationSpan &span : m_translationSpans) {
                if (span.translationId == translation->id
                    && span.verseId == aligned.reference.id) {
                    existing.append(span);
                }
            }

            if (!existing.isEmpty()) {
                next.append(existing);
                continue;
            }

            int tokenCount = 0;
            if (const SourceVerse *verse = translation->verse(aligned.reference.id)) {
                tokenCount = int(verse->tokens.size());
            }
            next.append(alignTranslation(
                translation->id,
                aligned.reference.id,
                tokenCount,
                associatedColumns(translation->id, aligned)));
        }
    }

    m_translationSpans = next;
}

void AppController::commitCombined(const QMap<QString, CombinedDraft> &next)
{
    commitCombined(
        next,
        m_columnSplits,
        m_chapterReferences,
        m_verseReferences,
        m_combinedNotes,
        m_interlinearWords);
}

void AppController::commitCombined(
    const QMap<QString, CombinedDraft> &next,
    const QMap<QString, QList<int>> &nextSplits,
    const QMap<QString, QString> &nextChapterReferences,
    const QMap<QString, QString> &nextVerseReferences,
    const QMap<QString, QString> &nextNotes,
    const QMap<QString, QString> &nextInterlinear)
{
    m_undoStack.append(currentStep());
    while (m_undoStack.size() > MaxUndoDepth) {
        m_undoStack.removeFirst();
    }
    m_redoStack.clear();
    m_combined = next;
    m_columnSplits = nextSplits;
    m_chapterReferences = nextChapterReferences;
    m_verseReferences = nextVerseReferences;
    m_combinedNotes = nextNotes;
    m_interlinearWords = nextInterlinear;
    setDirty(true);
    emit historyChanged();
}

AppController::EditStep AppController::currentStep() const
{
    return EditStep{
        m_combined,
        m_columnSplits,
        m_chapterReferences,
        m_verseReferences,
        m_combinedNotes,
        m_interlinearWords};
}

void AppController::restoreStep(const EditStep &step)
{
    m_combined = step.combined;
    m_columnSplits = step.columnSplits;
    m_chapterReferences = step.chapterReferences;
    m_verseReferences = step.verseReferences;
    m_combinedNotes = step.combinedNotes;
    m_interlinearWords = step.interlinearWords;
}

QString AppController::currentReference() const
{
    if (m_location.has_value()) {
        const QString chosen = m_chapterReferences.value(locationKey(*m_location));
        if (!chosen.isEmpty()) {
            return chosen;
        }
    }
    return m_priorityId;
}

QString AppController::referenceFor(const QString &verseId) const
{
    const DocumentRefs sources = manuscripts();

    // Each choice is honoured only while that manuscript still has the verse: a
    // project may be reopened against a re-cut source.
    const auto reads = [&sources, &verseId](const QString &sourceId) {
        if (sourceId.isEmpty()) {
            return false;
        }
        for (const SourceDocument *source : sources) {
            if (source->id == sourceId && source->hasVerse(verseId)) {
                return true;
            }
        }
        return false;
    };

    // The verse's own exception first, then its chapter's choice, then the
    // project's — narrowest to widest, so a verse the editor spoke for keeps
    // its reading however the chapter is changed around it.
    const QString verseChoice = m_verseReferences.value(verseId);
    if (reads(verseChoice)) {
        return verseChoice;
    }

    const QString chapterChoice =
        m_chapterReferences.value(verseId.section(QLatin1Char('.'), 0, 1));
    if (reads(chapterChoice)) {
        return chapterChoice;
    }

    return referenceForVerse(verseId, sources, m_priorityId);
}

QString AppController::combinedNote(const QString &verseId, int columnIndex) const
{
    return m_combinedNotes.value(columnKey(verseId, columnIndex));
}

QString AppController::interlinearWord(const QString &verseId, int columnIndex) const
{
    const QString chosen = m_interlinearWords.value(columnKey(verseId, columnIndex));
    if (!chosen.isEmpty()) {
        return chosen;
    }

    // Otherwise what the aligned translation reads here. Several words standing
    // for one Hebrew word are joined by a dash — "to do" reads "to-do" — which
    // is the convention an interlinear is read with, and what the export writes.
    QStringList words;
    for (const TranslationSpan &span : m_translationSpans) {
        if (span.verseId != verseId || span.removed) {
            continue;
        }
        // A group is hung on the column it starts at: the words belong to the
        // group rather than to any single column it happens to reach across.
        if (std::max(0, span.columnStart) != columnIndex) {
            continue;
        }
        const QStringList spanned = spanWords(span);
        if (!spanned.isEmpty()) {
            words.append(spanned.join(QLatin1Char('-')));
        }
    }
    return words.join(QLatin1Char(' '));
}

void AppController::setInterlinearWord(
    const QString &verseId,
    int columnIndex,
    const QString &text)
{
    const QString key = columnKey(verseId, columnIndex);
    const QString trimmed = text.trimmed();

    QMap<QString, QString> next = m_interlinearWords;
    if (trimmed.isEmpty() || trimmed == interlinearWord(verseId, columnIndex)) {
        // Either cleared, or typed back to what the translation already says:
        // both mean there is nothing of the editor's own to keep here.
        next.remove(key);
    } else {
        next.insert(key, trimmed);
    }
    if (next == m_interlinearWords) {
        return;
    }

    commitCombined(
        m_combined,
        m_columnSplits,
        m_chapterReferences,
        m_verseReferences,
        m_combinedNotes,
        next);
    emit verseChanged(verseId);
}

void AppController::closeTranslation(const QString &sourceId)
{
    const SourceDocument *closing = nullptr;
    for (const SourceDocument &source : m_sources) {
        if (source.id == sourceId && source.role == SourceRole::Translation) {
            closing = &source;
            break;
        }
    }
    if (!closing) {
        return;
    }

    // The spans are alignment the editor may have corrected by hand, and
    // closing is the only thing that discards them without a way back.
    const QString name =
        closing->metadata.title.isEmpty() ? closing->name : closing->metadata.title;
    if (!confirm(QStringLiteral("Close %1? Its aligned translation is discarded.")
                     .arg(name))) {
        return;
    }

    m_sources.removeIf([&sourceId](const SourceDocument &source) {
        return source.id == sourceId;
    });
    m_associations.removeIf([&sourceId](const TranslationAssociation &association) {
        return association.translationId == sourceId;
    });
    m_translationSpans.removeIf([&sourceId](const TranslationSpan &span) {
        return span.translationId == sourceId;
    });

    // What the editor typed into the Interlinear row is keyed by column rather
    // than by translation, so it stays: it is part of the edition now.
    setDirty(true);
    rebuildAlignedVerses();
    setMessage(QStringLiteral("Closed %1.").arg(name));
    emit sourcesChanged();
}

void AppController::setVerseReference(const QString &verseId, const QString &sourceId)
{
    QMap<QString, QString> next = m_verseReferences;
    if (sourceId.isEmpty()) {
        next.remove(verseId);
    } else {
        next.insert(verseId, sourceId);
    }
    if (next == m_verseReferences) {
        return;
    }

    // The words are left as they are: reading a verse against another witness
    // changes what it is compared with, not what the editor has settled. What
    // rebuilds the readings is Regenerate.
    commitCombined(
        m_combined,
        m_columnSplits,
        m_chapterReferences,
        next,
        m_combinedNotes,
        m_interlinearWords);
    rebuildAlignedVerses();
    emit locationChanged();
}

void AppController::setCombinedNote(
    const QString &verseId,
    int columnIndex,
    const QString &note)
{
    const QString key = columnKey(verseId, columnIndex);
    const QString trimmed = note.trimmed();

    QMap<QString, QString> next = m_combinedNotes;
    if (trimmed.isEmpty()) {
        next.remove(key);
    } else {
        next.insert(key, trimmed);
    }
    if (next == m_combinedNotes) {
        return;
    }

    commitCombined(
        m_combined,
        m_columnSplits,
        m_chapterReferences,
        m_verseReferences,
        next,
        m_interlinearWords);
    emit verseChanged(verseId);
    emit selectionChanged();
}

QStringList AppController::verseIdsInChapter() const
{
    if (!m_location.has_value()) {
        return {};
    }
    return verseIdsAtLocation(manuscripts(), *m_location);
}

bool AppController::regenerateChapter()
{
    const QStringList verseIds = verseIdsInChapter();
    if (verseIds.isEmpty()) {
        return false;
    }

    // Regenerating rebuilds every word of the chapter, so anything the editor
    // put there goes with it — including the notes, which name columns that are
    // about to be built afresh. Ask only where there is something to lose, and
    // name it: a warning about notes where none were written, or about work
    // that does not exist, teaches the reader to dismiss warnings unread.
    QStringList losses;
    if (hasManualEdits(verseIds)) {
        losses.append(QStringLiteral("your manual edits"));
    }
    if (hasNotes(verseIds)) {
        losses.append(QStringLiteral("the notes you have written on its words"));
    }
    if (hasInterlinearEdits(verseIds)) {
        losses.append(QStringLiteral("the interlinear wording you have typed"));
    }

    if (!losses.isEmpty()
        && !confirm(
            QStringLiteral(
                "Rebuilding this chapter's Combined text will discard %1.\n\nContinue?")
                .arg(losses.join(QStringLiteral(" and "))))) {
        return false;
    }

    // A divided column only ever holds a word the editor put there by hand, so
    // regenerating takes the divisions with the words; leaving them would strew
    // the verse with blank columns no witness reads. Cleared before the drafts
    // are built so the two agree on how many columns each verse has.
    const QMap<QString, QList<int>> divided = m_columnSplits;
    QMap<QString, QList<int>> nextSplits = m_columnSplits;
    QMap<QString, QString> nextNotes = m_combinedNotes;
    QMap<QString, QString> nextInterlinear = m_interlinearWords;

    // Both a note and an interlinear word name a column, and the columns of
    // these verses are about to be built afresh; keeping them would leave them
    // pointing at words that moved.
    const auto dropVerse = [](QMap<QString, QString> &entries, const QString &verseId) {
        const QString prefix = verseId + QLatin1Char(':');
        for (auto item = entries.begin(); item != entries.end();) {
            item = item.key().startsWith(prefix) ? entries.erase(item) : std::next(item);
        }
    };

    for (const QString &verseId : verseIds) {
        nextSplits.remove(verseId);
        dropVerse(nextNotes, verseId);
        dropVerse(nextInterlinear, verseId);
    }

    m_columnSplits = nextSplits;
    const DocumentRefs sources = manuscripts();
    QMap<QString, CombinedDraft> next = m_combined;
    for (const QString &verseId : verseIds) {
        next.insert(verseId, regeneratedDraft(verseId, sources));
    }
    m_columnSplits = divided;

    commitCombined(
        next,
        nextSplits,
        m_chapterReferences,
        m_verseReferences,
        nextNotes,
        nextInterlinear);
    return true;
}

QString AppController::lastDirectory() const
{
    // Empty when nothing has been opened yet, which asks QFileDialog to fall
    // back to its own default (the working or last-system directory).
    return QSettings().value(QStringLiteral("paths/lastDirectory")).toString();
}

void AppController::rememberDirectory(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return;
    }
    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"),
        QFileInfo(filePath).absolutePath());
}

void AppController::loadLibraryFiles(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }

    // The manifest recorded which of these is a translation, so the editor does
    // not have to say. loadPaths takes one role per batch, so a mixed choice is
    // two calls — manuscripts first, because that is the one that may ask about
    // replacing manual edits, and it reads oddly after the translations have
    // already gone in.
    QStringList manuscripts;
    QStringList translations;
    for (const QString &path : paths) {
        if (QFileInfo(path).completeBaseName().endsWith(
                QStringLiteral("_translation"), Qt::CaseInsensitive)) {
            translations.append(path);
        } else {
            manuscripts.append(path);
        }
    }

    if (!manuscripts.isEmpty()) {
        loadPaths(SourceRole::Manuscript, manuscripts);
    }
    if (!translations.isEmpty()) {
        loadPaths(SourceRole::Translation, translations);
    }
}

void AppController::downloadManuscripts()
{
    DownloadManuscriptsDialog dialog(m_dialogParent);
    dialog.exec();
    if (dialog.downloadedAnything()) {
        setMessage(QStringLiteral(
            "Downloaded. Use Load manuscripts to open what you have taken."));
    }
}

void AppController::loadSources(SourceRole role)
{
    // The library first, for manuscripts: what was downloaded is what an editor
    // most often wants, and it knows which files are translations, so they load
    // the right way round without being asked. Browsing is still one click away
    // — the corpus in tools/data/01_osis never passes through the library.
    if (role == SourceRole::Manuscript) {
        ManuscriptLibraryDialog library(m_dialogParent);
        if (!library.isEmpty()) {
            if (library.exec() != QDialog::Accepted) {
                return;
            }
            if (!library.wantsToBrowse()) {
                loadLibraryFiles(library.chosenFiles());
                return;
            }
        }
    }

    const QString title = role == SourceRole::Translation
        ? QStringLiteral("Load translation OSIS files")
        : QStringLiteral("Load manuscript OSIS files");

    const QStringList paths = QFileDialog::getOpenFileNames(
        m_dialogParent,
        title,
        lastDirectory(),
        QStringLiteral("OSIS files (*.osis *.xml);;All files (*)"));

    // Reopen the same folder next time, even if the load below is cancelled or
    // rejected further down.
    if (!paths.isEmpty()) {
        rememberDirectory(paths.first());
    }

    loadPaths(role, paths);
}

void AppController::loadPaths(SourceRole role, const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }

    const bool isTranslation = role == SourceRole::Translation;

    if (!isTranslation && hasManualEdits()
        && !confirm(QStringLiteral(
            "Adding manuscripts regenerates Combined and replaces manual edits. Continue?"))) {
        return;
    }

    QList<OpenedFile> opened;
    for (const QString &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            setMessage(QStringLiteral("Could not read %1.").arg(path));
            return;
        }
        const QByteArray contents = file.read(MaxOsisSize + 1);
        if (contents.size() > MaxOsisSize) {
            setMessage(QStringLiteral("OSIS file is too large: %1").arg(path));
            return;
        }
        opened.append(OpenedFile{QFileInfo(path).fileName(), QString::fromUtf8(contents)});
    }

    QList<SourceDocument> parsed;
    try {
        for (int index = 0; index < opened.size(); ++index) {
            ParseOptions options;
            options.id = makeSourceId(role, index);
            options.name = opened.at(index).name;
            options.role = role;
            parsed.append(parseOsis(opened.at(index).content, options));
        }
    } catch (const OsisError &error) {
        setMessage(error.message());
        return;
    }

    m_sources.append(parsed);
    const DocumentRefs nextManuscripts = manuscripts();

    if (!isTranslation) {
        if (m_priorityId.isEmpty() && !nextManuscripts.isEmpty()) {
            m_priorityId = nextManuscripts.first()->id;
        }
        m_combined = buildCombined(nextManuscripts);
        m_translationSpans.clear();

        for (TranslationAssociation &association : m_associations) {
            bool stillPresent = false;
            for (const SourceDocument *source : nextManuscripts) {
                if (source->id == association.manuscriptId) {
                    stillPresent = true;
                    break;
                }
            }
            if (stillPresent) {
                continue;
            }
            association.manuscriptId.clear();
            for (const SourceDocument &source : m_sources) {
                if (source.id == association.translationId) {
                    association.manuscriptId =
                        suggestAssociation(source, nextManuscripts);
                    break;
                }
            }
        }
    } else {
        for (const SourceDocument &translation : parsed) {
            TranslationAssociation association;
            association.translationId = translation.id;
            association.manuscriptId = suggestAssociation(translation, nextManuscripts);
            m_associations.append(association);
        }
    }

    rebuildLocations();
    const bool stillCovered =
        m_location.has_value() && indexOfLocation(*m_location) >= 0;
    if (!stillCovered) {
        m_location = m_locations.isEmpty()
            ? std::optional<Location>()
            : std::optional<Location>(m_locations.first().location);
    }

    rebuildAlignedVerses();
    refreshTranslationSpans();
    setDirty(true);

    setMessage(m_locations.isEmpty()
        ? QStringLiteral("The loaded manuscripts hold no chapters.")
        : QStringLiteral("%1 manuscript(s) loaded.").arg(nextManuscripts.size()));

    m_undoStack.clear();
    m_redoStack.clear();
    emit historyChanged();
    emit sourcesChanged();
}

bool AppController::confirmDiscard()
{
    if (!m_dirty) {
        return true;
    }

    // Save is the default, unlike confirm(), which defaults to No: that one
    // guards a single verse and this one guards the whole edition.
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        m_dialogParent,
        QStringLiteral("Milah"),
        QStringLiteral("This edition has changes you have not saved.\n"
                       "Save them before closing it?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    switch (answer) {
    case QMessageBox::Save:
        // Only a written file counts. The save dialog can be cancelled, and
        // treating that as a save is how the work would be lost by the very
        // step meant to keep it.
        return saveProject();
    case QMessageBox::Discard:
        return true;
    default:
        return false;
    }
}

void AppController::closeProject()
{
    if (!confirmDiscard()) {
        return;
    }

    // Everything a project is — the same ten the save writes out — and nothing
    // that outlives one. The dictionary, the Strong's row preference and the
    // last-used folder all stay: they belong to the editor, not the edition.
    m_sources.clear();
    m_associations.clear();
    m_priorityId.clear();
    m_combined.clear();
    m_translationSpans.clear();
    m_columnSplits.clear();
    m_chapterReferences.clear();
    m_verseReferences.clear();
    m_combinedNotes.clear();
    m_interlinearWords.clear();
    m_location.reset();

    // Not saved with a project, but the checkboxes are rebuilt from it, so a
    // stale tick would survive into an empty window.
    m_filters = ReviewFilters{};

    // Empties the derived caches, and validateSelection() inside the second
    // drops the selection and says so — which is what clears the Notes panel
    // and greys the word actions, neither of which rebuildAll() touches.
    rebuildLocations();
    rebuildAlignedVerses();

    m_undoStack.clear();
    m_redoStack.clear();
    setDirty(false);
    // The line the constructor opens with, so the status bar reads as it does
    // on a first launch.
    setMessage(QStringLiteral("Load two or more manuscript OSIS files to begin."));
    emit historyChanged();
    // rebuildAll() is the last statement of the window's constructor, and this
    // is what calls it. So an empty controller plus this signal *is* the
    // first-launch window; nothing has to be rebuilt by hand.
    emit sourcesChanged();
}

void AppController::openProject()
{
    if (!confirmDiscard()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Open Milah project"),
        lastDirectory(),
        QStringLiteral("Milah projects (*.milah)"));
    if (path.isEmpty()) {
        return;
    }

    rememberDirectory(path);
    loadProjectFrom(path);
}

void AppController::openRecentProject(const QString &path)
{
    if (!confirmDiscard()) {
        return;
    }
    if (!loadProjectFrom(path)) {
        // It was offered and it did not open. Leaving it on the menu would be
        // offering it again.
        forgetRecentFile(QLatin1String(RecentProjectsKey), path);
        return;
    }
    rememberDirectory(path);
}

bool AppController::loadProjectFrom(const QString &path)
{
    QString error;
    const QJsonObject json = ProjectStorage::loadFromPath(path, &error);
    if (!error.isEmpty()) {
        setMessage(error);
        return false;
    }

    try {
        const ProjectState state = restoreProject(payloadFromJson(json));
        m_sources = state.sources;
        m_associations = state.associations;
        m_priorityId = state.priorityManuscriptId;
        m_combined = state.combined;
        m_translationSpans = state.translationSpans;
        m_columnSplits = state.columnSplits;
        m_chapterReferences = state.chapterReferences;
        m_verseReferences = state.verseReferences;
        m_combinedNotes = state.combinedNotes;
        m_interlinearWords = state.interlinearWords;
        m_location = state.location;
    } catch (const ProjectError &projectError) {
        setMessage(projectError.message());
        return false;
    } catch (const OsisError &osisError) {
        setMessage(osisError.message());
        return false;
    }

    rebuildLocations();
    rebuildAlignedVerses();
    m_undoStack.clear();
    m_redoStack.clear();
    setDirty(false);
    rememberRecentFile(QLatin1String(RecentProjectsKey), path);
    setMessage(QStringLiteral("Milah project opened."));
    emit historyChanged();
    emit sourcesChanged();
    return true;
}

bool AppController::saveProject()
{
    const QString osis = serializeCombinedOsis(m_combined);

    ProjectState state;
    state.sources = m_sources;
    state.associations = m_associations;
    state.priorityManuscriptId = m_priorityId;
    state.combined = m_combined;
    state.translationSpans = m_translationSpans;
    state.columnSplits = m_columnSplits;
    state.chapterReferences = m_chapterReferences;
    state.verseReferences = m_verseReferences;
    state.combinedNotes = m_combinedNotes;
    state.interlinearWords = m_interlinearWords;
    state.location = m_location;

    const MilahProjectPayload payload = projectPayload(state, osis);

    QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Save Milah project"),
        payload.suggestedName,
        QStringLiteral("Milah projects (*.milah)"));
    if (path.isEmpty()) {
        setMessage(QStringLiteral("Project save was cancelled."));
        return false;
    }
    if (!path.endsWith(QStringLiteral(".milah"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".milah");
    }

    QString error;
    if (!ProjectStorage::saveToPath(path, payloadToJson(payload), &error)) {
        setMessage(error.isEmpty() ? QStringLiteral("Could not save the project.") : error);
        return false;
    }

    setDirty(false);
    // Saving counts as much as opening. This class keeps no path of its own, so
    // every save is a Save As — which makes this the only way a project's
    // whereabouts is learned other than the open dialog.
    rememberRecentFile(QLatin1String(RecentProjectsKey), path);
    rememberDirectory(path);
    setMessage(QStringLiteral("Milah project saved."));
    return true;
}

CombinedApparatus AppController::editorApparatus() const
{
    CombinedApparatus apparatus;
    for (auto item = m_combinedNotes.constBegin();
         item != m_combinedNotes.constEnd();
         ++item) {
        const int separator = item.key().lastIndexOf(QLatin1Char(':'));
        if (separator < 0) {
            continue;
        }
        const QString verseId = item.key().left(separator);
        bool numeric = false;
        const int columnIndex =
            QStringView(item.key()).mid(separator + 1).toInt(&numeric);
        if (!numeric) {
            continue;
        }

        const auto draft = m_combined.constFind(verseId);
        if (draft == m_combined.constEnd()) {
            continue;
        }

        SourceNote note;
        note.id = item.key();
        note.text = item.value();
        note.charOffset = columnCharOffset(*draft, columnIndex);
        apparatus.notes[verseId].append(note);
    }

    // Anchored notes are written in the order they are given, so put each
    // verse's in the order they appear in it rather than in map order.
    for (QList<SourceNote> &notes : apparatus.notes) {
        std::stable_sort(
            notes.begin(),
            notes.end(),
            [](const SourceNote &left, const SourceNote &right) {
                return left.charOffset < right.charOffset;
            });

        // Numbered once they are in order. Without this every note in a verse
        // exports as n="" and osisID="…!note.", which is one identifier for all
        // of them — and an OSIS id is supposed to name one thing.
        for (int index = 0; index < notes.size(); ++index) {
            notes[index].number = QString::number(index + 1);
        }
    }
    return apparatus;
}

InterlinearGlosses AppController::interlinearGlosses() const
{
    // Read off the Interlinear row rather than off the spans, so what is
    // exported is exactly what the editor sees — including anything they typed
    // where no translation reaches, and nothing where they emptied a cell.
    InterlinearGlosses glosses;
    for (auto draft = m_combined.constBegin(); draft != m_combined.constEnd(); ++draft) {
        const QString &verseId = draft.key();
        for (int column = 0; column < draft->columns.size(); ++column) {
            const QString gloss = interlinearWord(verseId, column);
            if (!gloss.isEmpty()) {
                glosses[verseId].insert(column, gloss);
            }
        }
    }
    return glosses;
}

bool AppController::writeOsis(const QString &path, const QString &osis)
{
    QSaveFile file(path);
    const QByteArray contents = osis.toUtf8();
    return file.open(QIODevice::WriteOnly)
        && file.write(contents) == contents.size()
        && file.commit();
}

void AppController::exportCombined()
{
    QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Export Combined OSIS"),
        QStringLiteral("Milah_Combined.osis"),
        QStringLiteral("OSIS files (*.osis *.xml)"));
    if (path.isEmpty()) {
        setMessage(QStringLiteral("Export was cancelled."));
        return;
    }
    if (!path.endsWith(QStringLiteral(".osis"), Qt::CaseInsensitive)
        && !path.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".osis");
    }

    // The edition itself, carrying no editor's commentary.
    if (!writeOsis(path, serializeCombinedOsis(m_combined))) {
        setMessage(QStringLiteral("Could not export Combined OSIS."));
        return;
    }

    // Companions beside the chosen file, each written only when it has
    // something to carry: the edition itself stays one clean document.
    const QFileInfo chosen(path);
    QStringList written{chosen.fileName()};

    const auto sibling = [&chosen](const QString &suffix) {
        return chosen.dir().filePath(QStringLiteral("%1-%2.%3")
                                         .arg(chosen.completeBaseName(),
                                              suffix,
                                              chosen.suffix()));
    };

    if (!m_combinedNotes.isEmpty()) {
        const QString annotated = sibling(QStringLiteral("notes"));
        if (!writeOsis(
                annotated, serializeCombinedOsis(m_combined, {}, editorApparatus()))) {
            setMessage(QStringLiteral("Exported %1, but could not write %2.")
                           .arg(chosen.fileName(), QFileInfo(annotated).fileName()));
            return;
        }
        written.append(QFileInfo(annotated).fileName());
    }

    const InterlinearGlosses glosses = interlinearGlosses();
    if (!glosses.isEmpty()) {
        const QString interlinear = sibling(QStringLiteral("interlinear"));
        if (!writeOsis(interlinear, serializeInterlinearOsis(m_combined, glosses))) {
            setMessage(QStringLiteral("Exported %1, but could not write %2.")
                           .arg(chosen.fileName(), QFileInfo(interlinear).fileName()));
            return;
        }
        written.append(QFileInfo(interlinear).fileName());
    }

    setMessage(written.size() == 1
        ? QStringLiteral("Combined OSIS exported.")
        : QStringLiteral("Exported %1.").arg(written.join(QStringLiteral(", "))));
}

void AppController::setLocation(const Location &location)
{
    if (m_location.has_value() && *m_location == location) {
        return;
    }
    m_location = location;
    rebuildAlignedVerses();
    refreshTranslationSpans();
    emit locationChanged();
}

void AppController::goToPreviousLocation()
{
    if (!m_location.has_value()) {
        return;
    }
    const int index = indexOfLocation(*m_location);
    if (index > 0) {
        setLocation(m_locations.at(index - 1).location);
    }
}

void AppController::goToNextLocation()
{
    if (!m_location.has_value()) {
        return;
    }
    const int index = indexOfLocation(*m_location);
    if (index >= 0 && index < m_locations.size() - 1) {
        setLocation(m_locations.at(index + 1).location);
    }
}

void AppController::setPriorityId(const QString &id)
{
    if (id.isEmpty() || !m_location.has_value()) {
        return;
    }

    // The toolbar speaks for the chapter on screen and for nothing else. One
    // entry records it, and `m_priorityId` is deliberately left alone so that a
    // chapter never chosen for keeps falling back to the manuscript the project
    // was loaded with, however many other chapters have since been changed.
    //
    // The words are not touched: changing what a chapter is read against is not
    // the same as rewriting what the editor has settled. Regenerate does that.
    QMap<QString, QString> next = m_chapterReferences;
    next.insert(locationKey(*m_location), id);
    if (next == m_chapterReferences) {
        return;
    }

    commitCombined(
        m_combined,
        m_columnSplits,
        next,
        m_verseReferences,
        m_combinedNotes,
        m_interlinearWords);
    rebuildAlignedVerses();
    refreshTranslationSpans();
    emit locationChanged();
}

void AppController::regenerate()
{
    if (regenerateChapter()) {
        rebuildAlignedVerses();
        emit locationChanged();
    }
}

void AppController::undo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }
    m_redoStack.append(currentStep());
    restoreStep(m_undoStack.takeLast());
    // A step may have divided a word, so the columns are built again before
    // the cards that read them are.
    rebuildAlignedVerses();
    setDirty(true);
    emit historyChanged();
    emit locationChanged();
}

void AppController::redo()
{
    if (m_redoStack.isEmpty()) {
        return;
    }
    m_undoStack.append(currentStep());
    restoreStep(m_redoStack.takeLast());
    rebuildAlignedVerses();
    setDirty(true);
    emit historyChanged();
    emit locationChanged();
}

void AppController::setAssociation(
    const QString &translationId,
    const QString &manuscriptId)
{
    bool found = false;
    bool changed = false;
    for (TranslationAssociation &association : m_associations) {
        if (association.translationId == translationId) {
            changed = association.manuscriptId != manuscriptId;
            association.manuscriptId = manuscriptId;
            found = true;
            break;
        }
    }
    if (!found) {
        m_associations.append(TranslationAssociation{translationId, manuscriptId});
        changed = !manuscriptId.isEmpty();
    }

    if (changed) {
        // The columns a translation is spread across come from its manuscript,
        // so naming a different one makes every existing span wrong. They are
        // dropped rather than nudged, and refreshed below for the chapter on
        // screen; the rest are spread again as each is opened. Corrections made
        // by hand go with them, which is why it is worth saying so.
        const auto before = m_translationSpans.size();
        m_translationSpans.removeIf([&translationId](const TranslationSpan &span) {
            return span.translationId == translationId;
        });
        if (before != m_translationSpans.size()) {
            setMessage(QStringLiteral(
                "Re-aligned the translation; any spans you had corrected were "
                "spread again."));
        }
        refreshTranslationSpans();
    }

    setDirty(true);
    emit locationChanged();
}

Qt::LayoutDirection AppController::readingDirection() const
{
    const DocumentRefs sources = manuscripts();
    if (sources.isEmpty()) {
        return Qt::LeftToRight;
    }

    const SourceDocument *reference = sources.first();
    for (const SourceDocument *source : sources) {
        if (source->id == m_priorityId) {
            reference = source;
            break;
        }
    }
    return isRightToLeft(reference->metadata.language) ? Qt::RightToLeft
                                                       : Qt::LeftToRight;
}

void AppController::setStrongsVisible(bool visible)
{
    if (visible == m_strongsVisible) {
        return;
    }
    m_strongsVisible = visible;
    emit displayOptionsChanged();
}

void AppController::addToDictionary(const QString &word)
{
    if (word.trimmed().isEmpty()) {
        return;
    }

    // Asked before anything is written: cancelling has to leave the dictionary
    // as it was, or the dialog would be describing a decision already taken.
    DictionaryEntryDialog dialog(
        word, m_dictionary.definitionsFor(word), m_dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!m_dictionary.save(word, dialog.definitions())) {
        setMessage(
            QStringLiteral("Could not save the word to your dictionary; it will "
                           "be forgotten when Milah closes."));
    }
    m_acceptedForms.unite(m_dictionary.keys());
    emit displayOptionsChanged();
}

void AppController::saveDictionaryAs()
{
    if (m_dictionary.isEmpty()) {
        setMessage(QStringLiteral("There is nothing in your dictionary yet."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Save my dictionary as"),
        lastDirectory().isEmpty()
            ? QStringLiteral("milah-dictionary.json")
            : lastDirectory() + QStringLiteral("/milah-dictionary.json"),
        QStringLiteral("Milah dictionary (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    rememberDirectory(path);

    if (!m_dictionary.writeTo(path)) {
        setMessage(QStringLiteral("Could not write %1.").arg(QFileInfo(path).fileName()));
        return;
    }
    setMessage(
        QStringLiteral("Saved your dictionary to %1.").arg(QFileInfo(path).fileName()));
}

void AppController::loadDictionary()
{
    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Load a dictionary"),
        lastDirectory(),
        QStringLiteral("Milah dictionary (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    rememberDirectory(path);

    const QString name = QFileInfo(path).fileName();
    const int changed = m_dictionary.mergeFrom(path);
    if (changed < 0) {
        setMessage(
            QStringLiteral("%1 is not a Milah dictionary; nothing was changed.")
                .arg(name));
        return;
    }
    if (changed == 0) {
        // The merge keeps everything and adds only what is new, so re-reading a
        // file already taken in is a no-op rather than a doubling. Saying so is
        // better than looking like nothing happened.
        setMessage(
            QStringLiteral("Nothing new in %1; your dictionary already has it all.")
                .arg(name));
        return;
    }

    m_acceptedForms.unite(m_dictionary.keys());
    emit displayOptionsChanged();
    setMessage(
        changed == 1
            ? QStringLiteral("Added one word from %1.").arg(name)
            : QStringLiteral("Added %1 words from %2.").arg(changed).arg(name));
}

void AppController::applyColumn(
    const QString &verseId,
    int columnIndex,
    const ConsensusColumn &column)
{
    const AlignedVerse *aligned = nullptr;
    for (const AlignedVerse &candidate : m_alignedVerses) {
        if (candidate.reference.id == verseId) {
            aligned = &candidate;
            break;
        }
    }
    if (!aligned || columnIndex < 0 || columnIndex >= aligned->columns.size()) {
        return;
    }

    CombinedDraft draft = draftFor(*aligned);
    if (columnIndex >= draft.columns.size()) {
        return;
    }

    // A project written before the Combined row became editable can still carry
    // whole-verse manual text, which combinedText() prefers over the columns.
    // Settling a word takes the verse back to its columns, so that the row and
    // the preview agree again.
    if (draft.manualText.has_value()
        && !confirm(QStringLiteral(
            "Replace the manual text for this verse with the chosen words?"))) {
        return;
    }

    const ConsensusColumn &existing = draft.columns.at(columnIndex);
    if (!draft.manualText.has_value() && existing.text == column.text
        && existing.sourceId == column.sourceId && !existing.needsReview) {
        return;
    }

    draft.columns[columnIndex] = column;
    draft.manualText.reset();

    QMap<QString, CombinedDraft> next = m_combined;
    next.insert(verseId, draft);
    commitCombined(next);
    emit verseChanged(verseId);
}

void AppController::chooseToken(
    const QString &verseId,
    int columnIndex,
    const QString &sourceId)
{
    const AlignedVerse *aligned = nullptr;
    for (const AlignedVerse &candidate : m_alignedVerses) {
        if (candidate.reference.id == verseId) {
            aligned = &candidate;
            break;
        }
    }
    if (!aligned || columnIndex < 0 || columnIndex >= aligned->columns.size()) {
        return;
    }

    // The reading came from a witness rather than from the keyboard, so the
    // column records which one: the apparatus reports where a reading came from.
    ConsensusColumn column;
    column.sourceId = sourceId;
    column.needsReview = false;
    if (const SourceToken *token = aligned->columns.at(columnIndex).cell(sourceId)) {
        column.text = token->text;
    }
    applyColumn(verseId, columnIndex, column);
}

void AppController::splitColumn(const QString &verseId, int columnIndex)
{
    const AlignedVerse *aligned = nullptr;
    for (const AlignedVerse &candidate : m_alignedVerses) {
        if (candidate.reference.id == verseId) {
            aligned = &candidate;
            break;
        }
    }
    if (!aligned || columnIndex < 0 || columnIndex >= aligned->columns.size()) {
        return;
    }

    CombinedDraft draft = draftFor(*aligned);
    if (columnIndex >= draft.columns.size()) {
        return;
    }
    const QStringList words = dividedWords(draft.columns.at(columnIndex).text.value_or(QString()));
    if (words.size() < 2) {
        return;
    }

    if (draft.manualText.has_value()
        && !confirm(QStringLiteral(
            "Replace the manual text for this verse with the chosen words?"))) {
        return;
    }

    QList<int> splits = m_columnSplits.value(verseId);
    const ColumnGroup group =
        columnGroupFor(splits, int(aligned->columns.size()), columnIndex);
    if (group.original < 0) {
        return;
    }
    const int groupStart = group.start;
    const int groupSize = group.size;

    QStringList groupWords;
    for (int offset = 0; offset < groupSize; ++offset) {
        const int column = groupStart + offset;
        if (column == columnIndex) {
            groupWords.append(words);
        } else if (column < draft.columns.size()) {
            groupWords.append(draft.columns.at(column).text.value_or(QString()));
        } else {
            groupWords.append(QString());
        }
    }

    // The new column joins the end of its group, which is where
    // applyColumnSplits will put it once the alignment is rebuilt.
    const int inserted = groupStart + groupSize;
    splits.append(group.original);

    // Spans hold column indices and are deliberately not regenerated once the
    // editor has corrected them, so they have to be moved by hand.
    for (TranslationSpan &span : m_translationSpans) {
        if (span.verseId != verseId) {
            continue;
        }
        if (span.columnStart >= inserted) {
            span.columnStart += 1;
        }
        if (span.columnEnd > inserted) {
            span.columnEnd += 1;
        }
    }

    ConsensusColumn blank;
    draft.columns.insert(inserted, blank);
    for (int offset = 0; offset < groupWords.size(); ++offset) {
        ConsensusColumn column;
        column.needsReview = false;
        const QString word = groupWords.at(offset).trimmed();
        if (!word.isEmpty()) {
            column.text = word;
        }
        draft.columns[groupStart + offset] = column;
    }
    draft.manualText.reset();

    QMap<QString, QList<int>> nextSplits = m_columnSplits;
    nextSplits.insert(verseId, splits);
    QMap<QString, CombinedDraft> next = m_combined;
    next.insert(verseId, draft);
    commitCombined(
        next,
        nextSplits,
        m_chapterReferences,
        m_verseReferences,
        shiftColumnKeys(m_combinedNotes, verseId, inserted, 1),
        shiftColumnKeys(m_interlinearWords, verseId, inserted, 1));

    rebuildAlignedVerses();
    // The verse has a column it did not have, and each card holds its own copy
    // of the alignment, so the chapter is rebuilt rather than refreshed.
    emit locationChanged();
}

bool AppController::canMergeWithPrevious(const QString &verseId, int columnIndex) const
{
    for (const AlignedVerse &aligned : m_alignedVerses) {
        if (aligned.reference.id != verseId) {
            continue;
        }
        const ColumnGroup group = columnGroupFor(
            m_columnSplits.value(verseId), int(aligned.columns.size()), columnIndex);
        return group.original >= 0 && columnIndex > group.start;
    }
    return false;
}

bool AppController::canMergeWithNext(const QString &verseId, int columnIndex) const
{
    for (const AlignedVerse &aligned : m_alignedVerses) {
        if (aligned.reference.id != verseId) {
            continue;
        }
        const ColumnGroup group = columnGroupFor(
            m_columnSplits.value(verseId), int(aligned.columns.size()), columnIndex);
        return group.original >= 0 && columnIndex + 1 < group.start + group.size;
    }
    return false;
}

void AppController::mergeColumns(const QString &verseId, int firstColumnIndex)
{
    const AlignedVerse *aligned = nullptr;
    for (const AlignedVerse &candidate : m_alignedVerses) {
        if (candidate.reference.id == verseId) {
            aligned = &candidate;
            break;
        }
    }
    if (!aligned || firstColumnIndex < 0
        || firstColumnIndex + 1 >= aligned->columns.size()) {
        return;
    }

    QList<int> splits = m_columnSplits.value(verseId);
    const ColumnGroup group =
        columnGroupFor(splits, int(aligned->columns.size()), firstColumnIndex);
    // Only two columns of one divided word may be joined. Where the witnesses
    // themselves read two words, the division is theirs and not the editor's,
    // so there is nothing here to undo.
    if (group.original < 0 || firstColumnIndex + 1 >= group.start + group.size) {
        return;
    }

    CombinedDraft draft = draftFor(*aligned);
    if (firstColumnIndex + 1 >= draft.columns.size()) {
        return;
    }
    if (draft.manualText.has_value()
        && !confirm(QStringLiteral(
            "Replace the manual text for this verse with the chosen words?"))) {
        return;
    }

    QStringList joined;
    for (const int column : {firstColumnIndex, firstColumnIndex + 1}) {
        const QString word = draft.columns.at(column).text.value_or(QString()).trimmed();
        if (!word.isEmpty()) {
            joined.append(word);
        }
    }

    const int removed = firstColumnIndex + 1;
    splits.removeOne(group.original);

    // The mirror of the shift a division makes; spans are not regenerated once
    // corrected, so they are moved by hand here too.
    for (TranslationSpan &span : m_translationSpans) {
        if (span.verseId != verseId) {
            continue;
        }
        if (span.columnStart > removed) {
            span.columnStart -= 1;
        }
        if (span.columnEnd > removed) {
            span.columnEnd -= 1;
        }
        span.columnEnd = std::max(span.columnEnd, span.columnStart + 1);
    }

    ConsensusColumn column;
    column.needsReview = false;
    if (!joined.isEmpty()) {
        column.text = joined.join(QLatin1Char(' '));
    }
    draft.columns[firstColumnIndex] = column;
    draft.columns.removeAt(removed);
    draft.manualText.reset();

    QMap<QString, QList<int>> nextSplits = m_columnSplits;
    if (splits.isEmpty()) {
        nextSplits.remove(verseId);
    } else {
        nextSplits.insert(verseId, splits);
    }
    QMap<QString, CombinedDraft> next = m_combined;
    next.insert(verseId, draft);
    commitCombined(
        next,
        nextSplits,
        m_chapterReferences,
        m_verseReferences,
        shiftColumnKeys(m_combinedNotes, verseId, removed, -1),
        shiftColumnKeys(m_interlinearWords, verseId, removed, -1));

    rebuildAlignedVerses();
    emit locationChanged();
}

void AppController::setColumnText(
    const QString &verseId,
    int columnIndex,
    const QString &text)
{
    const QString trimmed = text.trimmed();

    ConsensusColumn column;
    column.needsReview = false;
    if (!trimmed.isEmpty()) {
        column.text = trimmed;
    }
    applyColumn(verseId, columnIndex, column);
}

void AppController::setManualText(const QString &verseId, const QString &text)
{
    const AlignedVerse *aligned = nullptr;
    for (const AlignedVerse &candidate : m_alignedVerses) {
        if (candidate.reference.id == verseId) {
            aligned = &candidate;
            break;
        }
    }
    if (!aligned) {
        return;
    }

    CombinedDraft draft = draftFor(*aligned);
    if (draft.manualText.has_value() && *draft.manualText == text) {
        return;
    }
    draft.manualText = text;

    QMap<QString, CombinedDraft> next = m_combined;
    next.insert(verseId, draft);
    commitCombined(next);
    emit verseChanged(verseId);
}

QStringList AppController::spanWords(const TranslationSpan &span) const
{
    for (const SourceDocument &source : m_sources) {
        if (source.id != span.translationId) {
            continue;
        }
        const SourceVerse *verse = source.verse(span.verseId);
        if (!verse) {
            return {};
        }
        QStringList words;
        for (int index = span.tokenStart;
             index < span.tokenEnd && index < verse->tokens.size();
             ++index) {
            words.append(verse->tokens.at(index).text);
        }
        return words;
    }
    return {};
}

void AppController::removeSpan(const QString &spanId, bool removed)
{
    for (TranslationSpan &span : m_translationSpans) {
        if (span.id != spanId) {
            continue;
        }
        if (span.removed == removed) {
            return;
        }
        span.removed = removed;
        setDirty(true);
        emit verseChanged(span.verseId);
        return;
    }
}

void AppController::moveSpan(const QString &spanId, int delta)
{
    for (TranslationSpan &span : m_translationSpans) {
        if (span.id != spanId) {
            continue;
        }

        int columnCount = 0;
        for (const AlignedVerse &aligned : m_alignedVerses) {
            if (aligned.reference.id == span.verseId) {
                columnCount = int(aligned.columns.size());
                break;
            }
        }

        const int width = span.columnEnd - span.columnStart;
        const int start =
            std::max(0, std::min(columnCount - width, span.columnStart + delta));
        span.columnStart = start;
        span.columnEnd = start + width;
        setDirty(true);
        emit verseChanged(span.verseId);
        return;
    }
}

void AppController::resizeSpan(const QString &spanId, int delta)
{
    for (TranslationSpan &span : m_translationSpans) {
        if (span.id != spanId) {
            continue;
        }

        int columnCount = 0;
        for (const AlignedVerse &aligned : m_alignedVerses) {
            if (aligned.reference.id == span.verseId) {
                columnCount = int(aligned.columns.size());
                break;
            }
        }

        span.columnEnd = std::max(
            span.columnStart + 1,
            std::min(columnCount, span.columnEnd + delta));
        setDirty(true);
        emit verseChanged(span.verseId);
        return;
    }
}

void AppController::mergeSpan(const QString &spanId)
{
    int selectedIndex = -1;
    for (int index = 0; index < m_translationSpans.size(); ++index) {
        if (m_translationSpans.at(index).id == spanId) {
            selectedIndex = index;
            break;
        }
    }
    if (selectedIndex < 0) {
        return;
    }

    const TranslationSpan selected = m_translationSpans.at(selectedIndex);

    int nextIndex = -1;
    for (int index = 0; index < m_translationSpans.size(); ++index) {
        const TranslationSpan &candidate = m_translationSpans.at(index);
        if (candidate.translationId != selected.translationId
            || candidate.verseId != selected.verseId
            || candidate.tokenStart < selected.tokenEnd) {
            continue;
        }
        if (nextIndex < 0
            || candidate.tokenStart < m_translationSpans.at(nextIndex).tokenStart) {
            nextIndex = index;
        }
    }
    if (nextIndex < 0) {
        return;
    }

    const TranslationSpan next = m_translationSpans.at(nextIndex);
    m_translationSpans[selectedIndex].tokenEnd = next.tokenEnd;
    m_translationSpans[selectedIndex].columnEnd =
        std::max(selected.columnEnd, next.columnEnd);
    m_translationSpans[selectedIndex].confidence = SpanConfidence::Low;
    m_translationSpans.removeAt(nextIndex);

    setDirty(true);
    emit verseChanged(selected.verseId);
}

void AppController::splitSpan(const QString &spanId)
{
    for (int index = 0; index < m_translationSpans.size(); ++index) {
        TranslationSpan selected = m_translationSpans.at(index);
        if (selected.id != spanId) {
            continue;
        }
        if (selected.tokenEnd - selected.tokenStart < 2) {
            return;
        }

        const int tokenMiddle = (selected.tokenStart + selected.tokenEnd + 1) / 2;
        const bool hasColumnRoom = selected.columnEnd - selected.columnStart >= 2;
        const int columnMiddle = hasColumnRoom
            ? (selected.columnStart + selected.columnEnd + 1) / 2
            : selected.columnStart;

        TranslationSpan head = selected;
        head.tokenEnd = tokenMiddle;
        if (hasColumnRoom) {
            head.columnEnd = columnMiddle;
        }
        head.confidence = SpanConfidence::Low;

        TranslationSpan tail = selected;
        tail.id = QStringLiteral("%1:split:%2").arg(selected.id).arg(tokenMiddle);
        tail.tokenStart = tokenMiddle;
        if (hasColumnRoom) {
            tail.columnStart = columnMiddle;
        }
        tail.confidence = SpanConfidence::Low;

        m_translationSpans[index] = head;
        m_translationSpans.insert(index + 1, tail);

        setDirty(true);
        emit verseChanged(selected.verseId);
        return;
    }
}

} // namespace milah
