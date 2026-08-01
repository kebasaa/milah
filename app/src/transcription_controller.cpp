#include "transcription_controller.h"

#include "core/lexicon.h"
#include "core/project.h"
#include "core/serialize.h"
#include "core/suggestions.h"
#include "project_storage.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>

namespace milah {
namespace {

/// How many folios back an undo can reach. Generous, because a transcriber
/// works down a page in very small steps and the thing they want back is
/// usually several of them ago; bounded, because a step carries a whole folio.
constexpr int MaxUndoDepth = 200;

QString transcriptionFilter()
{
    return QStringLiteral("Milah transcriptions (*.trscrpt);;All files (*)");
}

/// Only what the running Qt can actually decode.
///
/// Built from the installed image plugins rather than written out, because the
/// set is not fixed: TIFF, which manuscript scans often are, needs the Qt Image
/// Formats add-on, and a filter naming it on a build without that add-on would
/// offer the transcriber a file that opens as a blank page.
QString imageFilter()
{
    QStringList patterns;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    patterns.reserve(formats.size());
    for (const QByteArray &format : formats) {
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format).toLower()));
    }
    patterns.removeDuplicates();
    patterns.sort();
    return QStringLiteral("Manuscript images (%1);;All files (*)")
        .arg(patterns.join(QLatin1Char(' ')));
}

QStringList imageNameFilters()
{
    QStringList patterns;
    for (const QByteArray &format : QImageReader::supportedImageFormats()) {
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format).toLower()));
    }
    patterns.removeDuplicates();
    return patterns;
}

} // namespace

TranscriptionController::TranscriptionController(
    QWidget *dialogParent,
    const UserDictionary *dictionary,
    QObject *parent)
    : QObject(parent)
    , m_dialogParent(dialogParent)
    , m_dictionary(dictionary)
{
}

const TranscribedPage *TranscriptionController::currentPage() const
{
    if (m_currentPage < 0 || m_currentPage >= m_document.pages.size()) {
        return nullptr;
    }
    return &m_document.pages.at(m_currentPage);
}

TranscribedPage *TranscriptionController::mutablePage()
{
    if (m_currentPage < 0 || m_currentPage >= m_document.pages.size()) {
        return nullptr;
    }
    return &m_document.pages[m_currentPage];
}

QByteArray TranscriptionController::currentImageBytes() const
{
    const TranscribedPage *page = currentPage();
    return page ? m_images.value(page->imageEntry) : QByteArray();
}

QString TranscriptionController::previousImageName() const
{
    if (m_folderIndex <= 0) {
        return QString();
    }
    return QFileInfo(m_folderImages.at(m_folderIndex - 1)).fileName();
}

QString TranscriptionController::nextImageName() const
{
    if (m_folderIndex < 0 || m_folderIndex + 1 >= m_folderImages.size()) {
        return QString();
    }
    return QFileInfo(m_folderImages.at(m_folderIndex + 1)).fileName();
}

bool TranscriptionController::isValid(int verse, int column) const
{
    const TranscribedPage *page = currentPage();
    if (!page || verse < 0 || verse >= page->verses.size()) {
        return false;
    }
    return column >= 0 && column < page->verses.at(verse).words.size();
}

QString TranscriptionController::selectedWord() const
{
    if (!isValid(m_selectedVerse, m_selectedColumn)) {
        return QString();
    }
    return currentPage()->verses.at(m_selectedVerse).words.at(m_selectedColumn).hebrew;
}

int TranscriptionController::selectedChapter() const
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return 0;
    }
    if (m_selectedVerse < 0 || m_selectedVerse >= page->verses.size()) {
        return page->firstChapter;
    }
    return chapterOfVerse(*page, m_selectedVerse);
}

// --------------------------------------------------------------------------
// Session state
// --------------------------------------------------------------------------

void TranscriptionController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

void TranscriptionController::setMessage(const QString &text)
{
    emit messageChanged(text);
}

void TranscriptionController::ensureTypingRoom()
{
    TranscribedPage *page = mutablePage();
    if (!page) {
        return;
    }
    if (page->verses.isEmpty()) {
        page->verses.append(TranscribedVerse());
    }
    // Every verse, not only the last: deleting the one word of a verse partway
    // down the folio would otherwise leave a numbered line with no cell in it,
    // and no way back into it short of deleting the verse.
    for (TranscribedVerse &verse : page->verses) {
        if (verse.words.isEmpty()) {
            verse.words.append(TranscribedWord());
        }
    }
}

