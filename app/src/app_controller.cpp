#include "app_controller.h"

#include "core/coverage.h"
#include "core/data_paths.h"
#include "core/lexicon.h"
#include "core/osis.h"
#include "core/project.h"
#include "core/serialize.h"
#include "core/tokenize.h"
#include "project_storage.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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
    return generateCombined(aligned, manuscripts(), m_priorityId);
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
    // A divided word is the editor's work as much as a typed one, and
    // regenerating discards it, so it earns the same warning.
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
    const DocumentRefs &manuscriptList,
    const QString &priority) const
{
    QMap<QString, CombinedDraft> result;
    for (const Location &location : commonLocations(manuscriptList)) {
        for (const QString &verseId : verseIdsAtLocation(manuscriptList, location)) {
            const AlignedVerse aligned = alignedFor(verseId, manuscriptList, priority);
            result.insert(verseId, generateCombined(aligned, manuscriptList, priority));
        }
    }
    return result;
}

void AppController::rebuildLocations()
{
    m_locations = commonLocations(manuscripts());
}

AlignedVerse AppController::alignedFor(
    const QString &verseId,
    const DocumentRefs &sources,
    const QString &priorityId) const
{
    return applyColumnSplits(
        alignVerse(verseId, sources, priorityId), m_columnSplits.value(verseId));
}

void AppController::rebuildAlignedVerses()
{
    m_alignedVerses.clear();
    if (!m_location.has_value()) {
        return;
    }

    const DocumentRefs sources = manuscripts();
    for (const QString &verseId : verseIdsAtLocation(sources, *m_location)) {
        m_alignedVerses.append(alignedFor(verseId, sources, m_priorityId));
    }
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
                int(aligned.columns.size())));
        }
    }

    m_translationSpans = next;
}

void AppController::commitCombined(const QMap<QString, CombinedDraft> &next)
{
    commitCombined(next, m_columnSplits);
}

void AppController::commitCombined(
    const QMap<QString, CombinedDraft> &next,
    const QMap<QString, QList<int>> &nextSplits)
{
    m_undoStack.append(EditStep{m_combined, m_columnSplits});
    while (m_undoStack.size() > MaxUndoDepth) {
        m_undoStack.removeFirst();
    }
    m_redoStack.clear();
    m_combined = next;
    m_columnSplits = nextSplits;
    setDirty(true);
    emit historyChanged();
}

