#pragma once

#include "core/alignment.h"
#include "core/collation_docx.h"
#include "core/coverage.h"
#include "core/serialize.h"
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

/// The Combined word the editor is working on. One word at a time, named by the
/// verse it sits in and its place among that verse's columns.
///
/// Several things need to know it and none of them can be asked: the notes
/// panel lives in a dock, the Edit menu on the window, and the word itself in a
/// verse card, so the answer is kept here where all three can reach it.
struct WordSelection
{
    QString verseId;
    int columnIndex = -1;

    bool isValid() const { return !verseId.isEmpty() && columnIndex >= 0; }
    bool operator==(const WordSelection &other) const
    {
        return verseId == other.verseId && columnIndex == other.columnIndex;
    }
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
    /// Asks what the word means, then accepts it. Cancelling the dialog leaves
    /// the dictionary untouched.
    void addToDictionary(const QString &word);
    /// Writes a copy of the dictionary somewhere the editor chooses, for their
    /// own backup. Where Milah keeps its own is unaffected.
    void saveDictionaryAs();
    /// Reads a saved dictionary and folds it in. Nothing already accepted is
    /// dropped and no definition is taken twice, so this is safe to repeat.
    void loadDictionary();
    /// Everything the unknown-word check should let pass: what the editor has
    /// accepted, plus what the shipped corpora attest.
    const QSet<QString> &acceptedForms() const { return m_acceptedForms; }
    /// The project's own reference: the fallback for chapters the editor has
    /// not spoken for. It does not drift as chapters are chosen for, so an
    /// untouched chapter always falls back to the same manuscript.
    QString priorityId() const { return m_priorityId; }
    /// What the toolbar shows: the reference of the chapter on screen, or the
    /// project's own where that chapter has none.
    QString currentReference() const;
    /// The manuscript this verse is read against — the editor's choice for it,
    /// otherwise the default, otherwise whichever manuscript actually has the
    /// verse. Never names a witness silent for it.
    QString referenceFor(const QString &verseId) const;
    /// The editor's own note on a Combined word, empty when there is none.
    QString combinedNote(const QString &verseId, int columnIndex) const;
    /// The interlinear word under a Combined column: the editor's wording where
    /// they have given one, otherwise the aligned translation's words joined by
    /// a dash — "to do" under one Hebrew word reads "to-do".
    QString interlinearWord(const QString &verseId, int columnIndex) const;
    const QList<TranslationSpan> &translationSpans() const { return m_translationSpans; }
    /// Every book and chapter any manuscript covers, each saying whether all of
    /// them do.
    const QList<LocationCoverage> &locations() const { return m_locations; }
    /// Where a location sits in that list, or -1.
    int indexOfLocation(const Location &location) const;
    std::optional<Location> location() const { return m_location; }
    const QList<AlignedVerse> &alignedVerses() const { return m_alignedVerses; }

    bool isDirty() const { return m_dirty; }
    /// The project file this came from, for the window to name. Recorded and
    /// nothing more — saving still asks where every time, as it always has.
    QString filePath() const { return m_filePath; }

    /// Asks what to do about unsaved work before it is thrown away.
    ///
    /// Returns true when the caller may go ahead: the editor either saved or
    /// chose to lose it. False means they changed their mind and nothing at
    /// all should happen. Answers true at once when there is nothing to lose,
    /// so a caller may ask unconditionally.
    ///
    /// Public because the window has to ask it too: closing the window is the
    /// commonest way an edition gets thrown away.
    bool confirmDiscard();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    QString message() const { return m_message; }
    /// Set when a data file Milah needs is missing. Empty when all is well.
    /// Kept out of message(), which any later action would overwrite.
    QString dataWarning() const { return m_dataWarning; }

    ReviewFilters filters() const { return m_filters; }
    void setFilters(const ReviewFilters &filters);

    /// The Combined word being worked on, if any.
    WordSelection selection() const { return m_selection; }
    /// Every manuscript note attached to the selected column, in the order the
    /// manuscripts are loaded. Empty when nothing is selected.
    QList<ColumnNote> selectedNotes() const;
    /// The selected Combined word itself, for menu entries that name it.
    QString selectedWord() const;

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
    /// Offers the published manuscripts for download into the local library.
    void downloadManuscripts();
    /// Shows what has been downloaded, and loads what is chosen from it.
    ///
    /// Apart from loadSources because the two answer different questions: this
    /// one knows which of its files are translations and needs nobody to say,
    /// while that one is for a text the library has never seen.
    void openLibrary();
    void openProject();
    /// Opens a project the reader chose from the Open Recent menu rather than
    /// from a dialog. Asks about unsaved work exactly as openProject does, and
    /// takes the file back out of the recent list if it will not open.
    void openRecentProject(const QString &path);
    /// Writes the project out. Returns true only when a file was actually
    /// written: the save dialog can be cancelled, and a caller about to throw
    /// the edition away has to be able to tell that apart from a save.
    bool saveProject();
    /// Puts the edition down and returns Milah to the state it starts in,
    /// asking about unsaved work first. Does nothing if that is declined.
    void closeProject();
    void exportCombined();
    /// Writes the current book as a Word document: every verse a table of the
    /// witnesses one above another, the differences coloured as they are on
    /// screen and the remarks as footnotes.
    void exportCollationWord();

