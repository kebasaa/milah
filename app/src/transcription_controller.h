#pragma once

#include "core/page_layout.h"
#include "core/serialize.h"
#include "core/transcription.h"
#include "ui/kraken_environment.h"

#include <QHash>
#include <QObject>
#include <QSize>
#include "core/osis_fill.h"

#include <QPoint>
#include <QString>
#include <QStringList>

#include <memory>

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

    /// How many words on this folio a machine read and nobody has looked at
    /// yet. Zero for a folio nothing was recognised on, which is every folio
    /// somebody typed.
    int uncheckedWordCount() const;
    /// Whether anything on this folio was placed by a recogniser, which is what
    /// decides whether the eye button is worth offering.
    ///
    /// Asked of the document rather than of the image pane, so the answer does
    /// not depend on which of the two heard about the change first.
    bool hasRecognisedWords() const;
    /// True when anything on the folio carries a line number — what the line
    /// view needs, and what the toolbar asks before offering the switch to it.
    bool hasRecognisedLines() const;

    /// Whether there is a kraken installation to remove, for the menu entry
    /// that offers to. Probes the machine the first time and remembers.
    bool krakenInstalled() const;
    /// What was found there, for the menu entry that offers to install it —
    /// which names WSL2 or does not, depending on this.
    KrakenEnvironment::State krakenState() const;

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
    /// Takes an ALTO or PAGE layout file somebody else produced — an
    /// institutional eScriptorium export, or a folio processed on a machine
    /// that is not this one — and reads it onto the folio on screen.
    ///
    /// The same landing place kraken's own output uses, and worth having on its
    /// own account rather than only as a way of testing that one: a transcriber
    /// whose library has already run the recognition should not have to run it
    /// again to get the benefit of it.
    void importRecognisedLayout();
    /// Reads the folio on screen with kraken. Sets kraken up first, asking, if
    /// this is the first time.
    /// `over` is the window the progress and any failures belong to. The fill
    /// dialog passes itself: QDialog::exec() is application-modal, so a progress
    /// dialog parented to the main window would be shut out of the input it
    /// needs and the run would look like a hang.
    /// Reads the folio with the installed model.
    ///
    /// `readingsOnly` keeps what is on the folio and fills in only what the
    /// machine read at each box — see recoverReadings().
    void transcribeFolio(QWidget *over = nullptr, bool readingsOnly = false);
    /// Reads the folio again to recover the machine's own readings, changing
    /// nothing else.
    ///
    /// For a folio read before Milah kept them, where a box held out of the work
    /// has nothing to go back to. Transcribe would recover them too and replace
    /// the text doing it, which on a folio already filled from a published
    /// transcription trades the larger thing for the smaller.
    void recoverReadings();
    /// Opens the setup dialog deliberately rather than by surprise, so it can
    /// be done once at a desk with time for it. The runtime only — models are
    /// manageModels().
    void setUpKraken();
    /// Opens the models dialog: what is installed, and which one runs.
    ///
    /// Separate from setUpKraken() because installing Kraken and installing a
    /// model happen on quite different schedules, and joining them left no way
    /// to add a second model once the first was in place.
    void manageModels();
    /// Shows what the last recognition ran, said and produced.
    void showLastRecognition();
    /// Puts this folio's corrected lines into its manuscript's training set.
    ///
    /// The way out of a hand no shipped model has seen — which is most hands.
    /// One folio at a time, because that is how a transcriber works; the set
    /// accumulates between sessions until there is enough to train on. See
    /// ui/training_set.h.
    void saveFolioForTraining();
    /// How many lines of the folio on screen are finished enough to be saved.
    /// Zero disables the command, and says why in its tooltip.
    int trainableLineCount() const;
    /// The folio at the largest size the library will give, fetched in as many
    /// pieces as its service caps require and put back together.
    ///
    /// For training only. A recogniser reads the small picture just as well —
    /// that was measured — but training shows every line to the model again and
    /// again, and a line stretched from 56 px to the 120 it wants is invented
    /// detail. Answers the bytes Milah already holds when the address is not a
    /// IIIF service, or when anything about the fetch fails.
    QByteArray fetchMasterImage(const TranscribedPage &page, QString *note);
    /// The training window: what has been gathered, and running the training.
    void showTraining();
    /// Puts a transcription that already exists onto this folio, keeping the
    /// recogniser's boxes and leaving every word unchecked.
    ///
    /// `folioPixel` is where the transcriber pointed, in the folio image's own
    /// pixels — a place rather than a line, because the folio commonly has no
    /// lines yet and the dialog reads it. Null for the top of the page.
    ///
    /// `carryOn` says this is a continuation of the transcription the last folio
    /// was filled from, rather than the start of a new one. Asked for by name in
    /// the folio's own menu and never inferred: the two are indistinguishable
    /// from the document, and guessing wrong lays down the wrong text.
    void fillFromOsis(QPoint folioPixel = QPoint(), bool carryOn = false);
    /// Where the folio before this one stopped filling, for the menu to name.
    ResumePoint resumePoint() const { return resumeFill(m_document, m_currentPage); }

