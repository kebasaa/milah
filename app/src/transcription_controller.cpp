#include "transcription_controller.h"

#include "core/alignment.h"
#include "core/books.h"
#include "core/lexicon.h"
#include "core/manuscript_catalogue.h"
#include "core/project.h"
#include "core/recent_files.h"
#include "core/serialize.h"
#include "core/transcription_docx.h"
#include "core/suggestions.h"
#include "project_storage.h"
#include "ui/network_fetch.h"
#include "ui/online_scan_dialog.h"
#include "ui/scan_metadata_dialog.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QApplication>
#include <QEventLoop>
#include <QImageReader>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSettings>

namespace milah {
namespace {

/// How many folios back an undo can reach. Generous, because a transcriber
/// works down a page in very small steps and the thing they want back is
/// usually several of them ago; bounded, because a step carries a whole folio.
constexpr int MaxUndoDepth = 200;

/// The most a single folio may weigh. A library serves a page at a size meant
/// for reading — Cambridge's are a couple of hundred kilobytes at the width
/// Milah asks for, and it caps delivery besides — so this is a bound on what a
/// reply may be trusted to be, not a limit anything real comes near.
constexpr qint64 ScanImageLimit = 64 * 1024 * 1024;

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

bool TranscriptionController::navigatesByDocument() const
{
    // A scan's folios are all pages of the document already, and there is no
    // folder to read. A local image's siblings are the folder's business.
    return m_folderImages.isEmpty() && m_document.pages.size() > 1;
}

QString TranscriptionController::previousImageName() const
{
    if (navigatesByDocument()) {
        if (m_currentPage <= 0) {
            return QString();
        }
        const TranscribedPage &page = m_document.pages.at(m_currentPage - 1);
        return page.imageLabel.isEmpty() ? page.imageName : page.imageLabel;
    }
    if (m_folderIndex <= 0) {
        return QString();
    }
    return QFileInfo(m_folderImages.at(m_folderIndex - 1)).fileName();
}

QString TranscriptionController::nextImageName() const
{
    if (navigatesByDocument()) {
        if (m_currentPage < 0 || m_currentPage + 1 >= m_document.pages.size()) {
            return QString();
        }
        const TranscribedPage &page = m_document.pages.at(m_currentPage + 1);
        return page.imageLabel.isEmpty() ? page.imageName : page.imageLabel;
    }
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

QString TranscriptionController::selectedNote() const
{
    if (!isValid(m_selectedVerse, m_selectedColumn)) {
        return QString();
    }
    return currentPage()->verses.at(m_selectedVerse).words.at(m_selectedColumn).note;
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
        page.bookLabel = previous->bookLabel;
        page.firstChapter = chapterOfVerse(*previous, previous->verses.size() - 1);
    }

    m_images.insert(page.imageEntry, bytes);
    m_document.pages.append(page);
    m_currentPage = m_document.pages.size() - 1;
    ensureTypingRoom();

    readImageFolder(imagePath);
    // Deliberately not marked changed. Opening a folio is looking at a picture,
    // not writing anything down, and a transcriber who walks a folder of three
    // hundred leaves to find their chapter has made nothing they could lose.
    // Whatever a real edit established is left alone: nothing is reset here.
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
    // Not a word typed anywhere in the transcription yet, so there is nothing to
    // commit and nowhere it needs to live. Asked before the file path and not
    // after, which is the whole of the bug this fixes: a never-saved
    // transcription used to open a Save As on every arrow press, and cancelling
    // it refused the turn — so a transcriber paging through a codex to find
    // where their chapter starts could not get off folio one without first
    // naming a file with nothing in it.
    if (isUntouched(m_document)) {
        return true;
    }
    // Already on disk exactly as it stands.
    if (!m_dirty && !m_filePath.isEmpty()) {
        return true;
    }
    // Leaving a folio commits what was typed on it. The first folio with text on
    // it is what settles where the transcription lives; everything after depends
    // on there being a file. After that it is simply written, because being
    // asked once per folio would be worse than not being asked at all.
    if (m_filePath.isEmpty()) {
        return saveTranscription();
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

void TranscriptionController::goToPage(int index)
{
    if (index < 0 || index >= m_document.pages.size() || index == m_currentPage) {
        return;
    }
    const int leaving = m_currentPage;
    // Let go of the folio before the file is written, not after. A folio nobody
    // read anything off would otherwise be base64'd into the archive on the way
    // past and only then dropped from memory — which is the one cost that
    // fetching and releasing exists to avoid.
    const QByteArray released = releaseImageIfUnread(leaving);
    if (!commitBeforeLeavingPage()) {
        // Cancelled, or the write failed. The transcriber stays where they are
        // with their text intact — and with the picture they are still looking
        // at, which was let go of a moment ago on the way out.
        if (!released.isEmpty()) {
            m_images.insert(m_document.pages.at(leaving).imageEntry, released);
        }
        return;
    }

    m_currentPage = index;
    ensureTypingRoom();

    if (ensureImageFetched()) {
        const TranscribedPage &page = m_document.pages.at(m_currentPage);
        setMessage(QStringLiteral("Transcribing %1.")
                       .arg(page.imageLabel.isEmpty() ? page.imageName : page.imageLabel));
    }
    emit pageChanged();
    emit versesChanged();
}

void TranscriptionController::goToPreviousImage()
{
    if (navigatesByDocument()) {
        goToPage(m_currentPage - 1);
        return;
    }
    goToImage(m_folderIndex - 1);
}

void TranscriptionController::goToNextImage()
{
    if (navigatesByDocument()) {
        goToPage(m_currentPage + 1);
        return;
    }
    goToImage(m_folderIndex + 1);
}

QByteArray TranscriptionController::releaseImageIfUnread(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_document.pages.size()) {
        return QByteArray();
    }
    const TranscribedPage &page = m_document.pages.at(pageIndex);
    if (page.imageUrl.isEmpty()) {
        // A local folio's image is the only copy Milah has of it; letting go
        // would mean the transcription could not show what was being read.
        return QByteArray();
    }
    if (!isUntouched(page)) {
        // Worked on, so it is kept — and saved, so the folio can still be seen
        // beside its text on a machine with no internet.
        return QByteArray();
    }
    // Taken rather than removed, so a navigation that is then refused can put
    // the picture back instead of leaving the transcriber looking at a blank
    // folio they never left.
    return m_images.take(page.imageEntry);
}

bool TranscriptionController::ensureImageFetched()
{
    m_imageFailure.clear();

    TranscribedPage *page = mutablePage();
    if (!page || page->imageUrl.isEmpty() || m_images.contains(page->imageEntry)) {
        // Nothing to fetch, or it is already held. Neither is a failure.
        return true;
    }

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }

    const QString folio = page->imageLabel.isEmpty() ? page->imageName : page->imageLabel;
    setMessage(QStringLiteral("Fetching %1…").arg(folio));
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Redirects off the host are followed here, unlike the manifests: a folio's
    // address belongs to the library's image server, which redirects freely,
    // and refusing to follow would simply not fetch the picture.
    QNetworkReply *reply =
        fetch(m_network, QUrl(page->imageUrl), Redirects::AnywhereNoLessSafe, m_preferIPv4);

    // Waited for rather than handled later. Everything else here assumes the
    // page it is on is the page on screen, and a folio is one picture that the
    // transcriber is sitting looking at — an asynchronous version would have to
    // answer what the grid shows in the meantime, which is a bigger change than
    // this feature is worth.
    QEventLoop waiting;
    connect(reply, &QNetworkReply::finished, &waiting, &QEventLoop::quit);
    waiting.exec();

    QApplication::restoreOverrideCursor();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_imageFailure = QStringLiteral("%1 could not be fetched.\n%2")
                             .arg(folio, reply->errorString());
        setMessage(QStringLiteral("Could not fetch %1: %2")
                       .arg(folio, reply->errorString()));
        return false;
    }

