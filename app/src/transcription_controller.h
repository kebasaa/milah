#pragma once

#include "core/serialize.h"
#include "core/transcription.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QWidget;

namespace milah {

class UserDictionary;

/// Holds a transcription session: the folio on screen, the folder it came out
/// of, the text read off it, and the file dialogs that read and write them.
///
/// A sibling of AppController rather than part of it. The two share a window
/// and the user's dictionary, and nothing else — an edition's unsaved-work
/// question offers to write a `.milah` file, its close enumerates ten fields of
/// project state, and every one of its saves is a Save As. A transcription
/// wants none of that: it remembers where it lives, because leaving a folio has
/// to write the file without asking again.
class TranscriptionController final : public QObject
{
    Q_OBJECT

public:
    /// `dictionary` is the editor's own, owned by AppController and shared so a
    /// word defined while transcribing is a word defined while editing.
    TranscriptionController(
        QWidget *dialogParent,
        const UserDictionary *dictionary,
        QObject *parent = nullptr);

    const TranscriptionDocument &document() const { return m_document; }
    const TranscriptionMetadata &metadata() const { return m_document.metadata; }
    const UserDictionary &dictionary() const { return *m_dictionary; }

    /// The folio being transcribed, or nullptr when none is open.
    const TranscribedPage *currentPage() const;
    int currentPageIndex() const { return m_currentPage; }
    /// The image itself, decoded. Empty when no folio is open.
    QByteArray currentImageBytes() const;
    /// Why the folio on screen has no picture, empty when it has one. What the
    /// image pane shows in place of the picture, so that a library being down
    /// reads as a library being down rather than as an empty window.
    QString imageFailure() const { return m_imageFailure; }

    /// Where the previous and next arrows would go, empty at the ends of the
    /// folder. Named so the arrows can say in a tooltip which folio they mean.
    QString previousImageName() const;
    QString nextImageName() const;

    bool hasDocument() const { return !m_document.pages.isEmpty(); }
    bool isDirty() const { return m_dirty; }
    QString filePath() const { return m_filePath; }

    /// Asks what to do about unsaved transcription before it is thrown away.
    /// True means the caller may go ahead. Answers true at once when there is
    /// nothing to lose, so a caller may ask unconditionally.
    ///
    /// Public because the window has to ask it too, alongside the edition's.
    bool confirmDiscard();

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    /// The word the caret is in, for the Edit menu. -1 when nothing is focused.
    int selectedVerse() const { return m_selectedVerse; }
    int selectedColumn() const { return m_selectedColumn; }
    QString selectedWord() const;
    /// The transcriber's own remark on the word the caret is in, for the Notes
    /// panel. Empty when nothing is selected or nothing has been written.
    QString selectedNote() const;
    /// Asks the Notes panel to take the caret, so "Add/edit note" lands there.
    void requestNoteEditing() { emit noteEditingRequested(); }
    /// The chapter the caret is in, for the toolbar field. 0 when nothing is.
    int selectedChapter() const;

public slots:
    /// Opens a folio and reads its folder, so the arrows have somewhere to go.
    void openImage();
    /// Offers the published scans, and opens the chosen manuscript at its first
    /// folio. Replaces whatever was open, after the unsaved-work question: a
    /// transcription is of one manuscript.
    void openOnlineScan();
    void openTranscription();
    /// Opens a transcription the reader chose from the Open Recent menu rather
    /// than from a dialog. Asks about unsaved work exactly as openTranscription
    /// does, and takes the file back out of the recent list if it will not open.
    void openRecentTranscription(const QString &path);
    /// Writes the transcription, asking where only the first time. True when a
    /// file was actually written — a cancelled dialog is a false, which is what
    /// lets leaving a folio be refused rather than silently losing the text.
    bool saveTranscription();
    /// Writes the transcription as OSIS where the transcriber chooses: the text
    /// alone, the text with their notes, and the text with the glosses.
    void exportOsis();
    /// Writes the transcription as two Word documents: the manuscript as a text
    /// to read, and the same verse by verse with the English underneath.
    ///
    /// More forgiving than the OSIS export, deliberately. OSIS addresses a verse
    /// by book, chapter and number and can carry no verse that lacks one; a page
    /// of Hebrew nobody has finished identifying is still worth reading, so this
    /// prints it and says how many came out unnamed.
    void exportWord();
    /// Files the transcription in the manuscript library, one book to a file,
    /// where the Textual criticism tab looks for witnesses to collate.
    void addToLibrary();
    void closeTranscription();
    void goToPreviousImage();
    void goToNextImage();