private:
    /// Carries the transcription on from `resume` onto this folio, with no
    /// window of its own.
    ///
    /// Every input is settled before it runs — the file from the document, the
    /// place from the leaf before, the line from where the transcriber clicked —
    /// so there is nothing to ask and asking would only be ceremony. The one
    /// thing that appears is the recognition's progress, on a folio nothing has
    /// read yet, and only then. Ctrl+Z puts the folio back.
    void continueFill(QPoint folioPixel, const ResumePoint &resume);
    /// Lays a gathered passage onto the folio and records what it did.
    ///
    /// One path for both ways in — the window and the windowless continuation —
    /// because everything hard is here: which lines are left alone, how the
    /// folio is cut back into verses, and which chapter each of them is in. Two
    /// copies of that would differ within a week and the difference would show
    /// up as a wrong verse number rather than as a crash.
    ///
    /// `standalone` is false for a re-flow, which is part of the step that asked
    /// for it: it pushes no undo of its own, does not move the folio's recorded
    /// start line, and says something else afterwards.
    void applyFill(
        const QList<FilledLine> &filled,
        const QStringList &verses,
        const QString &sourcePath,
        const QString &range,
        bool standalone = true);
    /// Takes only the readings and the line geometry off a fresh recognition,
    /// leaving the folio's text alone.
    void harvestReadings(const RecognisedPage &recognised);
    /// Which word carries this box, if any. One lookup for the several things
    /// the folio's right-click can do to a word it points at.
    bool wordAt(const QRect &box, int *verse, int *column) const;
    /// Whether anything from `line` down has been checked, so the transcriber
    /// can be asked before a re-flow replaces it.
    bool hasCheckedWordsFrom(int line) const;
    /// Lays the passage again from `line` to the foot of the folio, so a word
    /// taken off a box goes back into the flow. Everything above is untouched.
    void reflowFrom(int line);