    const QByteArray bytes = reply->read(ScanImageLimit);
    if (bytes.isEmpty()) {
        m_imageFailure = QStringLiteral("%1 arrived empty.").arg(folio);
        setMessage(m_imageFailure);
        return false;
    }
    m_images.insert(page->imageEntry, bytes);
    setMessage(QStringLiteral("Transcribing %1.").arg(folio));
    return true;
}

void TranscriptionController::openOnlineScan()
{
    if (!confirmDiscard()) {
        return;
    }

    OnlineScanDialog picker(m_dialogParent);
    if (picker.exec() != QDialog::Accepted || picker.chosen().id.isEmpty()) {
        return;
    }
    const ScanEntry scan = picker.chosen();

    // Offered rather than applied: the library's record and the transcriber's
    // own judgement are both worth something, and only one of them is here.
    ScanMetadataDialog details(scan, m_document.metadata, m_dialogParent);
    if (details.exec() != QDialog::Accepted) {
        return;
    }

    m_document = TranscriptionDocument();
    m_document.metadata = details.merged();
    m_images.clear();
    m_filePath.clear();
    m_folderImages.clear();
    m_folderIndex = -1;
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedVerse = -1;
    m_selectedColumn = -1;

    m_document.pages.reserve(scan.pages.size());
    for (const ScanPage &folio : scan.pages) {
        TranscribedPage page;
        page.imageName = folio.label.isEmpty()
            ? QStringLiteral("%1").arg(folio.number)
            : folio.label;
        page.imageLabel = folio.label;
        page.imageUrl = folio.imageUrl;
        // Named by the manuscript and the folio as well as by position, so that
        // somebody who opens the archive can see which leaf of which codex an
        // entry is. Sanitised on the way in rather than trusted: a scan's id is
        // whatever its catalogue said, a catalogue that names no manuscript has
        // only the manifest address to give, and an entry name with "https://"
        // in it is refused by the archive writer — which meant the transcription
        // could not be saved, and so the page could not be turned.
        page.imageEntry = imageEntryFor(
            m_document.pages.size(),
            QStringLiteral("%1-%2.jpg")
                .arg(scan.id)
                .arg(folio.number, 4, 10, QLatin1Char('0')));
        page.book = scan.book;
        m_document.pages.append(page);
    }

    m_currentPage = m_document.pages.isEmpty() ? -1 : 0;
    ensureTypingRoom();
    const bool fetched = ensureImageFetched();

    // Cleared rather than simply left alone. A scan just opened is one nobody
    // has read anything off yet — but confirmDiscard() returns true on Discard
    // without clearing the mark, and the document discarded a moment ago was
    // replaced wholesale just above, so its mark would otherwise follow the new
    // scan in and offer to save a manuscript nobody has touched.
    setDirty(false);
    // Only when there is something to celebrate. A scan whose first folio did
    // not arrive says so instead: the credit line will still be there once the
    // transcriber has a picture to credit.
    if (fetched) {
        setMessage(QStringLiteral("%1 — %2 folios. %3")
                       .arg(scan.displayTitle())
                       .arg(scan.pages.size())
                       .arg(scan.attribution));
    }
    emit documentChanged();
    emit pageChanged();
    emit versesChanged();
    emit selectionChanged();
    emit historyChanged();
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
    // The one place a transcription is written, so the one place its whereabouts
    // has to be recorded.
    rememberRecentFile(QLatin1String(RecentTranscriptionsKey), path);
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

    loadTranscriptionFrom(path);
}

void TranscriptionController::openRecentTranscription(const QString &path)
{
    if (!confirmDiscard()) {
        return;
    }
    if (!loadTranscriptionFrom(path)) {
        // It was offered and it did not open. Leaving it on the menu would be
        // offering it again.
        forgetRecentFile(QLatin1String(RecentTranscriptionsKey), path);
    }
}

bool TranscriptionController::loadTranscriptionFrom(const QString &path)
{
    QString error;
    const QJsonObject read =
        ProjectStorage::loadFromPath(path, &error, ProjectStorage::ImageEntryLimit);
    if (read.isEmpty()) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), error);
        return false;
    }

    const MilahProjectPayload payload = payloadFromJson(read);
    TranscriptionDocument document;
    try {
        document = restoreTranscription(payload);
    } catch (const ProjectError &failure) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), failure.message());
        return false;
    }

    m_document = document;
    m_images = transcriptionImages(payload);
    m_filePath = path;
    m_currentPage = m_document.pages.isEmpty() ? -1 : 0;
    ensureTypingRoom();
    // A folio of a scan that was never worked on was not saved with the file,
    // so it is fetched again — which is the bargain that keeps a transcription
    // of six folios from weighing what a codex weighs.
    const bool fetched = ensureImageFetched();
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
    rememberRecentFile(QLatin1String(RecentTranscriptionsKey), path);
    setDirty(false);
    // The file did open, whatever became of the folio — but "Opened …" over the
    // top of "could not be fetched" would be the one of the two the transcriber
    // cannot act on.
    if (fetched) {
        setMessage(QStringLiteral("Opened %1.").arg(QFileInfo(path).fileName()));
    }
    emit documentChanged();
    emit pageChanged();
    emit versesChanged();
    emit selectionChanged();
    emit historyChanged();
    return true;
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