QString TranscriptionController::suggestedGloss(const QString &hebrew)
{
    if (hebrew.isEmpty()) {
        return QString();
    }
    const QList<LexiconEntry> entries = HebrewLexicon::shared().lookup(hebrew);
    if (entries.isEmpty()) {
        return QString();
    }
    const LexiconEntry &entry = entries.first();
    if (!entry.briefGloss.isEmpty()) {
        return entry.briefGloss;
    }
    // The abridged BDB carries one sense per line, so its first line is the
    // nearest thing to a brief gloss. Strong's own definition is deliberately
    // not used as a fallback: it runs to paragraphs, and a paragraph in a cell
    // would set the width of the column the word above it has to fit in.
    const QString meaning = entry.meaning.section(QLatin1Char('\n'), 0, 0).trimmed();
    return meaning;
}

QString TranscriptionController::imageEntryFor(int pageIndex, const QString &imageName)
{
    // Numbered by page, because two folios called `1.jpg` out of different
    // folders would otherwise be the same entry and the second would silently
    // replace the first.
    return QStringLiteral("images/%1-%2")
        .arg(pageIndex + 1, 3, 10, QLatin1Char('0'))
        .arg(imageName);
}

void TranscriptionController::pushUndo()
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return;
    }
    m_undoStack.append(EditStep{*page, m_currentPage});
    if (m_undoStack.size() > MaxUndoDepth) {
        m_undoStack.removeFirst();
    }
    // Anything done after an undo makes what was undone unreachable; keeping it
    // would offer to redo a change onto a folio that has since moved on.
    m_redoStack.clear();
    emit historyChanged();
}

void TranscriptionController::restoreStep(const EditStep &step)
{
    if (step.pageIndex < 0 || step.pageIndex >= m_document.pages.size()) {
        return;
    }
    m_currentPage = step.pageIndex;
    m_document.pages[step.pageIndex] = step.page;
    // The caret may have been in a word the step removed, so it is dropped
    // rather than left pointing into a folio that no longer has that column.
    m_selectedVerse = -1;
    m_selectedColumn = -1;
    setDirty(true);
    emit versesChanged();
    emit selectionChanged();
    emit historyChanged();
}

void TranscriptionController::undo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }
    const TranscribedPage *page = currentPage();
    if (page) {
        m_redoStack.append(EditStep{*page, m_currentPage});
    }
    const EditStep step = m_undoStack.takeLast();
    restoreStep(step);
}

void TranscriptionController::redo()
{
    if (m_redoStack.isEmpty()) {
        return;
    }
    const TranscribedPage *page = currentPage();
    if (page) {
        m_undoStack.append(EditStep{*page, m_currentPage});
    }
    const EditStep step = m_redoStack.takeLast();
    restoreStep(step);
}

bool TranscriptionController::confirmDiscard()
{
    if (!m_dirty) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        m_dialogParent,
        QStringLiteral("Milah"),
        QStringLiteral("This transcription has changes you have not saved.\n"
                       "Save them before closing it?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    switch (answer) {
    case QMessageBox::Save:
        // Only a file actually written counts as saved: a cancelled Save As
        // must leave the transcription open rather than throwing it away.
        return saveTranscription();
    case QMessageBox::Discard:
        return true;
    default:
        return false;
    }
}

// --------------------------------------------------------------------------
// Images
// --------------------------------------------------------------------------

void TranscriptionController::readImageFolder(const QString &imagePath)
{
    m_folderImages.clear();
    m_folderIndex = -1;

    const QFileInfo info(imagePath);
    QDir folder = info.absoluteDir();
    const QFileInfoList entries =
        folder.entryInfoList(imageNameFilters(), QDir::Files | QDir::Readable);

    // Numeric order, not byte order: a folder of folios runs 9, 10, 11, and a
    // plain string sort would put 10 before 9 and send the arrows backwards.
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    for (const QFileInfo &entry : entries) {
        m_folderImages.append(entry.absoluteFilePath());
    }
    std::sort(
        m_folderImages.begin(),
        m_folderImages.end(),
        [&collator](const QString &left, const QString &right) {
            return collator.compare(left, right) < 0;
        });
    m_folderIndex = m_folderImages.indexOf(info.absoluteFilePath());
}

