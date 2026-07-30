#pragma once

#include "core/alignment.h"
#include "core/suggestions.h"
#include "core/types.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

class QWidget;

namespace milah {

/// Which verses the reader wants to see. With nothing ticked every verse is
/// shown.
struct ReviewFilters
{
    bool ties = false;
    bool manual = false;
    bool missing = false;
    bool uncertain = false;

    bool any() const { return ties || manual || missing || uncertain; }
};

/// Holds the whole editing session: the loaded witnesses, the Combined drafts,
/// translation alignment, undo history, and the file dialogs that read and
/// write them.
class AppController final : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QWidget *dialogParent, QObject *parent = nullptr);

    const QList<SourceDocument> &sources() const { return m_sources; }
    DocumentRefs manuscripts() const;
    DocumentRefs translations() const;
    const QList<TranslationAssociation> &associations() const { return m_associations; }
    QHash<QString, QString> associationMap() const;
    /// Short labels naming each source in the verse view, keyed by source id.
    QHash<QString, QString> acronyms() const { return sourceAcronyms(m_sources); }
    /// Which way the edition reads, taken from the reference manuscript.
    Qt::LayoutDirection readingDirection() const;
    /// Whether the interlinear Strong's line is drawn. A view preference, so
    /// it is not part of the project.
    bool strongsVisible() const { return m_strongsVisible; }
    void setStrongsVisible(bool visible);
    /// Words the editor has accepted, so the spelling checks stop asking.
    const UserDictionary &dictionary() const { return m_dictionary; }
    void addToDictionary(const QString &word);
    /// Everything the unknown-word check should let pass: what the editor has
    /// accepted, plus what the shipped corpora attest.
    const QSet<QString> &acceptedForms() const { return m_acceptedForms; }
    QString priorityId() const { return m_priorityId; }
    const QList<TranslationSpan> &translationSpans() const { return m_translationSpans; }
    const QList<Location> &locations() const { return m_locations; }
    std::optional<Location> location() const { return m_location; }
    const QList<AlignedVerse> &alignedVerses() const { return m_alignedVerses; }

    bool isDirty() const { return m_dirty; }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    QString message() const { return m_message; }
    /// Set when a data file Milah needs is missing. Empty when all is well.
    /// Kept out of message(), which any later action would overwrite.
    QString dataWarning() const { return m_dataWarning; }

    ReviewFilters filters() const { return m_filters; }
    void setFilters(const ReviewFilters &filters);

    /// The Combined draft for an aligned verse, generating one on the fly when
    /// the verse has never been committed.
    CombinedDraft draftFor(const AlignedVerse &aligned) const;

    /// True when this verse passes the active review filters.
    bool matchesFilters(const AlignedVerse &aligned) const;

public slots:
    /// Asks for OSIS files and loads them.
    void loadSources(milah::SourceRole role);
    /// Loads OSIS files that have already been chosen. Also how files named on
    /// the command line get in.
    void loadPaths(milah::SourceRole role, const QStringList &paths);
    void openProject();
    void saveProject();
    void exportCombined();

    void setLocation(const milah::Location &location);
    void goToPreviousLocation();
    void goToNextLocation();
    void setPriorityId(const QString &id);
    void regenerate();

    void undo();
    void redo();

    void setAssociation(const QString &translationId, const QString &manuscriptId);
    void chooseToken(const QString &verseId, int columnIndex, const QString &sourceId);
    /// Sets one Combined word by hand. An empty text drops the word.
    void setColumnText(const QString &verseId, int columnIndex, const QString &text);
    /// Divides a Combined word at its maqaf or space, giving the second half a
    /// column of its own. The witnesses are untouched: they still read one word
    /// there, so their rows show a gap beside it.
    void splitColumn(const QString &verseId, int columnIndex);
    /// Joins a column back into the one after it, undoing a division. Only two
    /// columns of the same divided word can be joined: where the witnesses
    /// themselves read two words, the division is not the editor's to undo.
    void mergeColumns(const QString &verseId, int firstColumnIndex);
    /// Whether this column and its neighbour are two halves of one divided
    /// word, and so can be joined. "Previous" and "next" are in reading order.
    bool canMergeWithPrevious(const QString &verseId, int columnIndex) const;
    bool canMergeWithNext(const QString &verseId, int columnIndex) const;
    void setManualText(const QString &verseId, const QString &text);

    void moveSpan(const QString &spanId, int delta);
    void resizeSpan(const QString &spanId, int delta);
    void mergeSpan(const QString &spanId);
    void splitSpan(const QString &spanId);

signals:
    /// The set of witnesses, the priority witness or the chapter list changed:
    /// the whole window needs rebuilding.
    void sourcesChanged();
    /// What the verse cards show changed, but not what they hold.
    void displayOptionsChanged();
    /// A different chapter is on screen.
    void locationChanged();
    /// One verse card needs refreshing.
    void verseChanged(const QString &verseId);
    void messageChanged(const QString &message);
    void dirtyChanged(bool dirty);
    void historyChanged();

private:
    void setMessage(const QString &message);
    void setDirty(bool dirty);
    void reportError(const QString &fallback);

    void rebuildLocations();
    void rebuildAlignedVerses();
    void refreshTranslationSpans();
    void commitCombined(const QMap<QString, CombinedDraft> &next);
    bool regenerateWith(const QString &nextPriority);
    /// The one place a verse's columns are produced. Both the on-screen
    /// alignment and the generated drafts go through it, because a difference
    /// of one column between them would silently misplace every reading after
    /// the split.
    AlignedVerse alignedFor(
        const QString &verseId,
        const DocumentRefs &sources,
        const QString &priorityId) const;
    void applyColumn(
        const QString &verseId,
        int columnIndex,
        const ConsensusColumn &column);
    QMap<QString, CombinedDraft> buildCombined(
        const DocumentRefs &manuscripts,
        const QString &priorityId) const;
    QString suggestAssociation(
        const SourceDocument &translation,
        const DocumentRefs &manuscripts) const;
    bool confirm(const QString &question);
    bool hasManualEdits() const;

    /// The folder last used in a load/open dialog, remembered across runs via
    /// QSettings. Empty until the user opens something.
    QString lastDirectory() const;
    /// Records the folder containing filePath as the last used directory.
    void rememberDirectory(const QString &filePath);

    QWidget *m_dialogParent = nullptr;

    QList<SourceDocument> m_sources;
    QList<TranslationAssociation> m_associations;
    QString m_priorityId;
    bool m_strongsVisible = true;
    UserDictionary m_dictionary{UserDictionary::defaultPath()};
    /// Cached because it is asked for once per verse card and the corpora run
    /// to six figures of forms.
    QSet<QString> m_acceptedForms;
    QMap<QString, CombinedDraft> m_combined;
    QList<TranslationSpan> m_translationSpans;
    QMap<QString, QList<int>> m_columnSplits;
    std::optional<Location> m_location;

    QList<Location> m_locations;
    QList<AlignedVerse> m_alignedVerses;

    /// What one undo step restores. The divided columns travel with the
    /// drafts: putting back the readings without also putting back the column
    /// count would leave a verse with more columns than readings to fill them,
    /// and every word after the division sitting one place out.
    struct EditStep
    {
        QMap<QString, CombinedDraft> combined;
        QMap<QString, QList<int>> columnSplits;
    };

    QList<EditStep> m_undoStack;
    QList<EditStep> m_redoStack;

    ReviewFilters m_filters;
    bool m_dirty = false;
    QString m_message;
    QString m_dataWarning;
    QString m_lastError;
};

} // namespace milah