WorkMetadata TranscriptionController::workMetadata() const
{
    WorkMetadata work;
    work.workId = QStringLiteral("Milah.Transcription");
    // A transcription is a witness, not an edition, and the library's own files
    // say so in this line.
    work.workType = QStringLiteral("x-manuscript");
    work.language = m_document.metadata.language.isEmpty()
        ? QStringLiteral("he")
        : m_document.metadata.language;
    work.title = m_document.metadata.manuscriptName.isEmpty()
        ? QStringLiteral("Milah Transcription")
        : m_document.metadata.manuscriptName;
    // The shelfmark, and not the library holding it. These were crossed: the
    // institution went out as x-shelfmark — which is what names a witness in
    // the comparison and fills the download window's Shelfmark column — while
    // the shelfmark itself reached no file at all.
    if (!m_document.metadata.shelfmark.isEmpty()) {
        work.identifiers.insert(
            QStringLiteral("x-shelfmark"), m_document.metadata.shelfmark);
    }
    if (!m_document.metadata.libraryMark.isEmpty()) {
        work.identifiers.insert(
            QStringLiteral("x-repository"), m_document.metadata.libraryMark);
    }
    if (!m_document.metadata.transcriber.isEmpty()) {
        work.identifiers.insert(
            QStringLiteral("x-transcriber"), m_document.metadata.transcriber);
    }

    // Keyed exactly as the published repository's manifest generator reads
    // them, so a transcription added to the library describes itself in the
    // download window without any of this being written out a second time.
    // Empty ones are dropped by the writer, so unanswered stays unwritten.
    work.descriptions.insert(QStringLiteral("x-folios"), m_document.metadata.folios);
    work.descriptions.insert(QStringLiteral("x-material"), m_document.metadata.material);
    work.descriptions.insert(
        QStringLiteral("x-provenance"), m_document.metadata.provenance);
    work.descriptions.insert(
        QStringLiteral("x-translated-from"), m_document.metadata.translatedFrom);
    work.descriptions.insert(QStringLiteral("x-exemplar"), m_document.metadata.exemplar);

    return work;
}

