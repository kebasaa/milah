#pragma once

#include "core/transcription.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

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
    /// The chapter the caret is in, for the toolbar field. 0 when nothing is.
    int selectedChapter() const;

public slots:
    /// Opens a folio and reads its folder, so the arrows have somewhere to go.
    void openImage();
    void openTranscription();
    /// Writes the transcription, asking where only the first time. True when a
    /// file was actually written — a cancelled dialog is a false, which is what
    /// lets leaving a folio be refused rather than silently losing the text.
    bool saveTranscription();
    void exportOsis();
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
    void dirtyChanged(bool dirty);
    void messageChanged(const QString &text);
    void historyChanged();

private:
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
    /// Loads an image into the document as a new page, or moves to the page it
    /// already has for that file.
    void showImage(const QString &imagePath);
    /// Writes the transcription to `m_filePath`, or asks where first. False
    /// when nothing was written — cancelled, or the write failed.
    bool commitBeforeLeavingPage();
    bool writeTo(const QString &path);

    void setDirty(bool dirty);
    void setMessage(const QString &text);
    /// The entry path a folio's image takes inside the archive. Numbered by
    /// page so two folios called `1.jpg` from different folders cannot collide.
    static QString imageEntryFor(int pageIndex, const QString &imageName);

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

    /// Where this transcription lives. Empty until it has been saved once,
    /// which is exactly the condition that makes leaving a folio ask.
    QString m_filePath;
    bool m_dirty = false;

    int m_selectedVerse = -1;
    int m_selectedColumn = -1;

    QList<EditStep> m_undoStack;
    QList<EditStep> m_redoStack;
};

} // namespace milah