void TranscriptionController::showImage(const QString &imagePath)
{
    const QFileInfo info(imagePath);

    // A folio already in this transcription is returned to rather than added
    // again, so walking back up a folder does not duplicate every page.
    for (int index = 0; index < m_document.pages.size(); ++index) {
        if (m_document.pages.at(index).sourcePath == info.absoluteFilePath()) {
            m_currentPage = index;
            ensureTypingRoom();
            readImageFolder(imagePath);
            emit pageChanged();
            emit versesChanged();
            return;
        }
    }

    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setMessage(QStringLiteral("Could not read %1.").arg(info.fileName()));
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    TranscribedPage page;
    page.imageName = info.fileName();
    page.sourcePath = info.absoluteFilePath();
    page.imageEntry = imageEntryFor(m_document.pages.size(), page.imageName);
    // A new folio opens where the last one left off, because a transcriber
    // working through a codex is nearly always still in the same book and
    // chapter — and correcting it is one field rather than two.
    if (const TranscribedPage *previous = currentPage()) {
        page.book = previous->book;
        page.firstChapter = chapterOfVerse(*previous, previous->verses.size() - 1);
    }

    m_images.insert(page.imageEntry, bytes);
    m_document.pages.append(page);
    m_currentPage = m_document.pages.size() - 1;
    ensureTypingRoom();

    readImageFolder(imagePath);
    setDirty(true);
    setMessage(QStringLiteral("Transcribing %1.").arg(page.imageName));
    emit documentChanged();
    emit pageChanged();
    emit versesChanged();
}

void TranscriptionController::openImage()
{
    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Open a manuscript image"),
        QSettings().value(QStringLiteral("paths/lastDirectory")).toString(),
        imageFilter());
    if (path.isEmpty()) {
        return;
    }
    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"), QFileInfo(path).absolutePath());
    showImage(path);
}

bool TranscriptionController::commitBeforeLeavingPage()
{
    if (!hasDocument()) {
        return true;
    }
    // Leaving a folio commits what was typed on it. The first time, that means
    // asking where the transcription is to live: everything after depends on
    // there being a file, and a transcriber who has just read a page should not
    // be able to walk off it into nothing. After that the file is simply
    // written, because being asked once per folio would be worse than not being
    // asked at all.
    if (m_filePath.isEmpty()) {
        return saveTranscription();
    }
    if (!m_dirty) {
        return true;
    }
    return writeTo(m_filePath);
}

void TranscriptionController::goToImage(int index)
{
    if (index < 0 || index >= m_folderImages.size()) {
        return;
    }
    if (!commitBeforeLeavingPage()) {
        // Cancelled, or the write failed. The transcriber stays where they are
        // with their text intact rather than losing it to a navigation.
        return;
    }
    showImage(m_folderImages.at(index));
}

void TranscriptionController::goToPreviousImage()
{
    goToImage(m_folderIndex - 1);
}

void TranscriptionController::goToNextImage()
{
    goToImage(m_folderIndex + 1);
}

// --------------------------------------------------------------------------
// Files
// --------------------------------------------------------------------------

bool TranscriptionController::writeTo(const QString &path)
{
    const MilahProjectPayload payload = transcriptionPayload(m_document, m_images);
    QString error;
    if (!ProjectStorage::saveToPath(
            path, payloadToJson(payload), &error, ProjectStorage::ImageEntryLimit)) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), error);
        setMessage(error);
        return false;
    }

    m_filePath = path;
    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"), QFileInfo(path).absolutePath());
    setDirty(false);
    setMessage(QStringLiteral("Saved %1.").arg(QFileInfo(path).fileName()));
    emit documentChanged();
    return true;
}