TranscriptionController::Exportable TranscriptionController::exportable() const
{
    // The transcription is shaped into the same drafts the edition exports, so
    // the OSIS a folio produces and the OSIS an edition produces are written by
    // one function and cannot drift apart.
    Exportable out;

    for (const TranscribedPage &page : m_document.pages) {
        for (int index = 0; index < page.verses.size(); ++index) {
            const TranscribedVerse &verse = page.verses.at(index);
            const QString id = transcribedVerseId(page, index);
            if (id.isEmpty()) {
                ++out.unnamed;
                for (const TranscribedWord &word : verse.words) {
                    if (!word.note.isEmpty()) {
                        // A note is anchored to a verse by its osisID, and this
                        // verse has none — so it goes nowhere, and that is
                        // worth saying rather than discovering later.
                        ++out.strandedNotes;
                    }
                }
                continue;
            }

            CombinedDraft draft;
            draft.reference.id = id;
            draft.reference.book = page.book;
            draft.reference.chapter = chapterOfVerse(page, index);
            draft.reference.verse = verse.number;

            QMap<int, QString> verseGlosses;
            QList<int> notedColumns;
            for (int column = 0; column < verse.words.size(); ++column) {
                const TranscribedWord &word = verse.words.at(column);
                ConsensusColumn cell;
                cell.text = word.hebrew;
                draft.columns.append(cell);
                if (!word.english.isEmpty()) {
                    verseGlosses.insert(column, word.english);
                }
                if (!word.note.isEmpty()) {
                    notedColumns.append(column);
                }
            }

            // Anchored twice, because the two writers ask different questions.
            // The interlinear one writes each word separately and wants to know
            // which word; the plain one writes running text and wants to know
            // how many characters in. Answering only one of them is how every
            // note in a verse ends up piled onto its first word.
            for (const int column : notedColumns) {
                SourceNote note;
                note.text = verse.words.at(column).note;
                note.tokenIndex = column;
                note.charOffset = columnCharOffset(draft, column);
                note.number = QString::number(out.apparatus.notes[id].size() + 1);
                out.apparatus.notes[id].append(note);
            }

            if (!verseGlosses.isEmpty()) {
                out.glosses.insert(id, verseGlosses);
            }
            out.drafts.insert(id, draft);
        }
    }

    return out;
}