bool AppController::regenerateWith(const QString &nextPriority)
{
    if (hasManualEdits()
        && !confirm(QStringLiteral("Regenerate Combined and replace manual edits?"))) {
        return false;
    }

    // A divided column only ever holds a word the editor put there by hand, so
    // regenerating takes the divisions with the words; leaving them would strew
    // the verse with blank columns no witness reads. Cleared before the drafts
    // are built so the two agree on how many columns the verse has.
    const QMap<QString, QList<int>> divided = m_columnSplits;
    m_columnSplits.clear();
    const QMap<QString, CombinedDraft> regenerated =
        buildCombined(manuscripts(), nextPriority);
    m_columnSplits = divided;

    commitCombined(regenerated, {});
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

void AppController::loadSources(SourceRole role)
{
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
        m_combined = buildCombined(nextManuscripts, m_priorityId);
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
    const bool stillCommon =
        m_location.has_value() && m_locations.contains(*m_location);
    if (!stillCommon) {
        m_location = m_locations.isEmpty()
            ? std::optional<Location>()
            : std::optional<Location>(m_locations.first());
    }

    rebuildAlignedVerses();
    refreshTranslationSpans();
    setDirty(true);

    setMessage(m_locations.isEmpty()
        ? QStringLiteral("The loaded manuscripts do not share a book and chapter.")
        : QStringLiteral("%1 manuscript(s) loaded.").arg(nextManuscripts.size()));

    m_undoStack.clear();
    m_redoStack.clear();
    emit historyChanged();
    emit sourcesChanged();
}

void AppController::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Open Milah project"),
        {},
        QStringLiteral("Milah projects (*.milah)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    const QJsonObject json = ProjectStorage::loadFromPath(path, &error);
    if (!error.isEmpty()) {
        setMessage(error);
        return;
    }

    try {
        const ProjectState state = restoreProject(payloadFromJson(json));
        m_sources = state.sources;
        m_associations = state.associations;
        m_priorityId = state.priorityManuscriptId;
        m_combined = state.combined;
        m_translationSpans = state.translationSpans;
        m_columnSplits = state.columnSplits;
        m_location = state.location;
    } catch (const ProjectError &projectError) {
        setMessage(projectError.message());
        return;
    } catch (const OsisError &osisError) {
        setMessage(osisError.message());
        return;
    }

    rebuildLocations();
    rebuildAlignedVerses();
    m_undoStack.clear();
    m_redoStack.clear();
    setDirty(false);
    setMessage(QStringLiteral("Milah project opened."));
    emit historyChanged();
    emit sourcesChanged();
}

void AppController::saveProject()
{
    const QString osis = serializeCombinedOsis(m_combined);

    ProjectState state;
    state.sources = m_sources;
    state.associations = m_associations;
    state.priorityManuscriptId = m_priorityId;
    state.combined = m_combined;
    state.translationSpans = m_translationSpans;
    state.columnSplits = m_columnSplits;
    state.location = m_location;

    const MilahProjectPayload payload = projectPayload(state, osis);

    QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Save Milah project"),
        payload.suggestedName,
        QStringLiteral("Milah projects (*.milah)"));
    if (path.isEmpty()) {
        setMessage(QStringLiteral("Project save was cancelled."));
        return;
    }
    if (!path.endsWith(QStringLiteral(".milah"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".milah");
    }

    QString error;
    if (!ProjectStorage::saveToPath(path, payloadToJson(payload), &error)) {
        setMessage(error.isEmpty() ? QStringLiteral("Could not save the project.") : error);
        return;
    }

    setDirty(false);
    setMessage(QStringLiteral("Milah project saved."));
}

void AppController::exportCombined()
{
    const QString osis = serializeCombinedOsis(m_combined);

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

    QSaveFile file(path);
    const QByteArray contents = osis.toUtf8();
    if (!file.open(QIODevice::WriteOnly)
        || file.write(contents) != contents.size()
        || !file.commit()) {
        setMessage(QStringLiteral("Could not export Combined OSIS."));
        return;
    }

    setMessage(QStringLiteral("Combined OSIS exported."));
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
    const int index = int(m_locations.indexOf(*m_location));
    if (index > 0) {
        setLocation(m_locations.at(index - 1));
    }
}

void AppController::goToNextLocation()
{
    if (!m_location.has_value()) {
        return;
    }
    const int index = int(m_locations.indexOf(*m_location));
    if (index >= 0 && index < m_locations.size() - 1) {
        setLocation(m_locations.at(index + 1));
    }
}

void AppController::setPriorityId(const QString &id)
{
    if (id == m_priorityId || id.isEmpty()) {
        return;
    }
    if (!regenerateWith(id)) {
        // The reader declined; tell the window to put the combo box back.
        emit sourcesChanged();
        return;
    }
    m_priorityId = id;
    m_translationSpans.clear();
    rebuildAlignedVerses();
    refreshTranslationSpans();
    emit sourcesChanged();
}

void AppController::regenerate()
{
    if (regenerateWith(m_priorityId)) {
        emit locationChanged();
    }
}

void AppController::undo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }
    m_redoStack.append(EditStep{m_combined, m_columnSplits});
    const EditStep step = m_undoStack.takeLast();
    m_combined = step.combined;
    m_columnSplits = step.columnSplits;
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
    m_undoStack.append(EditStep{m_combined, m_columnSplits});
    const EditStep step = m_redoStack.takeLast();
    m_combined = step.combined;
    m_columnSplits = step.columnSplits;
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
    for (TranslationAssociation &association : m_associations) {
        if (association.translationId == translationId) {
            association.manuscriptId = manuscriptId;
            found = true;
            break;
        }
    }
    if (!found) {
        m_associations.append(TranslationAssociation{translationId, manuscriptId});
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
    if (!m_dictionary.add(word)) {
        setMessage(
            QStringLiteral("Could not save the word to your dictionary; it will "
                           "be forgotten when Milah closes."));
    }
    m_acceptedForms.unite(m_dictionary.keys());
    emit displayOptionsChanged();
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
    commitCombined(next, nextSplits);

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
    commitCombined(next, nextSplits);

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