bool TranscriptionController::saveTranscription()
{
    if (!hasDocument()) {
        setMessage(QStringLiteral("There is nothing to save yet."));
        return false;
    }

    if (!m_filePath.isEmpty()) {
        return writeTo(m_filePath);
    }

    const QString suggested = m_document.metadata.manuscriptName.isEmpty()
        ? QStringLiteral("Transcription")
        : m_document.metadata.manuscriptName;
    const QString directory =
        QSettings().value(QStringLiteral("paths/lastDirectory")).toString();
    QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Save the transcription"),
        directory.isEmpty() ? suggested
                            : QDir(directory).filePath(suggested),
        transcriptionFilter());
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".trscrpt"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".trscrpt");
    }
    return writeTo(path);
}

void TranscriptionController::openTranscription()
{
    if (!confirmDiscard()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Open a transcription"),
        QSettings().value(QStringLiteral("paths/lastDirectory")).toString(),
        transcriptionFilter());
    if (path.isEmpty()) {
        return;
    }

    QString error;
    const QJsonObject read =
        ProjectStorage::loadFromPath(path, &error, ProjectStorage::ImageEntryLimit);
    if (read.isEmpty()) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), error);
        return;
    }

    const MilahProjectPayload payload = payloadFromJson(read);
    TranscriptionDocument document;
    try {
        document = restoreTranscription(payload);
    } catch (const ProjectError &failure) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), failure.message());
        return;
    }

    m_document = document;
    m_images = transcriptionImages(payload);
    m_filePath = path;
    m_currentPage = m_document.pages.isEmpty() ? -1 : 0;
    ensureTypingRoom();
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedVerse = -1;
    m_selectedColumn = -1;

    // The folder the folios came from may be gone, or may be somewhere else
    // entirely on this machine. The images themselves travel inside the file,
    // so the transcription opens either way; only the arrows stand down.
    m_folderImages.clear();
    m_folderIndex = -1;
    if (const TranscribedPage *page = currentPage()) {
        if (!page->sourcePath.isEmpty() && QFileInfo::exists(page->sourcePath)) {
            readImageFolder(page->sourcePath);
        }
    }

    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"), QFileInfo(path).absolutePath());
    setDirty(false);
    setMessage(QStringLiteral("Opened %1.").arg(QFileInfo(path).fileName()));
    emit documentChanged();
    emit pageChanged();
    emit versesChanged();
    emit selectionChanged();
    emit historyChanged();
}

void TranscriptionController::closeTranscription()
{
    if (!confirmDiscard()) {
        return;
    }
    m_document = TranscriptionDocument();
    m_images.clear();
    m_filePath.clear();
    m_currentPage = -1;
    m_folderImages.clear();
    m_folderIndex = -1;
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedVerse = -1;
    m_selectedColumn = -1;
    setDirty(false);
    setMessage(QStringLiteral("Transcription closed."));
    emit documentChanged();
    emit pageChanged();
    emit versesChanged();
    emit selectionChanged();
    emit historyChanged();
}