/// What to say when some of the folio could not be addressed. Empty when all of
/// it could.
QString TranscriptionController::unaddressedNotice(const Exportable &work)
{
    if (work.unnamed == 0) {
        return QString();
    }
    // Named rather than dropped quietly: a transcriber who has not filled the
    // Book field in would otherwise see a successful export missing a folio.
    return work.strandedNotes > 0
        ? QStringLiteral(" %1 verses could not be addressed and were left out — they "
                         "need a book and a verse number — and %2 of your notes went "
                         "with them.")
              .arg(work.unnamed)
              .arg(work.strandedNotes)
        : QStringLiteral(" %1 verses could not be addressed and were left out — they "
                         "need a book and a verse number.")
              .arg(work.unnamed);
}

bool TranscriptionController::writeOsisTo(const QString &path, const QString &osis)
{
    // Through a QSaveFile, so an interrupted write leaves nothing behind: half
    // a manuscript in the library would be worse than none, because it would
    // look like one.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(osis.toUtf8()) < 0
        || !file.commit()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Could not write %1.").arg(path));
        return false;
    }
    return true;
}

void TranscriptionController::exportOsis()
{
    if (!hasDocument()) {
        setMessage(QStringLiteral("There is nothing to export yet."));
        return;
    }

    const Exportable work = exportable();
    if (work.drafts.isEmpty()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Nothing can be exported yet: OSIS addresses a verse by "
                           "book, chapter and number, and none of the verses "
                           "transcribed so far has all three."));
        return;
    }

    const WorkMetadata metadata = workMetadata();
    const QString suggested = QStringLiteral("%1.osis").arg(metadata.title);
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

    // Three editions of the one transcription, as the comparison exports its
    // own: the text by itself, which is what another program wants; the text
    // with the remarks, which is what the library holds; and the text with the
    // English, which is what nothing else carries.
    const QFileInfo chosen(path);
    const QString base =
        chosen.absolutePath() + QLatin1Char('/') + chosen.completeBaseName();
    const QString suffix =
        chosen.suffix().isEmpty() ? QStringLiteral("osis") : chosen.suffix();

    QStringList written;
    if (!writeOsisTo(path, serializeCombinedOsis(work.drafts, metadata))) {
        return;
    }
    written.append(chosen.fileName());

    // The other two only where they would carry something a reader has not
    // already got: an empty apparatus makes a commented edition that is the
    // plain one under another name.
    if (!work.apparatus.notes.isEmpty()) {
        const QString commented = QStringLiteral("%1-commented.%2").arg(base, suffix);
        if (!writeOsisTo(
                commented,
                serializeCombinedOsis(work.drafts, metadata, work.apparatus))) {
            return;
        }
        written.append(QFileInfo(commented).fileName());
    }
    if (!work.glosses.isEmpty()) {
        const QString interlinear = QStringLiteral("%1-interlinear.%2").arg(base, suffix);
        if (!writeOsisTo(
                interlinear,
                serializeInterlinearOsis(
                    work.drafts, work.glosses, metadata, work.apparatus))) {
            return;
        }
        written.append(QFileInfo(interlinear).fileName());
    }

    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"), chosen.absolutePath());
    setMessage(QStringLiteral("Exported %1 verses to %2.%3")
                   .arg(work.drafts.size())
                   .arg(written.join(QStringLiteral(", ")))
                   .arg(unaddressedNotice(work)));
}