    /// The book of this folio: the id the verses will be addressed by, and the
    /// name the transcriber wrote. Both at once, because they are one decision
    /// and have to be one undo step — putting a name back without the id it
    /// resolved to would leave the folio saying two different things.
    ///
    /// `label` empty means the name is simply the canonical one for `id`.
    void setBook(const QString &id, const QString &label = QString());
    void setFirstChapter(int chapter);
    void setMetadata(const TranscriptionMetadata &metadata);

    void selectWord(int verse, int column);
    void clearSelection();

    void setWord(int verse, int column, const QString &hebrew);
    /// Records a gloss the transcriber typed themselves, which re-reading the
    /// Hebrew above it must then leave alone.
    void setEnglish(int verse, int column, const QString &english);
    /// Divides the word at `caret`, the tail becoming the next column. A caret
    /// at either end opens an empty column instead, which is what a space at
    /// the end of a word means.
    void splitAt(int verse, int column, int caret);
    /// Joins a column onto the one before it, the caret landing at the seam.
    /// Does nothing for the first column of a verse.
    void mergeWithPrevious(int verse, int column);
    void removeColumn(int verse, int column);
    /// Opens a verse after `afterVerse`, numbered as typed. -1 opens the first.
    void insertVerse(int afterVerse, const QString &number);
    /// Starts a verse at `column`, numbered `number`, carrying the words after
    /// it along.
    ///
    /// One operation rather than a removal and an insertion, because it is one
    /// thing the transcriber did and has to be one thing to undo — and because
    /// the half-finished state between the two was where a stale cell used to
    /// write the number back as a word.
    ///
    /// A number typed between spaces is a verse boundary wherever it falls, so
    /// whatever followed it on the line belongs to the new verse.
    ///
    /// `firstWord` is what stood after the number in the cell it was typed in —
    /// the case of a number put in front of a word already there. Empty when
    /// the number was the whole of the cell.
    void startVerse(
        int verse, int column, const QString &number, const QString &firstWord = QString());
    /// Replaces the word at `column` with everything `text` divides into,
    /// keeping the words after it on the line.
    void pasteAt(int verse, int column, const QString &text);
    /// The transcriber's own remark on a word. Empty removes it.
    void setNote(int verse, int column, const QString &note);
    void setVerseNumber(int verse, const QString &number);
    void removeVerse(int verse);
    /// This verse and every verse after it move into the next chapter.
    void moveVerseToNewChapter(int verse);
    /// Puts a chapter break back, so the move can be undone from the menu as
    /// well as by Ctrl+Z.
    void clearChapterBreak(int verse);

    void undo();
    void redo();

signals:
    /// The document as a whole: a project opened or closed, or metadata edited.
    void documentChanged();
    /// A different folio is on screen.
    void pageChanged();
    /// The text of the current folio changed, so the grid must be rebuilt.
    void versesChanged();
    /// Which word the caret is in.
    void selectionChanged();
    /// Asks the Notes panel to take the caret, for "Add/edit note".
    void noteEditingRequested();
    void dirtyChanged(bool dirty);
    void messageChanged(const QString &text);
    void historyChanged();

private:
    /// The transcription in the shape the OSIS writers take it.
    ///
    /// Built once and used by both commands, so what is exported and what is
    /// filed in the library cannot come to differ in anything but their shape.
    struct Exportable
    {
        QMap<QString, CombinedDraft> drafts;
        InterlinearGlosses glosses;
        CombinedApparatus apparatus;
        /// Verses with no book, chapter or number, which OSIS cannot address.
        int unnamed = 0;
        /// Notes on those verses, which go nowhere with them.
        int strandedNotes = 0;
    };
    Exportable exportable() const;
    /// The work header both commands write, from the Manuscript panel.
    WorkMetadata workMetadata() const;
    /// What to say about verses OSIS could not address. Empty when it could
    /// address them all.
    static QString unaddressedNotice(const Exportable &work);
    /// Writes one OSIS file, atomically, reporting a failure itself.
    bool writeOsisTo(const QString &path, const QString &osis);

    TranscribedPage *mutablePage();
    /// True when `verse` and `column` name a word that exists.
    bool isValid(int verse, int column) const;
    /// Keeps one empty word at the end of the folio for the transcriber to type
    /// into. Without it a page that has just been opened — or whose last word
    /// has just been deleted — would have no cell at all, and there would be
    /// nowhere to start.
    void ensureTypingRoom();
    /// The lexicon's suggestion for a word, empty when it has none. Short by
    /// design: the gloss shares a column with the word above it, and Strong's
    /// own definitions run to paragraphs.
    static QString suggestedGloss(const QString &hebrew);