void TranscriptionController::exportOsis()
{
    if (!hasDocument()) {
        setMessage(QStringLiteral("There is nothing to export yet."));
        return;
    }

    // The transcription is shaped into the same drafts the edition exports, so
    // the OSIS a folio produces and the OSIS an edition produces are written by
    // one function and cannot drift apart.
    QMap<QString, CombinedDraft> drafts;
    InterlinearGlosses glosses;
    int unnamed = 0;

    for (const TranscribedPage &page : m_document.pages) {
        for (int index = 0; index < page.verses.size(); ++index) {
            const TranscribedVerse &verse = page.verses.at(index);
            const QString id = transcribedVerseId(page, index);
            if (id.isEmpty()) {
                ++unnamed;
                continue;
            }

            CombinedDraft draft;
            draft.reference.id = id;
            draft.reference.book = page.book;
            draft.reference.chapter = chapterOfVerse(page, index);
            draft.reference.verse = verse.number;

            QMap<int, QString> verseGlosses;
            for (int column = 0; column < verse.words.size(); ++column) {
                const TranscribedWord &word = verse.words.at(column);
                ConsensusColumn cell;
                cell.text = word.hebrew;
                draft.columns.append(cell);
                if (!word.english.isEmpty()) {
                    verseGlosses.insert(column, word.english);
                }
            }
            if (!verseGlosses.isEmpty()) {
                glosses.insert(id, verseGlosses);
            }
            drafts.insert(id, draft);
        }
    }

    if (drafts.isEmpty()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Nothing can be exported yet: OSIS addresses a verse by "
                           "book, chapter and number, and none of the verses "
                           "transcribed so far has all three."));
        return;
    }

    WorkMetadata work;
    work.workId = QStringLiteral("Milah.Transcription");
    work.language = m_document.metadata.language.isEmpty()
        ? QStringLiteral("he")
        : m_document.metadata.language;
    work.title = m_document.metadata.manuscriptName.isEmpty()
        ? QStringLiteral("Milah Transcription")
        : m_document.metadata.manuscriptName;
    if (!m_document.metadata.libraryMark.isEmpty()) {
        work.identifiers.insert(
            QStringLiteral("x-shelfmark"), m_document.metadata.libraryMark);
    }
    if (!m_document.metadata.transcriber.isEmpty()) {
        work.identifiers.insert(
            QStringLiteral("x-transcriber"), m_document.metadata.transcriber);
    }

    const QString suggested = QStringLiteral("%1.osis").arg(work.title);
    const QString directory =
        QSettings().value(QStringLiteral("paths/lastDirectory")).toString();
    QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Export the transcription as OSIS"),
        directory.isEmpty() ? suggested : QDir(directory).filePath(suggested),
        QStringLiteral("OSIS files (*.osis *.xml);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.contains(QLatin1Char('.'))) {
        path += QStringLiteral(".osis");
    }

    const QString osis = serializeInterlinearOsis(drafts, glosses, work);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(osis.toUtf8()) < 0
        || !file.commit()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Could not write %1.").arg(path));
        return;
    }

    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"), QFileInfo(path).absolutePath());
    // Verses that could not be addressed are named rather than dropped quietly:
    // a transcriber who has not filled the Book field in would otherwise see a
    // successful export that is missing a folio.
    setMessage(unnamed > 0
        ? QStringLiteral("Exported %1 verses. %2 could not be addressed and were "
                         "left out — they need a book and a verse number.")
              .arg(drafts.size())
              .arg(unnamed)
        : QStringLiteral("Exported %1 verses to %2.")
              .arg(drafts.size())
              .arg(QFileInfo(path).fileName()));
}

// --------------------------------------------------------------------------
// Editing
// --------------------------------------------------------------------------

void TranscriptionController::setBook(const QString &id, const QString &label)
{
    TranscribedPage *page = mutablePage();
    if (!page || (page->book == id && page->bookLabel == label)) {
        return;
    }
    pushUndo();
    page->book = id;
    page->bookLabel = label;
    setDirty(true);
    emit pageChanged();
    // The verse labels carry the book nowhere, but the export does, and a folio
    // whose book has just been named is one whose verses can now be addressed.
    emit versesChanged();
}

void TranscriptionController::setFirstChapter(int chapter)
{
    TranscribedPage *page = mutablePage();
    if (!page || chapter < 1 || page->firstChapter == chapter) {
        return;
    }
    pushUndo();
    page->firstChapter = chapter;
    setDirty(true);
    emit pageChanged();
    // Every verse on the folio counts from this number, so the chapter shown
    // beside each of them moves with it.
    emit versesChanged();
}

void TranscriptionController::setMetadata(const TranscriptionMetadata &metadata)
{
    m_document.metadata = metadata;
    setDirty(true);
    emit documentChanged();
}

void TranscriptionController::selectWord(int verse, int column)
{
    if (m_selectedVerse == verse && m_selectedColumn == column) {
        return;
    }
    m_selectedVerse = verse;
    m_selectedColumn = column;
    emit selectionChanged();
}

void TranscriptionController::clearSelection()
{
    selectWord(-1, -1);
}