void TranscriptionController::exportWord()
{
    if (!hasDocument()) {
        setMessage(QStringLiteral("There is nothing to export yet."));
        return;
    }

    const DocxDocument reading = readingWordDocument(m_document);
    if (reading.blocks.isEmpty()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Nothing has been read off this manuscript yet, so "
                           "there is nothing to put in a document."));
        return;
    }

    const QString name = m_document.metadata.manuscriptName.isEmpty()
        ? QStringLiteral("Transcription")
        : m_document.metadata.manuscriptName;
    const QString suggested = QStringLiteral("%1.docx").arg(name);
    const QString directory =
        QSettings().value(QStringLiteral("paths/lastDirectory")).toString();
    QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Export the transcription as Word documents"),
        directory.isEmpty() ? suggested : QDir(directory).filePath(suggested),
        QStringLiteral("Word documents (*.docx);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".docx"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".docx");
    }

    // Two documents from the one command, the way Export to OSIS writes its
    // three: the reading text under the name that was chosen, and the
    // interlinear beside it.
    const QFileInfo chosen(path);
    const QString interlinearPath = QStringLiteral("%1/%2-interlinear.docx")
                                        .arg(chosen.absolutePath(), chosen.completeBaseName());

    QString error;
    if (!writeDocx(path, reading, &error)) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), error);
        setMessage(error);
        return;
    }
    if (!writeDocx(interlinearPath, interlinearWordDocument(m_document), &error)) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), error);
        setMessage(error);
        return;
    }

    QSettings().setValue(QStringLiteral("paths/lastDirectory"), chosen.absolutePath());

    const int unnamed = unnamedVerseCount(m_document);
    const QString notice = unnamed == 0
        ? QString()
        : QStringLiteral(" %1 %2 no book, chapter and number between them, and "
                         "are headed by as much as is known.")
              .arg(unnamed)
              .arg(unnamed == 1 ? QStringLiteral("verse has")
                                : QStringLiteral("verses have"));
    setMessage(QStringLiteral("Exported %1 and %2.%3")
                   .arg(chosen.fileName(), QFileInfo(interlinearPath).fileName(), notice));
}