public:
    /// Whether there is anything to fill: a folio a machine has read.
    bool canFillFromOsis() const;
    /// Deletes the venv and the models, after saying what will go and roughly
    /// how much that is. Not WSL2 and not the distribution.
    void removeKraken();
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
    /// Says the manuscript's line ends after this word — for training, and for
    /// nothing else. See TranscribedWord::endsLine.
    void setLineBreak(int verse, int column, bool endsLine);
    /// The same, for the word the folio's right-click landed on. True when a
    /// word of that box was found.
    ///
    /// The folio is where this belongs: the break is a claim about the ink, and
    /// the text grid's bands are Milah's own wrapping with nothing to judge it
    /// against.
    bool setLineBreakAt(const QRect &box, bool endsLine);
    /// Holds a whole recognised line out of the work, or puts it back — the
    /// same decision setMarginal() makes about one word, made once for all of
    /// them, with one undo step, one warning and one re-flow. True when
    /// something changed.
    bool setLineMarginal(int line, bool marginal);
    /// Says this line and the next are one line of the manuscript, because the
    /// segmenter cut one line in two. See LineFill's joinLine(), which does the
    /// arithmetic; this adds the undo step, the warning where checked readings
    /// would be lost, and the re-flow that lays the joined line out as one.
    bool joinLineAt(int line);
    /// The transcriber has said the poured text's line ends **before** this
    /// word: the segmenter drew more boxes on the line than the manuscript has
    /// words there, so everything from here down belongs further along.
    ///
    /// Records the line's length on the page — see TranscribedPage::lineWords,
    /// which explains why this is not setLineBreakAt() — and lays the passage
    /// out again from that line to the foot of the leaf. Whatever no longer fits
    /// moves off the folio, and the recorded end point moves back with it, so
    /// the next leaf's continuation begins exactly there.
    bool breakLineBefore(const QRect &box);
    /// The other direction: this word belongs on the line above, because the
    /// segmenter drew fewer boxes on that line than the manuscript has words on
    /// it. One word per call. Only the first word of a line can be pulled up,
    /// and the line above is the previous one the folio *has* — a join leaves
    /// the numbering with a gap in it.
    bool pullWordUp(const QRect &box);
    /// Holds this word out of the work — a marginal note, a catchword, a running
    /// header — or puts it back. See TranscribedWord::marginal.
    void setMarginal(int verse, int column, bool marginal);
    /// The same, for the word the folio's right-click landed on. True when a
    /// word of that box was found.
    bool setMarginalAt(const QRect &box, bool marginal);
    /// What the word at this box currently reads, for the editor to open with.
    QString wordTextAt(const QRect &box) const;
    /// Corrects the word at this box, through the same path the grid edits by.
    bool setWordAt(const QRect &box, const QString &hebrew);
    void setVerseNumber(int verse, const QString &number);
    /// Takes one word off the folio, and nothing else.
    ///
    /// The verse keeps its place even when that was its last word: the heading
    /// carries the number and the chapter break, and a recogniser that read one
    /// word too many has not made the verse wrong. What is left is an empty
    /// cell to type in, which is what every verse ends with anyway.
    void removeWord(int verse, int column);
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
    /// A machine has just read this folio, and there is something new on the
    /// picture to look at.
    ///
    /// Deliberately not versesChanged, which also fires on every keystroke a
    /// transcriber commits. This is the one moment where opening the overlay
    /// unasked is what somebody wants — checking a reading against the ink it
    /// came from is the whole of what happens next.
    void recognitionApplied();
    /// One word a machine read has now been looked at by a person.
    ///
    /// Deliberately not versesChanged. This fires as the caret leaves a word,
    /// which is to say while somebody is moving through the folio — and a grid
    /// rebuilt at that moment destroys the very field the Tab key is on its way
    /// to. So the two places that show the flag change the one word in place:
    /// the cell drops its muting, and the folio overlay redraws.
    void wordChecked(int verse, int column);
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

    /// Puts a recognised page onto the folio on screen, as one verse with no
    /// number, every word marked unchecked. One undo step, and — when the folio
    /// already has something on it — one question first.
    ///
    /// `source` names where the reading came from, for the message and for the
    /// question. False when nothing was applied.
    bool applyRecognition(const RecognisedPage &recognised, const QString &source);
    /// The folio's own pixel size, read from the image header rather than by
    /// decoding it. What a layout file's declared page size is converted into,
    /// so that TranscribedWord::box means one thing everywhere.
    QSize folioPixelSize() const;

    /// Where kraken lives, made the first time anything asks. Probing costs a
    /// `wsl.exe` launch on Windows, and a session that never presses Transcribe
    /// should not pay for it.
    KrakenEnvironment &kraken() const;

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

    /// Mutable because krakenInstalled() is a question about the machine, and a
    /// caller asking it has not changed the transcription.
    mutable std::unique_ptr<KrakenEnvironment> m_kraken;
};

} // namespace milah