void TranscriptionController::setWord(int verse, int column, const QString &hebrew)
{
    if (!isValid(verse, column)) {
        return;
    }
    TranscribedPage *page = mutablePage();
    TranscribedWord &word = page->verses[verse].words[column];
    if (word.hebrew == hebrew) {
        return;
    }
    pushUndo();
    word.hebrew = hebrew;
    // The gloss follows the word unless the transcriber has written one
    // themselves, in which case re-reading the Hebrew must leave it alone —
    // correcting a letter should not silently discard a translation.
    if (!word.englishIsOwn) {
        word.english = suggestedGloss(hebrew);
    }
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::setEnglish(int verse, int column, const QString &english)
{
    if (!isValid(verse, column)) {
        return;
    }
    TranscribedPage *page = mutablePage();
    TranscribedWord &word = page->verses[verse].words[column];
    if (word.english == english) {
        return;
    }
    pushUndo();
    word.english = english;
    // From here the gloss is the transcriber's, and re-reading the Hebrew above
    // it must not overwrite what they wrote. Clearing it hands the word back to
    // the lexicon, which is how a suggestion is asked for again.
    word.englishIsOwn = !english.trimmed().isEmpty();
    if (!word.englishIsOwn) {
        word.english = suggestedGloss(word.hebrew);
    }
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::splitAt(int verse, int column, int caret)
{
    if (!isValid(verse, column)) {
        return;
    }
    pushUndo();
    TranscribedPage *page = mutablePage();
    QList<TranscribedWord> &words = page->verses[verse].words;

    const QString whole = words.at(column).hebrew;
    const int at = std::clamp(caret, 0, int(whole.size()));

    TranscribedWord tail;
    tail.hebrew = whole.mid(at);

    // The head keeps the gloss because it keeps the word's opening; the tail is
    // a word nobody has read yet and starts with nothing said about it.
    words[column].hebrew = whole.left(at);
    if (!words[column].englishIsOwn) {
        words[column].english = suggestedGloss(words.at(column).hebrew);
    }
    tail.english = suggestedGloss(tail.hebrew);
    words.insert(column + 1, tail);

    ensureTypingRoom();
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::mergeWithPrevious(int verse, int column)
{
    if (!isValid(verse, column) || column == 0) {
        return;
    }
    pushUndo();
    TranscribedPage *page = mutablePage();
    QList<TranscribedWord> &words = page->verses[verse].words;

    TranscribedWord &previous = words[column - 1];
    previous.hebrew += words.at(column).hebrew;
    // The joined word is not either of the words that made it, so whatever was
    // said about them is dropped and the lexicon is asked again.
    previous.englishIsOwn = false;
    previous.english = suggestedGloss(previous.hebrew);
    words.removeAt(column);

    ensureTypingRoom();
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::removeColumn(int verse, int column)
{
    if (!isValid(verse, column)) {
        return;
    }
    pushUndo();
    mutablePage()->verses[verse].words.removeAt(column);
    ensureTypingRoom();
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::insertVerse(int afterVerse, const QString &number)
{
    TranscribedPage *page = mutablePage();
    if (!page) {
        return;
    }
    pushUndo();
    TranscribedVerse verse;
    verse.number = number;
    const int at = std::clamp(afterVerse + 1, 0, int(page->verses.size()));
    page->verses.insert(at, verse);
    ensureTypingRoom();
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::setVerseNumber(int verse, const QString &number)
{
    TranscribedPage *page = mutablePage();
    if (!page || verse < 0 || verse >= page->verses.size()) {
        return;
    }
    if (page->verses.at(verse).number == number) {
        return;
    }
    pushUndo();
    page->verses[verse].number = number;
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::removeVerse(int verse)
{
    TranscribedPage *page = mutablePage();
    if (!page || verse < 0 || verse >= page->verses.size()) {
        return;
    }
    pushUndo();
    page->verses.removeAt(verse);
    ensureTypingRoom();
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::moveVerseToNewChapter(int verse)
{
    TranscribedPage *page = mutablePage();
    if (!page || verse < 0 || verse >= page->verses.size()) {
        return;
    }
    if (page->verses.at(verse).startsNewChapter) {
        return;
    }
    pushUndo();
    // One flag, and every verse below it follows: that is what "this verse
    // begins a new chapter" means, and it is why the break is stored as a flag
    // rather than as a number on each verse.
    page->verses[verse].startsNewChapter = true;
    setDirty(true);
    emit versesChanged();
}

void TranscriptionController::clearChapterBreak(int verse)
{
    TranscribedPage *page = mutablePage();
    if (!page || verse < 0 || verse >= page->verses.size()) {
        return;
    }
    if (!page->verses.at(verse).startsNewChapter) {
        return;
    }
    pushUndo();
    page->verses[verse].startsNewChapter = false;
    setDirty(true);
    emit versesChanged();
}

} // namespace milah