    void setLocation(const milah::Location &location);
    void goToPreviousLocation();
    void goToNextLocation();
    void setPriorityId(const QString &id);
    void regenerate();

    void undo();
    void redo();

    /// Records which Combined word is being worked on. Passing an invalid
    /// column clears the selection.
    void selectWord(const QString &verseId, int columnIndex);

    /// Reads one verse against a different manuscript. An empty sourceId drops
    /// the choice, letting the verse follow the chapter's reference again.
    void setVerseReference(const QString &verseId, const QString &sourceId);
    /// Records the editor's note on a Combined word. An empty note removes it.
    void setCombinedNote(const QString &verseId, int columnIndex, const QString &note);
    /// Sets the interlinear word under a Combined column. An empty text drops
    /// the choice, so the cell follows the aligned translation again.
    void setInterlinearWord(const QString &verseId, int columnIndex, const QString &text);
    /// Drops a loaded translation, its association and its aligned spans.
    void closeTranslation(const QString &sourceId);
    /// Asks the Notes panel to take the caret, for "Add/edit note".
    void requestNoteEditing() { emit noteEditingRequested(); }

    void setAssociation(const QString &translationId, const QString &manuscriptId);
    void chooseToken(const QString &verseId, int columnIndex, const QString &sourceId);
    /// Sets one Combined word by hand. An empty text drops the word.
    void setColumnText(const QString &verseId, int columnIndex, const QString &text);
    /// Divides a Combined word at its space, giving the second half a column of
    /// its own. The witnesses are untouched, so their rows show a gap beside
    /// the new column.
    ///
    /// This used to be how a maqaf compound was taken apart. It is not needed
    /// for that any more: the parser now divides a witness's own reading at a
    /// maqaf or hyphen, because a joiner joins two words without making them
    /// one. What is left for this is a word the witnesses genuinely write as
    /// one and the edition wants as two.
    void splitColumn(const QString &verseId, int columnIndex);
    /// Joins a column back into the one after it, undoing a division. The two
    /// words are separated by a space: the maqaf a division was made at is not
    /// remembered, and the cell can be typed in if it is wanted back. Only two
    /// columns of the same divided word can be joined: where the witnesses
    /// themselves read two words, the division is not the editor's to undo.
    void mergeColumns(const QString &verseId, int firstColumnIndex);
    /// Whether this column and its neighbour are two halves of one divided
    /// word, and so can be joined. "Previous" and "next" are in reading order.
    bool canMergeWithPrevious(const QString &verseId, int columnIndex) const;
    bool canMergeWithNext(const QString &verseId, int columnIndex) const;
    void setManualText(const QString &verseId, const QString &text);

    /// The translation's own words for this group, which is what the
    /// Interlinear row derives its wording from.
    QStringList spanWords(const TranslationSpan &span) const;
    /// Takes a group out of the interlinear, and puts it back.
    void removeSpan(const QString &spanId, bool removed = true);

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
    /// A different Combined word is being worked on, or none is.
    void selectionChanged();
    /// The editor asked to write a note on the selected word.
    void noteEditingRequested();
    void messageChanged(const QString &message);
    void dirtyChanged(bool dirty);
    void historyChanged();

private:
    void setMessage(const QString &message);
    void setDirty(bool dirty);
    void reportError(const QString &fallback);

    void rebuildLocations();
    void rebuildAlignedVerses();
    /// Drops the selection when the word it names is no longer on screen — a
    /// different chapter, a reloaded source, or a verse that lost the column.
    /// Run after every rebuild, so nothing can describe a word that is gone.
    void validateSelection();
    void refreshTranslationSpans();
    /// The columns of this verse the translation's own manuscript occupies, in
    /// reading order — what its words are spread across. Every column when no
    /// manuscript is named or it is silent here.
    QList<int> associatedColumns(
        const QString &translationId,
        const AlignedVerse &aligned) const;
    /// The verses of the chapter on screen, in reading order.
    QStringList verseIdsInChapter() const;
    /// Rebuilds the drafts of the chapter on screen from its references,
    /// asking first when that would discard the editor's own work. False when
    /// there is nothing to do or the editor declined.
    bool regenerateChapter();
    /// One verse's draft, generated afresh against that verse's reference.
    CombinedDraft regeneratedDraft(
        const QString &verseId,
        const DocumentRefs &manuscriptList) const;