    /// Everything one undo step restores. The whole folio rather than the one
    /// word, because a split moves every column after it and a chapter break
    /// moves every verse below it — restoring less would leave the page saying
    /// something neither state ever said.
    struct EditStep
    {
        TranscribedPage page;
        int pageIndex = -1;
    };
    /// Records the folio as it stands, to be restored by one Ctrl+Z. Called
    /// before a change, never after.
    void pushUndo();
    void restoreStep(const EditStep &step);

    /// Reads the folder the current folio sits in, so the arrows know what is
    /// on either side. Only formats the running Qt can actually decode are
    /// listed, so an arrow never leads to a blank page.
    void readImageFolder(const QString &imagePath);
    /// Commits the current folio and moves to `index` of the folder listing.
    void goToImage(int index);
    /// True while the folios come from a scan rather than from a folder: every
    /// one of them is already a page of the document, so the arrows walk the
    /// document instead of a directory listing.
    bool navigatesByDocument() const;
    /// Commits the current folio and moves to page `index` of the document.
    void goToPage(int index);
    /// Fetches the current folio's image if it is a scan's and is not held.
    /// Synchronous: a folio is one picture and the transcriber is waiting for
    /// it, and everything else in this class assumes the page it is on is the
    /// page on screen.
    ///
    /// False when the folio could not be had — and then the caller must leave
    /// the message alone. Every caller used to announce its own success
    /// immediately afterwards, in the same block, with no repaint in between,
    /// so "Could not fetch 12r: …" was written and overwritten before anyone
    /// could read it. Nothing to fetch is not a failure and answers true.
    bool ensureImageFetched();
    /// Lets go of a folio's image on the way off it, unless it was worked on.
    ///
    /// This is what keeps a transcription of six folios out of a codex from
    /// weighing what the codex weighs. A scan's images can always be had again
    /// from the library, so the ones worth carrying are the ones somebody read
    /// something off — and those are exactly the ones the file has to be able
    /// to show when it is opened again with no internet.
    /// Returns the bytes let go of, and an empty result when it kept them, so a
    /// navigation that is then refused can put the picture back rather than
    /// leaving the transcriber looking at a blank folio they have not left.
    QByteArray releaseImageIfUnread(int pageIndex);
    /// Loads an image into the document as a new page, or moves to the page it
    /// already has for that file.
    void showImage(const QString &imagePath);
    /// Reads a transcription out of `path` and puts it on screen. False when it
    /// could not be read, having said why.
    ///
    /// Does not ask about unsaved work: both public callers do that before they
    /// get here, so the question is asked once, and asked before a file dialog
    /// rather than after the reader has already chosen a file.
    bool loadTranscriptionFrom(const QString &path);
    /// Writes the transcription to `m_filePath`, or asks where first. False when
    /// nothing was written — cancelled, or the write failed — and then the
    /// navigation that called it does not happen.
    ///
    /// True at once while nothing has been read off any folio: a transcriber
    /// paging through a codex to find their chapter is not making anything to
    /// lose, and asking them to name a file for it refused the page turn when
    /// they cancelled.
    bool commitBeforeLeavingPage();
    bool writeTo(const QString &path);

    void setDirty(bool dirty);
    void setMessage(const QString &text);

    QWidget *m_dialogParent = nullptr;
    const UserDictionary *m_dictionary = nullptr;

    TranscriptionDocument m_document;
    /// The folio images, keyed by archive entry path. Held rather than re-read
    /// from disk because a transcription must save what was being read even
    /// after the folder it came from has moved.
    QHash<QString, QByteArray> m_images;
    int m_currentPage = -1;

    /// Every readable image in the current folio's folder, in the order a
    /// reader would put them, and where the current folio sits in that list.
    QStringList m_folderImages;
    int m_folderIndex = -1;

    /// Where this transcription lives. Empty until it has been saved once, and
    /// settled by the first folio somebody actually writes on: walking folios
    /// with nothing typed asks for nothing and writes nothing.
    QString m_filePath;
    bool m_dirty = false;

    /// Why the current folio has no image. Set by ensureImageFetched and read
    /// by the window, because a status message does not survive the next thing
    /// that happens and this has to still be there when the transcriber looks
    /// up from the page.
    QString m_imageFailure;

    /// Made only when a folio has to be fetched, so a session that never opens
    /// a scan never builds one.
    QNetworkAccessManager *m_network = nullptr;
    /// Learned once per session: this network's ordinary route does not carry
    /// traffic and the IPv4 fallback should be used from the start.
    bool m_preferIPv4 = false;

    int m_selectedVerse = -1;
    int m_selectedColumn = -1;

    QList<EditStep> m_undoStack;
    QList<EditStep> m_redoStack;
};

} // namespace milah