void TranscriptionController::addToLibrary()
{
    if (!hasDocument()) {
        setMessage(QStringLiteral("There is nothing to add yet."));
        return;
    }

    const Exportable work = exportable();
    if (work.drafts.isEmpty()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Nothing can be added to your library yet: a manuscript is "
                           "filed by its book, and none of the verses transcribed so "
                           "far has a book, a chapter and a number."));
        return;
    }

    const QString directory = manuscriptWriteDirectory();
    if (directory.isEmpty()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("There is nowhere to keep a library on this machine."));
        return;
    }

    // One file per book, because that is how the published library is
    // organised: a witness is a book of a manuscript, and a codex of
    // twenty-six of them is twenty-six texts to collate separately.
    QMap<QString, QMap<QString, CombinedDraft>> byBook;
    for (auto draft = work.drafts.constBegin(); draft != work.drafts.constEnd(); ++draft) {
        byBook[draft->reference.book].insert(draft.key(), draft.value());
    }

    const WorkMetadata metadata = workMetadata();
    QMap<QString, QString> files; // path -> contents
    QStringList existing;
    for (auto book = byBook.constBegin(); book != byBook.constEnd(); ++book) {
        WorkMetadata one = metadata;
        // Which book this file is, which is what the manifest generator and the
        // download list group by.
        one.scope = book.key();

        const QString name =
            libraryFileName(book.key(), m_document.metadata.manuscriptName);
        const QString path = QDir(directory).filePath(name);
        files.insert(path, serializeCombinedOsis(book.value(), one, work.apparatus));
        if (QFileInfo::exists(path)) {
            existing.append(name);
        }
    }

    if (!existing.isEmpty()) {
        // Downloads replace silently, because re-downloading is how a
        // correction is taken. Here the name could just as easily belong to a
        // published manuscript somebody spent a year on.
        const QMessageBox::StandardButton answer = QMessageBox::question(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Your library already holds %1.\nReplace %2?")
                .arg(
                    existing.join(QStringLiteral(", ")),
                    existing.size() == 1 ? QStringLiteral("it") : QStringLiteral("them")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    QDir().mkpath(directory);
    QStringList written;
    for (auto file = files.constBegin(); file != files.constEnd(); ++file) {
        if (!writeOsisTo(file.key(), file.value())) {
            return;
        }
        written.append(QFileInfo(file.key()).fileName());
    }

    setMessage(QStringLiteral("Added %1 to your library. Load it from the Textual "
                              "criticism tab with File ▸ Load manuscripts.%2")
                   .arg(written.join(QStringLiteral(", ")))
                   .arg(unaddressedNotice(work)));
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

void TranscriptionController::startVerse(
    int verse, int column, const QString &number, const QString &firstWord)
{
    if (!isValid(verse, column)) {
        return;
    }
    pushUndo();
    TranscribedPage *page = mutablePage();
    QList<TranscribedWord> &words = page->verses[verse].words;

    TranscribedVerse opened;
    opened.number = number;
    if (!firstWord.isEmpty()) {
        // What stood after the number in the cell it was typed in. It opens the
        // new verse, ahead of the words that followed on the line.
        TranscribedWord carried;
        carried.hebrew = firstWord;
        carried.english = suggestedGloss(firstWord);
        opened.words.append(carried);
    }
    // Everything after the number goes with it. A number typed between spaces
    // is a boundary wherever it falls, and the words beyond it are the new
    // verse's — leaving them behind would put the back half of one verse under
    // the number of the one before it.
    for (int index = column + 1; index < words.size(); ++index) {
        opened.words.append(words.at(index));
    }
    words.remove(column, words.size() - column);

    page->verses.insert(verse + 1, opened);
    ensureTypingRoom();
    // The caret is about to be put in the new verse, and the word it was in no
    // longer exists.
    m_selectedVerse = -1;
    m_selectedColumn = -1;
    setDirty(true);
    emit versesChanged();
    emit selectionChanged();
}

void TranscriptionController::pasteAt(int verse, int column, const QString &text)
{
    if (!isValid(verse, column)) {
        return;
    }
    const QList<TranscribedVerse> pasted = parseTranscribedText(text);
    if (pasted.isEmpty()) {
        return;
    }

    pushUndo();
    TranscribedPage *page = mutablePage();
    QList<TranscribedWord> &words = page->verses[verse].words;

    // The words after this one on the line. They keep their place at the end of
    // whatever the pasted text turns into, so pasting into the middle of a
    // verse pushes the rest along rather than overwriting it.
    QList<TranscribedWord> trailing;
    for (int index = column + 1; index < words.size(); ++index) {
        trailing.append(words.at(index));
    }
    words.remove(column, words.size() - column);

    // Whether the number the paste opens with, if it opens with one, can be
    // this verse's own: only where the verse has none and nothing has been read
    // off it. Anywhere else that number belongs to a verse of its own — which
    // is what the same digits typed by hand would have opened, and what this
    // used to drop on the floor instead.
    const bool opensWithNumber = !pasted.constFirst().number.isEmpty();
    bool adoptable = page->verses.at(verse).number.isEmpty();
    if (adoptable) {
        for (const TranscribedWord &word : page->verses.at(verse).words) {
            if (!word.hebrew.isEmpty()) {
                adoptable = false;
                break;
            }
        }
    }

    // The first of the pasted verses joins the one being typed in — text cut
    // out of the middle of a chapter opens mid-verse and has no number to give.
    int firstToOpen = 0;
    if (!opensWithNumber || adoptable) {
        for (const TranscribedWord &word : pasted.constFirst().words) {
            TranscribedWord read = word;
            read.english = suggestedGloss(read.hebrew);
            words.append(read);
        }
        if (opensWithNumber) {
            page->verses[verse].number = pasted.constFirst().number;
        }
        firstToOpen = 1;
    }

    int landedIn = verse;
    for (int index = firstToOpen; index < pasted.size(); ++index) {
        TranscribedVerse opened = pasted.at(index);
        for (TranscribedWord &word : opened.words) {
            word.english = suggestedGloss(word.hebrew);
        }
        page->verses.insert(++landedIn, opened);
    }

    page->verses[landedIn].words.append(trailing);

    ensureTypingRoom();
    m_selectedVerse = -1;
    m_selectedColumn = -1;
    setDirty(true);
    emit versesChanged();
    emit selectionChanged();
}

void TranscriptionController::setNote(int verse, int column, const QString &note)
{
    if (!isValid(verse, column)) {
        return;
    }
    TranscribedPage *page = mutablePage();
    TranscribedWord &word = page->verses[verse].words[column];
    const QString trimmed = note.trimmed();
    if (word.note == trimmed) {
        return;
    }
    pushUndo();
    word.note = trimmed;
    setDirty(true);
    emit versesChanged();
    // The panel shows the note for whichever word is selected, and this is that
    // word: it has to be told the note it is displaying has just changed.
    emit selectionChanged();
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