    void commitCombined(const QMap<QString, CombinedDraft> &next);
    /// Records one undo step and puts a verse's whole state in place at once.
    /// The columns, references and notes move with the words because an undo
    /// restoring one without the others would describe a verse by an alignment
    /// it no longer has, so a caller cannot change them separately and get the
    /// order wrong.
    void commitCombined(
        const QMap<QString, CombinedDraft> &next,
        const QMap<QString, QList<int>> &nextSplits,
        const QMap<QString, QString> &nextChapterReferences,
        const QMap<QString, QString> &nextVerseReferences,
        const QMap<QString, QString> &nextNotes,
        const QMap<QString, QString> &nextInterlinear);
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
    QMap<QString, CombinedDraft> buildCombined(const DocumentRefs &manuscripts) const;
    /// The editor's notes as apparatus, each anchored at its word's place in
    /// the verse text. Only for the annotated export; the plain one gets none.
    /// Every verse of the book on screen, aligned and gathered for the Word
    /// export.
    ///
    /// The whole book rather than the chapter being read: an edition is a book,
    /// and a document titled with one that held a single chapter would be a
    /// document that lied about itself. Aligning the chapters not on screen is
    /// the expensive part, which is why this is only reached from the export.
    CollationExport collationExport() const;

    CombinedApparatus editorApparatus() const;
    /// The aligned translation as one gloss per Combined column, groups of
    /// several words joined by a dash. Empty when nothing is aligned, which is
    /// what decides whether an interlinear file is written at all.
    InterlinearGlosses interlinearGlosses() const;
    /// Writes one OSIS document, atomically. False when anything went wrong.
    static bool writeOsis(const QString &path, const QString &osis);
    QString suggestAssociation(
        const SourceDocument &translation,
        const DocumentRefs &manuscripts) const;
    bool confirm(const QString &question);
    /// Loads what was chosen from the library, splitting manuscripts from
    /// translations so each goes in under the right role.
    void loadLibraryFiles(const QStringList &paths);
    /// Whether anything anywhere holds work a rebuild would discard.
    bool hasManualEdits() const;
    /// Whether any of these verses does — what Regenerate asks, being scoped
    /// to the chapter on screen.
    bool hasManualEdits(const QStringList &verseIds) const;
    /// Whether the editor has written a note on any word of these verses.
    bool hasNotes(const QStringList &verseIds) const;
    /// Whether they have typed interlinear wording in any of them.
    bool hasInterlinearEdits(const QStringList &verseIds) const;

    /// Reads a project out of `path` and makes it the edition on screen. False
    /// when it could not be read, having said why.
    ///
    /// Does not ask about unsaved work: both public callers do that before they
    /// get here, so the question is asked once, and asked before a file dialog
    /// rather than after the reader has already chosen a file.
    bool loadProjectFrom(const QString &path);

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
    QMap<QString, QString> m_chapterReferences;
    QMap<QString, QString> m_verseReferences;
    QMap<QString, QString> m_combinedNotes;
    QMap<QString, QString> m_interlinearWords;
    WordSelection m_selection;
    std::optional<Location> m_location;

    QList<LocationCoverage> m_locations;
    QList<AlignedVerse> m_alignedVerses;

    /// What one undo step restores. The divided columns travel with the
    /// drafts: putting back the readings without also putting back the column
    /// count would leave a verse with more columns than readings to fill them,
    /// and every word after the division sitting one place out. The references
    /// and notes travel for the same reason — a verse read against a different
    /// manuscript aligns differently, and a note names a column by its place.
    struct EditStep
    {
        QMap<QString, CombinedDraft> combined;
        QMap<QString, QList<int>> columnSplits;
        QMap<QString, QString> chapterReferences;
        QMap<QString, QString> verseReferences;
        QMap<QString, QString> combinedNotes;
        QMap<QString, QString> interlinearWords;
    };

    /// Everything one undo step covers, as it stands now.
    EditStep currentStep() const;
    void restoreStep(const EditStep &step);

    QList<EditStep> m_undoStack;
    QList<EditStep> m_redoStack;

    ReviewFilters m_filters;
    bool m_dirty = false;
    QString m_filePath;
    QString m_message;
    QString m_dataWarning;
    QString m_lastError;
};

} // namespace milah
