#include "transcription_controller.h"

#include "core/alignment.h"
#include "core/books.h"
#include "core/lexicon.h"
#include "core/line_fill.h"
#include "core/manuscript_catalogue.h"
#include "core/osis.h"
#include "core/osis_fill.h"
#include "core/project.h"
#include "core/recent_files.h"
#include "core/serialize.h"
#include "core/training_export.h"
#include "core/transcription_docx.h"
#include "core/suggestions.h"
#include "project_storage.h"
#include "ui/htr_last_run_dialog.h"
#include "ui/htr_models_dialog.h"
#include "ui/htr_setup_dialog.h"
#include "ui/htr_strips_dialog.h"
#include "ui/htr_training_dialog.h"
#include "ui/iiif_image.h"
#include "ui/osis_fill_dialog.h"
#include "ui/training_set.h"
#include "ui/network_fetch.h"
#include "ui/online_scan_dialog.h"
#include "ui/scan_metadata_dialog.h"

#include <QBuffer>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QApplication>
#include <QEventLoop>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>

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

/// How many pieces Milah will fetch to build one folio at its largest size.
///
/// Cambridge comes to six and Manchester to twelve. A service that advertised an
/// absurd size against a tiny cap would otherwise fire off hundreds of requests
/// at somebody's library on a single button press.
constexpr int MaxTilesPerFolio = 32;

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

    const QString suggested = QStringLiteral("%1.trscrpt")
                                  .arg(transcriptionFileStem(m_document, m_currentPage));
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
    // The gloss is the lexicon's answer, so a file written against an older
    // lexicon carries older answers — and the Strong's number beside it is
    // derived at draw time and would be the new one, leaving the two visibly
    // contradicting each other. Only where the transcriber has not written the
    // gloss themselves: englishIsOwn is exactly the record of which are theirs.
    //
    // Not marked dirty. Opening a transcription must not, by itself, make one;
    // the file catches up at the next save.
    for (TranscribedPage &page : m_document.pages) {
        for (TranscribedVerse &verse : page.verses) {
            for (TranscribedWord &word : verse.words) {
                if (!word.englishIsOwn) {
                    word.english = suggestedGloss(word.hebrew);
                }
            }
        }
    }
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

int TranscriptionController::uncheckedWordCount() const
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return 0;
    }
    int count = 0;
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.unchecked) {
                ++count;
            }
        }
    }
    return count;
}

bool TranscriptionController::hasRecognisedWords() const
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return false;
    }
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (!word.box.isNull()) {
                return true;
            }
        }
    }
    return false;
}

bool TranscriptionController::hasRecognisedLines() const
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return false;
    }
    // Of the words rather than of page->lines, for the same reason the folio
    // view asks it that way: a line box can be drawn round a line's words with
    // no stored geometry at all, which is every folio read before Milah began
    // keeping what the segmenter drew.
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line >= 0 && !word.box.isNull()) {
                return true;
            }
        }
    }
    return false;
}

QSize TranscriptionController::folioPixelSize() const
{
    const QByteArray bytes = currentImageBytes();
    if (bytes.isEmpty()) {
        return QSize();
    }
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    // The same transformation the image pane applies, so that the size the
    // boxes are converted into is the size they will be drawn against. A folio
    // whose file says it is rotated is a folio whose width and height swap, and
    // getting that the wrong way round would put every word on the page at
    // right angles to its ink.
    reader.setAutoTransform(true);
    return reader.size();
}

bool TranscriptionController::applyRecognition(
    const RecognisedPage &recognised,
    const QString &source)
{
    if (!mutablePage()) {
        // Said out loud. This is one of the two ways a whole recognition can
        // end in nothing at all, and the status bar is not where somebody who
        // has waited minutes for a folio will be looking.
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("There is no folio open to read %1's words onto, so "
                           "nothing has been changed.")
                .arg(source));
        return false;
    }

    if (recognised.words.isEmpty()) {
        QMessageBox::information(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("%1 has no words on it, so there is nothing to read "
                           "onto this folio.")
                .arg(source));
        return false;
    }

    // A folio somebody has already read is not a folio to overwrite without
    // asking. Undo would bring it back, but a transcriber who has just lost an
    // hour's work does not yet know that.
    if (!isUntouched(*currentPage())
        && QMessageBox::question(
               m_dialogParent,
               QStringLiteral("Milah"),
               QStringLiteral("This folio already has text on it. Replace all of "
                              "it with what %1 read?")
                   .arg(source),
               QMessageBox::Yes | QMessageBox::Cancel,
               QMessageBox::Cancel)
            != QMessageBox::Yes) {
        return false;
    }

    // The recogniser may have been given a copy of the folio at another size —
    // an institution's export is made from whatever it holds, not from what
    // Milah fetched. Converting here, once, is what lets TranscribedWord::box
    // mean the folio's own pixels everywhere else.
    const QSize folio = folioPixelSize();
    double scaleX = 1.0;
    double scaleY = 1.0;
    if (folio.isValid() && recognised.imageSize.isValid() && folio != recognised.imageSize) {
        scaleX = double(folio.width()) / recognised.imageSize.width();
        scaleY = double(folio.height()) / recognised.imageSize.height();
    }

    TranscribedVerse verse;
    verse.words.reserve(recognised.words.size());
    for (const RecognisedWord &read : recognised.words) {
        TranscribedWord word;
        word.hebrew = read.text;
        // Kept as well as used. A fill overwrites the reading with a word of the
        // published work, and a box that turns out to be a marginal note has to
        // be able to go back to what was actually read there.
        word.recognised = read.text;
        // The same lexicon lookup a typed word gets. A machine reading a folio
        // does not change what the words mean.
        word.english = suggestedGloss(read.text);
        word.unchecked = true;
        // Which line it came off, so a folio corrected today can be handed back
        // as training data months from now, when the layout file this reading
        // came from is long overwritten.
        word.line = read.line;
        if (!read.box.isNull()) {
            word.box = QRect(
                qRound(read.box.x() * scaleX),
                qRound(read.box.y() * scaleY),
                qRound(read.box.width() * scaleX),
                qRound(read.box.height() * scaleY));
        }
        verse.words.append(word);
    }

    // What the segmenter drew, carried across in the same space as the boxes.
    // A recogniser cuts its training strips from these, so keeping Milah's own
    // invention instead costs the model a sheared and contaminated line — see
    // TranscribedLine.
    QList<TranscribedLine> lines;
    lines.reserve(recognised.lines.size());
    for (const RecognisedLine &read : recognised.lines) {
        TranscribedLine line;
        line.index = read.index;
        for (const QPoint &point : read.baseline) {
            line.baseline.append(
                QPoint(qRound(point.x() * scaleX), qRound(point.y() * scaleY)));
        }
        for (const QPoint &point : read.boundary) {
            line.boundary.append(
                QPoint(qRound(point.x() * scaleX), qRound(point.y() * scaleY)));
        }
        lines.append(line);
    }

    pushUndo();
    // One verse with no number, in the order the page was read. Nothing here
    // knows where the verses of this chapter begin — that is the transcriber's
    // to say, and typing a number in front of a word is how they say it.
    mutablePage()->verses = {verse};
    mutablePage()->lines = lines;
    ensureTypingRoom();
    m_selectedVerse = -1;
    m_selectedColumn = -1;
    setDirty(true);
    setMessage(QStringLiteral("Read %1 words off the folio. None has been "
                              "checked yet — editing a word marks it checked.")
                   .arg(verse.words.size()));
    emit versesChanged();
    emit selectionChanged();
    // Last, and only from here — the refusals above all return before it, so a
    // folio nobody agreed to overwrite leaves the overlay exactly as it was.
    emit recognitionApplied();
    return true;
}

void TranscriptionController::importRecognisedLayout()
{
    if (!currentPage()) {
        setMessage(QStringLiteral("Open a folio before reading a layout onto it."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Open a recognised layout"),
        QSettings().value(QStringLiteral("paths/lastDirectory")).toString(),
        QStringLiteral("Layout files (*.xml *.alto);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"), QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("%1 could not be opened.\n%2")
                .arg(QFileInfo(path).fileName(), file.errorString()));
        return;
    }

    QString error;
    const RecognisedPage recognised = parseRecognisedPage(file.readAll(), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), error);
        return;
    }

    applyRecognition(recognised, QFileInfo(path).fileName());
}

KrakenEnvironment &TranscriptionController::kraken() const
{
    if (!m_kraken) {
        m_kraken = std::make_unique<KrakenEnvironment>();
    }
    return *m_kraken;
}

KrakenEnvironment::State TranscriptionController::krakenState() const
{
    return kraken().state();
}

bool TranscriptionController::krakenInstalled() const
{
    // Anything on disk, not just a working installation — see
    // KrakenEnvironment::hasInstallation.
    return kraken().hasInstallation();
}

void TranscriptionController::setUpKraken()
{
    kraken().refresh();
    HtrSetupDialog dialog(&kraken(), m_dialogParent);
    dialog.exec();
    emit documentChanged();
}

namespace {

/// The pixel size of an encoded image, without decoding the whole of it.
QSize sizeOfImage(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return QSize();
    }
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    // The same transformation the image pane applies, so the size the boxes
    // were stored against is the size they are written out against.
    reader.setAutoTransform(true);
    return reader.size();
}

} // namespace

int TranscriptionController::trainableLineCount() const
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return 0;
    }
    const QSize size = sizeOfImage(m_images.value(page->imageEntry));
    if (!size.isValid()) {
        return 0;
    }
    // The name does not matter for counting, only for writing, so any non-empty
    // one will do here.
    return trainingAlto(*page, QStringLiteral("folio.jpg"), size).lines;
}

QByteArray TranscriptionController::fetchMasterImage(
    const TranscribedPage &page, QString *note)
{
    const QByteArray held = m_images.value(page.imageEntry);
    const auto settle = [note](const QString &why) {
        if (note) {
            *note = why;
        }
    };

    const QUrl address(page.imageUrl);
    if (!IiifImage::looksLikeImageApi(address)) {
        // A local file, or a library that serves one fixed size. Nothing to ask.
        settle(QString());
        return held;
    }

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    const auto get = [this](const QUrl &url) {
        QNetworkReply *reply =
            fetch(m_network, url, Redirects::AnywhereNoLessSafe, m_preferIPv4);
        QEventLoop waiting;
        connect(reply, &QNetworkReply::finished, &waiting, &QEventLoop::quit);
        waiting.exec();
        reply->deleteLater();
        return reply->error() == QNetworkReply::NoError ? reply->read(ScanImageLimit)
                                                        : QByteArray();
    };

    const IiifImage::Service service = IiifImage::serviceFromInfo(get(IiifImage::infoUrl(address)));
    if (!service.isValid()) {
        settle(QStringLiteral("The library did not say what size it holds, so the "
                              "picture on screen was used."));
        return held;
    }
    const QList<QRect> grid = IiifImage::tiles(service);
    if (grid.isEmpty() || grid.size() > MaxTilesPerFolio) {
        settle(QStringLiteral("The library's largest scan would take %1 requests, "
                              "which Milah will not do; the picture on screen was "
                              "used.")
                   .arg(grid.size()));
        return held;
    }

    const QString folio = page.imageLabel.isEmpty() ? page.imageName : page.imageLabel;
    QProgressDialog progress(
        QStringLiteral("Fetching %1 at full size…").arg(folio),
        QStringLiteral("Cancel"),
        0,
        int(grid.size()),
        m_dialogParent);
    progress.setWindowTitle(QStringLiteral("Milah"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QImage composed(service.full, QImage::Format_RGB32);
    composed.fill(Qt::white);
    QPainter painter(&composed);
    for (int index = 0; index < grid.size(); ++index) {
        progress.setValue(index);
        progress.setLabelText(QStringLiteral("Fetching %1 at full size, piece %2 of %3…")
                                  .arg(folio)
                                  .arg(index + 1)
                                  .arg(grid.size()));
        QCoreApplication::processEvents();
        if (progress.wasCanceled()) {
            settle(QStringLiteral("Stopped; the picture on screen was used."));
            return held;
        }

        const QImage tile = QImage::fromData(get(IiifImage::tileUrl(address, grid.at(index))));
        if (tile.isNull() || tile.size() != grid.at(index).size()) {
            // A short or missing piece would leave a hole in the folio, and a
            // hole is a line the model is taught out of blank paper.
            settle(QStringLiteral("Piece %1 of the largest scan did not arrive whole, "
                                  "so the picture on screen was used.")
                       .arg(index + 1));
            return held;
        }
        painter.drawImage(grid.at(index).topLeft(), tile);
    }
    painter.end();
    progress.setValue(int(grid.size()));

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!composed.save(&buffer, "JPG", 92) || bytes.isEmpty()) {
        settle(QStringLiteral("The pieces could not be put back together, so the "
                              "picture on screen was used."));
        return held;
    }
    settle(QString());
    return bytes;
}

QList<TrainingStrip> TranscriptionController::cutTrainingStrips(
    const TranscribedPage &page,
    const QByteArray &master,
    const QSize &boxSize,
    const QString &label,
    QString *failure)
{
    const auto give = [failure](const QString &why) {
        if (failure) {
            *failure = why;
        }
        return QList<TrainingStrip>();
    };

    const QSize size = sizeOfImage(master);
    const TrainingPage truth = trainingAlto(
        page,
        QStringLiteral("folio.jpg"),
        size,
        boxSize == size ? QSize() : boxSize);
    if (truth.isEmpty()) {
        return give(QStringLiteral("%1 could not be written out as a training layout.")
                        .arg(label));
    }

    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        return give(QStringLiteral("A working folder could not be made for the strips."));
    }
    const QString imagePath = workspace.filePath(QStringLiteral("folio.jpg"));
    const QString altoPath = workspace.filePath(QStringLiteral("folio.xml"));
    QFile image(imagePath);
    QFile alto(altoPath);
    if (!image.open(QIODevice::WriteOnly) || image.write(master) != master.size()
        || !alto.open(QIODevice::WriteOnly)
        || alto.write(truth.alto) != truth.alto.size()) {
        return give(QStringLiteral("The folio could not be written out for cutting."));
    }
    image.close();
    alto.close();

    const QString script = kraken().writeStripScript();
    if (script.isEmpty()) {
        return give(QStringLiteral("The strip helper could not be written out."));
    }
    QStringList command =
        kraken().stripsCommand(script, altoPath, imagePath, workspace.path());
    const QString program = command.takeFirst();

    QProgressDialog progress(
        QStringLiteral("Cutting %1's lines the way training cuts them.").arg(label),
        QStringLiteral("Cancel"),
        0,
        truth.lines,
        m_dialogParent);
    progress.setWindowTitle(QStringLiteral("Milah"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QProcess cutter;
    QEventLoop loop;
    QString said;
    QByteArray answer;
    // The manifest comes back on stdout and the progress on stderr, so unlike
    // the recogniser these two are kept apart rather than merged: one of them
    // is going to be parsed.
    QObject::connect(&cutter, &QProcess::readyReadStandardOutput, &loop, [&] {
        answer += cutter.readAllStandardOutput();
    });
    QObject::connect(&cutter, &QProcess::readyReadStandardError, &loop, [&] {
        said += QString::fromUtf8(cutter.readAllStandardError());
        const QStringList spoken = said.split(QLatin1Char('\n'));
        for (const QString &line : spoken) {
            if (!line.startsWith(QLatin1String(KrakenEnvironment::progressMarker()))) {
                continue;
            }
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                progress.setValue(parts.at(1).toInt());
            }
        }
    });
    QObject::connect(
        &cutter,
        &QProcess::finished,
        &loop,
        [&loop](int, QProcess::ExitStatus) { loop.quit(); });
    // Milah's own flag, for the reason spelled out in transcribeFolio(): a
    // progress dialog emits canceled() on its way off the screen as well as
    // when the button is pressed.
    bool cancelled = false;
    QObject::connect(&progress, &QProgressDialog::canceled, &cutter, [&] {
        if (cutter.state() == QProcess::NotRunning) {
            return;
        }
        cancelled = true;
        cutter.kill();
    });

    cutter.start(program, command);
    if (!cutter.waitForStarted(15000)) {
        return give(QStringLiteral("The strip helper could not be started.\n\n%1\n%2")
                        .arg(program, cutter.errorString()));
    }
    if (cutter.state() != QProcess::NotRunning) {
        loop.exec();
    }
    progress.reset();

    if (cancelled) {
        return give(QString());
    }
    if (cutter.exitStatus() != QProcess::NormalExit || cutter.exitCode() != 0) {
        const int marker = said.indexOf(QLatin1String(KrakenEnvironment::errorMarker()));
        return give(QStringLiteral("The lines could not be cut.\n\n%1")
                        .arg(marker >= 0
                                 ? said.mid(marker).section(QLatin1Char('\n'), 0, 0)
                                 : said.right(2000)));
    }

    QList<TrainingStrip> strips;
    const QJsonArray lines = QJsonDocument::fromJson(answer)
                                 .object()
                                 .value(QStringLiteral("lines"))
                                 .toArray();
    for (const QJsonValue &value : lines) {
        const QJsonObject entry = value.toObject();
        TrainingStrip strip;
        // Kraken carries the ALTO's own line id through, and the ALTO's is the
        // recogniser's line number: core/training_export writes them as line_N.
        // One-based here, to match the number the line view draws.
        strip.line = entry.value(QStringLiteral("id"))
                         .toString()
                         .section(QLatin1Char('_'), -1)
                         .toInt()
            + 1;
        strip.text = entry.value(QStringLiteral("text")).toString();
        strip.refused = entry.value(QStringLiteral("refused")).toString();
        const QString file = entry.value(QStringLiteral("file")).toString();
        if (!file.isEmpty()) {
            // Read here rather than kept as a path: the workspace dies with this
            // function, and a QImage holding a filename would go with it.
            strip.image = QImage(workspace.filePath(file));
            if (strip.image.isNull()) {
                strip.refused =
                    QStringLiteral("The strip was cut but could not be read back.");
            }
        }
        strips.append(strip);
    }

    if (strips.isEmpty()) {
        return give(QStringLiteral("Nothing came back from the strip helper.\n\n%1")
                        .arg(said.right(2000)));
    }
    if (failure) {
        failure->clear();
    }
    return strips;
}

QString TranscriptionController::refusalReport(const QList<TrainingStrip> &strips)
{
    QStringList refused;
    for (const TrainingStrip &strip : strips) {
        if (!strip.refused.isEmpty()) {
            refused << QStringLiteral("line %1 — %2")
                           .arg(strip.line)
                           .arg(strip.refused.toHtmlEscaped());
        }
    }
    if (refused.isEmpty()) {
        return QString();
    }
    // **Written into the set all the same, and that is the point of saying so.**
    // Kraken skips what it cannot cut and carries on; nothing downstream ever
    // mentions it. So the line sits in the set, is counted towards the fifty
    // that open Train a model…, and is never trained on — which makes the set
    // that many lines more than it is. Excluding them here would need this cut
    // to be run on every save whether or not Kraken is installed; saying so does
    // not, and it is what tells somebody whether it is worth doing.
    return QStringLiteral(
               "<p><b>%1 of them will not be trained on.</b> Kraken cannot cut "
               "these lines, and skips them without saying so — they are in the "
               "set and counted there, but a model will never see them:</p>"
               "<p><small>%2</small></p>")
        .arg(refused.size())
        .arg(refused.join(QStringLiteral("<br>")));
}

void TranscriptionController::previewTrainingStrips()
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        setMessage(QStringLiteral("Open a folio before asking what training would "
                                  "be shown."));
        return;
    }
    QWidget *front = m_dialogParent;
    const QString label = page->imageLabel.isEmpty() ? page->imageName : page->imageLabel;

    if (trainableLineCount() == 0) {
        QMessageBox::information(
            front,
            QStringLiteral("Milah"),
            QStringLiteral(
                "No line of %1 has been checked all the way through yet, so there "
                "is nothing training would be shown.<p>Read a line against the ink "
                "and press Space over it in the line view; the number beside each "
                "line says how many of its words are still unread.</p>")
                .arg(label));
        return;
    }

    if (kraken().refresh() != KrakenEnvironment::State::Ready) {
        // A model is not needed to cut a strip — this reads no text — but the
        // installation is, and describe() is where the difference between the
        // several ways of not having one is already written down.
        setMessage(KrakenEnvironment::describe(kraken().state()));
        return;
    }

    // The same picture and the same layout the save would use, so that a preview
    // cannot be a preview of something else. The note says which picture that
    // turned out to be, and travels to the window with the strips.
    QString note;
    const QByteArray master = fetchMasterImage(*page, &note);
    if (master.isEmpty()) {
        QMessageBox::warning(
            front,
            QStringLiteral("Milah"),
            QStringLiteral("%1 has no picture to cut lines out of.").arg(label));
        return;
    }

    QString failure;
    const QList<TrainingStrip> strips = cutTrainingStrips(
        *page, master, sizeOfImage(m_images.value(page->imageEntry)), label, &failure);
    if (strips.isEmpty()) {
        // An empty reason is a cancellation, which needs no window of its own.
        if (failure.isEmpty()) {
            setMessage(QStringLiteral("Stopped. Nothing was changed — this only looks."));
        } else {
            QMessageBox::warning(front, QStringLiteral("Milah"), failure);
        }
        return;
    }

    HtrStripsDialog dialog(label, note, strips, front);
    dialog.exec();
}

void TranscriptionController::saveFolioForTraining()
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        setMessage(QStringLiteral("Open a folio before saving it for training."));
        return;
    }

    const QString label = page->imageLabel.isEmpty() ? page->imageName : page->imageLabel;

    // Nothing is fetched until there is something worth fetching it for.
    if (trainableLineCount() == 0) {
        QMessageBox::information(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral(
                "No line of %1 has been checked all the way through yet, so there "
                "is nothing a recogniser could be taught from it.<p>A line counts "
                "once every word on it has been looked at — moving the caret "
                "through a word is what marks it checked. The status bar says how "
                "many are left.</p>")
                .arg(label));
        return;
    }

    QString note;
    const QByteArray master = fetchMasterImage(*page, &note);
    const QSize onScreen = sizeOfImage(m_images.value(page->imageEntry));
    const int lines = TrainingSet::add(*page, m_document.metadata, master, onScreen);

    if (lines == 0) {
        // The count above said there was something, so getting nothing here is
        // the writing having failed rather than the folio being unfinished.
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("%1 could not be written into the training set.").arg(label));
        return;
    }

    const TrainingSet::Set set =
        TrainingSet::contentsOf(TrainingSet::slugFor(m_document.metadata));
    const QString standing = set.lines >= TrainingSet::EnoughLines
        ? QStringLiteral("That is enough to train on — File ▸ Handwriting "
                         "recognition ▸ Train a model….")
        : QStringLiteral("Train a model… opens at %1 lines.")
              .arg(TrainingSet::EnoughLines);

    // What of it a model will actually see. Kraken skips a line it cannot cut
    // and carries on with a log warning, so a folio can be saved, counted and
    // trained on with lines quietly missing from it — and the moment those lines
    // are committed to the set is the moment worth finding out.
    //
    // Only where there is a Kraken to ask. The saving does not depend on it and
    // has already happened; this adds a sentence to the answer or nothing at
    // all, rather than turning a working save into a failure.
    QString verdict;
    if (kraken().refresh() == KrakenEnvironment::State::Ready) {
        QString failure;
        const QList<TrainingStrip> strips = cutTrainingStrips(
            *page,
            master,
            sizeOfImage(m_images.value(page->imageEntry)),
            label,
            &failure);
        if (!strips.isEmpty()) {
            verdict = refusalReport(strips);
            if (verdict.isEmpty()) {
                verdict = QStringLiteral(
                    "<p><small>All of them cut cleanly — a model will see every "
                    "one.</small></p>");
            }
        }
    }

    QMessageBox::information(
        m_dialogParent,
        QStringLiteral("Milah"),
        QStringLiteral("%1 line(s) off %2 saved.<p>%3 now holds %4 line(s) off %5 "
                       "folio(s). %6</p>%7%8")
            .arg(lines)
            .arg(label, set.label)
            .arg(set.lines)
            .arg(set.folios)
            .arg(
                standing,
                // Said, not swallowed. Training on the small picture works, but
                // it is not what was asked for, and somebody wondering later why
                // a model came out poor deserves to have been told.
                note.isEmpty() ? QString()
                               : QStringLiteral("<p><small>%1</small></p>").arg(note),
                verdict));
    setMessage(QStringLiteral("Saved %1 line(s) off %2 for training.").arg(lines).arg(label));
}

bool TranscriptionController::canFillFromOsis() const
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return false;
    }
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line >= 0 && !word.box.isNull()) {
                return true;
            }
        }
    }
    return false;
}

void TranscriptionController::fillFromOsis(QPoint folioPixel, bool carryOn)
{
    if (!currentPage()) {
        setMessage(QStringLiteral("Open a folio before filling it."));
        return;
    }

    // Where the folio before this one stopped, so a book can run on across a
    // leaf without the place having to be found again. Applied only when it was
    // asked for by name.
    const ResumePoint resume = resumePoint();
    if (carryOn && resume.isValid()) {
        continueFill(folioPixel, resume);
        return;
    }

    // The window opens first and the reading happens inside it. Choosing the
    // transcription is the part that needs a person, and it needs no recognition
    // whatever — so the minute of reading is spent after the deciding rather
    // than in front of it.
    OsisFillDialog::Setup setup;
    setup.folio = [this] { return currentPage(); };
    setup.folioPixel = folioPixel;

    OsisFillDialog dialog(setup, m_dialogParent);
    // Bound after the dialog exists, because the recognition has to raise its
    // progress over it.
    dialog.setReader([this, &dialog] {
        transcribeFolio(&dialog);
        return canFillFromOsis();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    applyFill(dialog.filled(), dialog.wordVerses(), dialog.sourcePath(), dialog.range());
    // Where this folio's text begins, so holding a box out later can lay the
    // rest of it again. Recorded here rather than derived from the leaf before,
    // which the first filled folio of a document has not got.
    if (!dialog.filled().isEmpty()) {
        if (TranscribedPage *page = mutablePage()) {
            page->fillStartVerse = dialog.startVerse();
            page->fillStartWord = dialog.startWord();
        }
    }
}

void TranscriptionController::continueFill(QPoint folioPixel, const ResumePoint &resume)
{
    // Nothing here is a question, so nothing here is a window. The file, the
    // book, the chapter, the verse and the words the last leaf already holds are
    // all settled before this is called; the only thing that appears is the
    // recognition's own progress, which is a two-minute job reporting itself
    // rather than something being asked. Ctrl+Z is what makes that safe.
    QString path = m_document.fillSource;
    if (path.isEmpty() || !QFile::exists(path)) {
        // Only a transcription written before Milah remembered its source, or
        // one whose edition has moved. Asked once: accepting this fill records
        // the answer.
        path = QFileDialog::getOpenFileName(
            m_dialogParent,
            QStringLiteral("Which transcription is this folio carrying on?"),
            QSettings().value(QStringLiteral("paths/lastOsis")).toString(),
            QStringLiteral("OSIS files (*.osis *.xml);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QSettings().setValue(QStringLiteral("paths/lastOsis"), path);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("%1 could not be opened.\n%2")
                .arg(QFileInfo(path).fileName(), file.errorString()));
        return;
    }
    SourceDocument source;
    try {
        source = parseOsis(QString::fromUtf8(file.readAll()), ParseOptions{});
    } catch (const OsisError &failure) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), failure.message());
        return;
    }

    // "JAS.1.25" — the folio's own book code, which the OSIS may spell "Jas".
    const QString book = resume.verse.section(QLatin1Char('.'), 0, 0);
    if (bookNamed(source, book).isEmpty()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("%1 does not contain %2, which is the book this folio "
                           "would be carrying on.")
                .arg(QFileInfo(path).fileName(), book));
        return;
    }

    // Read only now, and only if nothing has: the recognition is the slow part
    // and a folio already read must not be read again.
    if (!canFillFromOsis()) {
        transcribeFolio();
        if (!canFillFromOsis()) {
            // Cancelled, or nothing came back. Said rather than silently doing
            // nothing, which would read as the menu entry being broken.
            setMessage(QStringLiteral("Nothing was read off this folio, so there are "
                                      "no lines to lay the transcription into."));
            return;
        }
    }

    const TranscribedPage *page = currentPage();
    if (!page) {
        return;
    }

    // The folio's lines, and how many words the recogniser found on each — the
    // same counts the window starts from before anybody nudges them. Marginalia
    // are not among them: a note beside the text is not a slot for a word of the
    // work, and pouring into one shifts everything after it by one.
    const QMap<int, QList<QRect>> lines = fillableLines(*page);
    QList<TranscribedWord> words;
    for (const TranscribedVerse &verse : page->verses) {
        words += verse.words;
    }
    if (lines.isEmpty()) {
        return;
    }

    // Through wordCounts(), so a line the transcriber has told how long it is
    // keeps that answer here as well as in a re-flow. The box count is a guess;
    // see TranscribedPage::lineWords.
    const QMap<int, int> held = wordCounts(*page);
    const QList<int> lineIndices = lines.keys();
    QList<int> counts;
    for (const int line : lineIndices) {
        counts.append(held.value(line, int(lines.value(line).size())));
    }

    // Where the transcriber pointed, resolved now that there are lines to
    // resolve it against.
    int startLine = 0;
    const int pointed = folioPixel.isNull() ? -1 : lineAtPoint(words, folioPixel);
    if (pointed >= 0) {
        startLine = std::max(0, int(lineIndices.indexOf(pointed)));
    }

    const Passage passage = gatherPassage(
        source,
        book,
        resume.verse.section(QLatin1Char('.'), 1, 1).toInt(),
        resume.verse.section(QLatin1Char('.'), 2, 2).toInt(),
        resume.word);
    if (passage.isEmpty()) {
        setMessage(
            QStringLiteral("%1 has no more text after %2 — the book ends there.")
                .arg(QFileInfo(path).fileName(), resume.verse));
        return;
    }

    QList<FilledLine> filled;
    QStringList wordVerses;
    for (const LineFill::Laid &laid :
         LineFill::layOut(counts, startLine, 0, int(passage.words.size()))) {
        FilledLine line;
        line.index = lineIndices.at(laid.line);
        line.words = passage.words.mid(laid.from, laid.count);
        line.boxes = LineFill::place(lines.value(line.index), laid.count);
        if (line.boxes.size() != line.words.size()) {
            continue;
        }
        wordVerses += passage.verses.mid(laid.from, laid.count);
        filled.append(line);
    }
    if (filled.isEmpty()) {
        return;
    }

    const QString range = QStringLiteral("%1 – %2")
                              .arg(wordVerses.first(), wordVerses.last());
    applyFill(filled, wordVerses, path, range);
    // The same record the window's fill keeps: where this leaf's text starts.
    if (TranscribedPage *page = mutablePage()) {
        page->fillStartVerse = resume.verse;
        page->fillStartWord = resume.word;
    }
}

void TranscriptionController::applyFill(
    const QList<FilledLine> &filled,
    const QStringList &verses,
    const QString &sourcePath,
    const QString &range,
    bool standalone)
{
    if (filled.isEmpty() || !currentPage()) {
        return;
    }

    // Which lines the fill covers, so everything else is left exactly as it is —
    // the end of the book before it above, and whatever follows below.
    QSet<int> covered;
    for (const FilledLine &line : filled) {
        covered.insert(line.index);
    }

    // A re-flow is part of the step that asked for it — marking a box — and
    // pushes no undo of its own, so Ctrl+Z takes back the mark and the flow
    // together rather than one at a time.
    if (standalone) {
        pushUndo();
    }
    TranscribedPage *target = mutablePage();

    // Rebuilt rather than overwritten in place: a line can come out with more
    // words than the recogniser found on it, which is the whole point, and a
    // word that was never there cannot be assigned to.
    // What the machine read where, so a poured word can carry it. Keyed by the
    // box it was read at; a word poured onto a piece of a box that place() cut
    // into columns takes the reading of the box it was cut from, since a piece
    // was never separately read.
    // On a folio nothing has poured onto, a boxed word's own text *is* what the
    // machine read there — nothing else could have put it in a box. Kept before
    // the pour overwrites it, which recovers the reading for every folio
    // recognised before Milah started storing it.
    if (target->fillStartLine < 0) {
        for (TranscribedVerse &verse : target->verses) {
            for (TranscribedWord &word : verse.words) {
                if (!word.box.isNull() && word.recognised.isEmpty() && word.unchecked) {
                    word.recognised = word.hebrew;
                }
            }
        }
    }

    QList<QPair<QRect, QString>> readings;
    for (const TranscribedVerse &verse : target->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (!word.box.isNull() && !word.recognised.isEmpty()) {
                readings.append({word.box, word.recognised});
            }
        }
    }
    const auto readingAt = [&readings](const QRect &box) {
        for (const QPair<QRect, QString> &known : readings) {
            if (known.first.contains(box.center())) {
                return known.second;
            }
        }
        return QString();
    };

    QList<TranscribedWord> before;
    QList<TranscribedWord> after;
    // Marginalia standing on a line the fill covers, kept aside to be woven back
    // into it. The pour was laid out around them, so they keep their place.
    QMap<int, QList<TranscribedWord>> heldOut;
    bool passed = false;
    for (const TranscribedVerse &verse : target->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (covered.contains(word.line)) {
                passed = true;
                if (word.marginal) {
                    heldOut[word.line].append(word);
                }
                continue;
            }
            (passed ? after : before).append(word);
        }
    }

    QList<TranscribedWord> poured;
    // The verse each poured word belongs to, built alongside rather than by
    // index into `verses`: the marginalia woven in below have no verse of their
    // own and would otherwise put every word after them one out.
    QStringList pouredVerses;
    int at = 0;
    for (const FilledLine &line : filled) {
        QList<TranscribedWord> row;
        QStringList rowVerses;
        for (int index = 0; index < line.words.size(); ++index) {
            TranscribedWord word;
            word.hebrew = line.words.at(index);
            word.english = suggestedGloss(word.hebrew);
            word.box = line.boxes.at(index);
            word.line = line.index;
            word.recognised = readingAt(word.box);
            // Still nobody's reading. A better machine than the recogniser put
            // these words here, but a machine — and unchecked is exactly the
            // record of that.
            word.unchecked = true;
            row.append(word);
            rowVerses.append(at < verses.size() ? verses.at(at) : QString());
            ++at;
        }

        // Back into reading order, which is what the boxes say. Sorted rather
        // than appended: a note in the middle of a line belongs in the middle of
        // it, and a line whose words arrive out of order groups as two lines in
        // the training export rather than one.
        const QList<TranscribedWord> notes = heldOut.value(line.index);
        for (const TranscribedWord &note : notes) {
            QList<QRect> boxes;
            for (const TranscribedWord &word : row) {
                boxes.append(word.box);
            }
            const bool rightToLeft =
                LineFill::directionOf(boxes) == LineFill::Direction::RightToLeft;
            int place = int(row.size());
            for (int index = 0; index < row.size(); ++index) {
                const bool after = rightToLeft ? note.box.left() > row.at(index).box.left()
                                               : note.box.left() < row.at(index).box.left();
                if (after) {
                    place = index;
                    break;
                }
            }
            row.insert(place, note);
            // A note is not a word of any verse, so it takes the verse of what
            // it stands beside — which keeps the verses of this line contiguous.
            rowVerses.insert(
                place,
                rowVerses.isEmpty()
                    ? QString()
                    : rowVerses.at(std::clamp(place, 0, int(rowVerses.size()) - 1)));
        }

        poured += row;
        pouredVerses += rowVerses;
    }

    // One verse per verse of the source, so the folio comes out numbered the way
    // the published transcription is rather than as one undivided block.
    //
    // **And carrying the chapters with it.** The id says which chapter each
    // verse is in, and this used to take only the number off the end of it — so
    // a leaf running from Jas 1:25 into chapter 2 laid its verses down as 1, 2,
    // 3 *inside chapter 1*, and chapterOfVerse() agreed. The verse numbers on
    // screen were then wrong for every leaf after the first chapter break, and
    // so was every id built from them — including the one the next folio
    // resumes at.
    QList<TranscribedVerse> rebuilt;
    if (!before.isEmpty()) {
        TranscribedVerse kept;
        kept.words = before;
        rebuilt.append(kept);
    }
    // What the kept words above are in, so a break is written only where the
    // chapter really changes.
    int chapter = before.isEmpty() ? 0 : chapterOfVerse(*target, 0);
    for (int index = 0; index < poured.size(); ++index) {
        const QString id = index < pouredVerses.size() ? pouredVerses.at(index) : QString();
        const QString number = id.section(QLatin1Char('.'), 2, 2);
        const int itsChapter = id.section(QLatin1Char('.'), 1, 1).toInt();
        if (rebuilt.isEmpty() || (!before.isEmpty() && rebuilt.size() == 1)
            || rebuilt.last().number != number) {
            TranscribedVerse opened;
            opened.number = number;
            if (itsChapter > 0 && chapter > 0 && itsChapter != chapter) {
                opened.startsNewChapter = true;
            }
            rebuilt.append(opened);
        }
        if (itsChapter > 0) {
            chapter = itsChapter;
        }
        rebuilt.last().words.append(poured.at(index));
    }
    if (!after.isEmpty()) {
        TranscribedVerse rest;
        rest.words = after;
        rebuilt.append(rest);
    }
    target->verses = rebuilt;

    // Where the pour starts the leaf, the leaf opens in the chapter the pour
    // does. Where words are kept above it they carry the page's existing
    // chapter, and rewriting it under them would move them to a chapter they
    // were never in.
    if (before.isEmpty() && !verses.isEmpty()) {
        const int first = verses.first().section(QLatin1Char('.'), 1, 1).toInt();
        if (first > 0) {
            target->firstChapter = first;
        }
    }

    // Still written, because it records what this fill did and the message below
    // reports it — but no longer what the next folio trusts. That reads the leaf
    // itself, so a correction made afterwards moves the resume with it. See
    // resumeFill().
    // Where the pour began, so a re-flow can count its way from there. Only a
    // fill in its own right sets it: a re-flow starts partway down and must not
    // move the mark it counted from, or the next one would count from there.
    if (standalone) {
        target->fillStartLine = filled.first().index;
    }
    target->fillEndVerse = verses.isEmpty() ? QString() : verses.last();
    target->fillEndWord = 0;
    for (int index = verses.size() - 1;
         index >= 0 && verses.at(index) == target->fillEndVerse;
         --index) {
        ++target->fillEndWord;
    }
    // On the document rather than the folio: one transcription is filled from
    // one published edition, and the next leaf should not have to find it again.
    m_document.fillSource = sourcePath;

    ensureTypingRoom();
    m_selectedVerse = -1;
    m_selectedColumn = -1;
    setDirty(true);
    emit versesChanged();
    emit selectionChanged();

    setMessage(
        standalone
            ? QStringLiteral("Filled %1 word(s)%2. None has been checked — walk the "
                             "folio and confirm the lines. Ctrl+Z puts it back.")
                  .arg(poured.size())
                  .arg(range.isEmpty() ? QString()
                                       : QStringLiteral(" from %1").arg(range))
            : QStringLiteral("Laid the %1 word(s) below that line again. Ctrl+Z puts "
                             "it back.")
                  .arg(poured.size()));
}

void TranscriptionController::showTraining()
{
    HtrTrainingDialog dialog(&kraken(), m_dialogParent);
    dialog.exec();
    // A model may have arrived, which the Transcribe button's tooltip and the
    // model menus all read from settings.
    emit documentChanged();
}

void TranscriptionController::showLastRecognition()
{
    HtrLastRunDialog dialog(m_dialogParent);
    dialog.exec();
}

void TranscriptionController::manageModels()
{
    kraken().refresh();
    HtrModelsDialog dialog(&kraken(), m_dialogParent);
    dialog.exec();
    // So the Transcribe tooltip catches up with whichever model now runs.
    emit documentChanged();
}

void TranscriptionController::removeKraken()
{
    // Re-asked rather than trusted: the menu's answer is as old as the last
    // time anything looked, and what is being confirmed is a deletion.
    kraken().refresh();
    if (!krakenInstalled()) {
        setMessage(QStringLiteral("There is no Kraken installation to remove."));
        return;
    }

    const qint64 bytes = kraken().installedBytes();
    const QString size = bytes > 0
        ? QStringLiteral(" (about %1)")
              .arg(QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat))
        : QString();

    // Explicit about what survives, so nobody has to guess whether Milah has
    // just uninstalled their Ubuntu.
    const QString kept = KrakenEnvironment::usesSubsystem()
        ? QStringLiteral("<p>WSL2 and your Linux distribution are left exactly "
                         "as they are.</p>")
        : QString();

    if (QMessageBox::question(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Delete Kraken, its Python environment and its "
                           "models%1?%2")
                .arg(size, kept),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel)
        != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!kraken().remove(&error)) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Kraken could not be removed.\n%1").arg(error));
        return;
    }
    setMessage(bytes > 0
                   ? QStringLiteral("Kraken removed, freeing %1.")
                         .arg(QLocale().formattedDataSize(
                             bytes, 1, QLocale::DataSizeTraditionalFormat))
                   : QStringLiteral("Kraken removed."));
    emit documentChanged();
}

void TranscriptionController::transcribeFolio(QWidget *over, bool readingsOnly)
{
    // Whatever window is in front. The fill dialog passes itself, because
    // QDialog::exec() is application-modal and a progress dialog parented
    // behind it would never see a mouse.
    QWidget *const front = over ? over : m_dialogParent;
    const TranscribedPage *page = currentPage();
    if (!page) {
        setMessage(QStringLiteral("Open a folio before transcribing it."));
        return;
    }
    if (!ensureImageFetched()) {
        return;
    }
    const QByteArray bytes = currentImageBytes();
    if (bytes.isEmpty()) {
        setMessage(QStringLiteral("This folio has no picture to read."));
        return;
    }

    // Two things have to be true, and they are two different jobs: Kraken has
    // to be installed, and a model has to be chosen. Walked in that order, each
    // with its own dialog, because joining them is what made a second model
    // unreachable.
    KrakenEnvironment::State state = kraken().refresh();

    if (state == KrakenEnvironment::State::NoSubsystem
        || state == KrakenEnvironment::State::AwaitingRestart
        || state == KrakenEnvironment::State::NotInstalled) {
        HtrSetupDialog setup(&kraken(), front);
        setup.exec();
        state = kraken().refresh();
    }

    if (state == KrakenEnvironment::State::NoModel) {
        HtrModelsDialog models(&kraken(), front);
        models.exec();
        state = kraken().refresh();
    }

    if (state != KrakenEnvironment::State::Ready) {
        // Said rather than merely returned. This used to end in silence, so a
        // press of Transcribe could produce nothing whatever and leave nobody
        // any the wiser about which of the two pieces was missing.
        setMessage(KrakenEnvironment::describe(state));
        emit documentChanged();
        return;
    }
    emit documentChanged();

    // Kept, not temporary. A QTemporaryDir here meant that a run which produced
    // nothing left nothing to look at — no command, no output, no layout file —
    // and a feature that takes minutes and can end in silence has to be
    // answerable afterwards. One run's worth, replaced each time.
    const QDir workspace(KrakenEnvironment::lastRunDirectory());
    for (const QString &stale : workspace.entryList(QDir::Files)) {
        QFile::remove(workspace.filePath(stale));
    }

    // From the address rather than the label. A scan's imageName is what the
    // library calls the leaf — "104", or "Ebr. 530, f. 1r" — and asking
    // QFileInfo for the suffix of that gives "" or, worse, " 1r".
    //
    // This is not a tidying. An empty suffix used to make the name "folio.",
    // and Windows strips a trailing dot when it creates a file, so the bytes
    // landed in "folio" while Kraken was sent "/mnt/c/…/folio." — a path that
    // exists on neither side. Kraken spent a minute or two importing itself,
    // failed to find its input, and the whole silent minute was reported into
    // the status bar. That is the bug this whole run of repairs is about.
    QString suffix = QFileInfo(QUrl(page->imageUrl).path()).suffix();
    if (suffix.isEmpty()) {
        suffix = QFileInfo(page->imageName).suffix();
    }
    const QString imagePath = workspace.filePath(
        QStringLiteral("folio.%1").arg(suffix.isEmpty() ? QStringLiteral("png") : suffix));
    const QString altoPath = workspace.filePath(QStringLiteral("folio.xml"));

    QFile image(imagePath);
    if (!image.open(QIODevice::WriteOnly) || image.write(bytes) != bytes.size()) {
        QMessageBox::warning(
            front,
            QStringLiteral("Milah"),
            QStringLiteral("The folio could not be written out for the "
                           "recogniser.\n\n%1\n%2")
                .arg(imagePath, image.errorString()));
        return;
    }
    image.close();

    // Asked for by the name Kraken will be given, not by the handle that wrote
    // it. Windows will quietly file bytes under a name other than the one it
    // was handed — trailing dots and spaces are dropped — so "it opened and it
    // wrote" is not the same statement as "the recogniser can find it". Cheap,
    // and it turns a silent two-minute failure into a sentence.
    if (!QFileInfo::exists(imagePath)) {
        QMessageBox::warning(
            front,
            QStringLiteral("Milah"),
            QStringLiteral("The folio was written out, but not under the name "
                           "the recogniser would be given.\n\n%1")
                .arg(imagePath));
        return;
    }

    // The bytes written are the bytes Milah holds, so the coordinates that come
    // back are in the space of the image on screen and nothing has to be
    // guessed about how the recogniser saw it.
    QStringList command = kraken().recognitionCommand(imagePath, altoPath);
    const QString program = command.takeFirst();

    // Written before the run, so that even a Milah that dies mid-recognition
    // leaves the exact command behind.
    QFile record(workspace.filePath(QStringLiteral("command.txt")));
    if (record.open(QIODevice::WriteOnly)) {
        record.write(program.toUtf8());
        for (const QString &argument : command) {
            record.write("\n    ");
            record.write(argument.toUtf8());
        }
        record.write("\n");
        record.close();
    }

    QProgressDialog progress(
        QStringLiteral("Reading %1 with Kraken. On a processor this takes "
                       "minutes.")
            .arg(page->imageLabel.isEmpty() ? page->imageName : page->imageLabel),
        QStringLiteral("Cancel"),
        0,
        0,
        front);
    progress.setWindowTitle(QStringLiteral("Milah"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QProcess recogniser;
    recogniser.setProcessChannelMode(QProcess::MergedChannels);

    // A local event loop rather than a blocking wait: the window keeps
    // repainting and Cancel keeps working, which for something that runs for
    // minutes is the difference between waiting and having hung.
    QEventLoop loop;
    QString transcript;
    QObject::connect(&recogniser, &QProcess::readyRead, &loop, [&] {
        // kraken narrates its progress on stderr. Only the last line is worth
        // showing; the rest is what the log in the setup dialog is for.
        transcript += QString::fromUtf8(recogniser.readAll());
        const QString last =
            transcript.section(QLatin1Char('\n'), -2, -1).trimmed();
        if (!last.isEmpty()) {
            progress.setLabelText(last);
        }
    });
    QObject::connect(
        &recogniser,
        &QProcess::finished,
        &loop,
        [&loop](int, QProcess::ExitStatus) { loop.quit(); });
    // Milah's own flag rather than QProgressDialog::wasCanceled(), and only set
    // while there is something to cancel.
    //
    // The dialog emits canceled() on its way off the screen as well as when the
    // button is pressed — closeEvent() emits it — and Qt connects that signal
    // to the cancel() slot for you, which is what wasCanceled() reports. So
    // taking the dialog down at the end of a *successful* run set the flag, and
    // every reading that arrived whole was announced as cancelled and thrown
    // away three lines later.
    bool cancelled = false;
    QObject::connect(&progress, &QProgressDialog::canceled, &recogniser, [&] {
        if (recogniser.state() == QProcess::NotRunning) {
            return;
        }
        cancelled = true;
        recogniser.kill();
    });

    recogniser.start(program, command);
    if (!recogniser.waitForStarted(15000)) {
        QMessageBox::warning(
            front,
            QStringLiteral("Milah"),
            QStringLiteral("Kraken could not be started.\n\n%1\n%2")
                .arg(program, recogniser.errorString()));
        return;
    }
    // Entered only while there is something to wait for. waitForStarted() runs
    // an event loop of its own, so a process that fails instantly can finish
    // inside it — and the quit() would then be delivered to a loop that is not
    // running yet, leaving exec() to block for ever with the dialog on screen.
    if (recogniser.state() != QProcess::NotRunning) {
        loop.exec();
    }
    // reset(), not close(): the documented way to take a progress dialog down,
    // and the one that does not emit canceled(). See the flag above.
    progress.reset();

    // What it said, kept whatever happened next.
    QFile said(workspace.filePath(QStringLiteral("output.txt")));
    if (said.open(QIODevice::WriteOnly)) {
        said.write(transcript.toUtf8());
        said.write(QStringLiteral("\n\n--- exit code %1, status %2\n")
                       .arg(recogniser.exitCode())
                       .arg(recogniser.exitStatus() == QProcess::NormalExit
                                ? QStringLiteral("normal")
                                : QStringLiteral("crashed"))
                       .toUtf8());
        said.close();
    }

    if (cancelled) {
        // Nothing has been touched: the folio is written to only after a
        // reading has arrived whole.
        setMessage(QStringLiteral("Transcription cancelled. The folio is as it "
                                  "was."));
        return;
    }

    if (recogniser.exitStatus() != QProcess::NormalExit || recogniser.exitCode() != 0) {
        // One failure is worth telling apart from the rest, because it is not
        // about the folio at all: kraken's repository holds recognition models
        // for more than one program, and handed one of another program's it
        // stops at "No loader found" after a page of Python. Saying which
        // model, and what to do, beats reprinting the traceback.
        if (transcript.contains(QLatin1String("No loader found"))
            || transcript.contains(QLatin1String("not in model registry"))) {
            QMessageBox::warning(
                front,
                QStringLiteral("Milah"),
                QStringLiteral(
                    "Kraken cannot load the model it was given — it is a model "
                    "for a different program, not for Kraken.<p>Choose another "
                    "under the arrow beside Transcribe, or in File ▸ "
                    "Handwriting recognition ▸ Install Kraken…. One marked "
                    "<b>Dedicated</b> for your script is the one to want.</p>"));
            return;
        }
        QMessageBox::warning(
            front,
            QStringLiteral("Milah"),
            QStringLiteral("Kraken could not read this folio.\n\n%1")
                .arg(transcript.trimmed().isEmpty()
                         ? QStringLiteral("It said nothing about why.")
                         : transcript.trimmed()));
        return;
    }

    QFile alto(altoPath);
    if (!alto.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            front,
            QStringLiteral("Milah"),
            QStringLiteral("Kraken finished but wrote no layout file.\n\n%1\n%2")
                .arg(altoPath, alto.errorString()));
        return;
    }

    QString error;
    const RecognisedPage recognised = parseRecognisedPage(alto.readAll(), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(front, QStringLiteral("Milah"), error);
        return;
    }

    if (readingsOnly) {
        harvestReadings(recognised);
        return;
    }
    applyRecognition(recognised, QStringLiteral("Kraken"));
}

void TranscriptionController::recoverReadings()
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        setMessage(QStringLiteral("Open a folio first."));
        return;
    }
    int missing = 0;
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (!word.box.isNull() && word.recognised.isEmpty()) {
                ++missing;
            }
        }
    }
    if (missing == 0) {
        setMessage(QStringLiteral("Every box on this folio already carries what the "
                                  "machine read there."));
        return;
    }
    // Read again, but only to fill those in. Transcribe would replace the text
    // outright, which on a folio already filled from a transcription would throw
    // that away to recover something smaller.
    transcribeFolio(nullptr, true);
}

void TranscriptionController::harvestReadings(const RecognisedPage &recognised)
{
    TranscribedPage *page = mutablePage();
    if (!page) {
        return;
    }

    // The same conversion applyRecognition does, for the same reason: the
    // reading may have been made from a differently sized copy of the folio.
    const QSize folio = folioPixelSize();
    double scaleX = 1.0;
    double scaleY = 1.0;
    if (folio.isValid() && recognised.imageSize.isValid() && folio != recognised.imageSize) {
        scaleX = double(folio.width()) / recognised.imageSize.width();
        scaleY = double(folio.height()) / recognised.imageSize.height();
    }

    QList<QPair<QRect, QString>> read;
    read.reserve(recognised.words.size());
    for (const RecognisedWord &word : recognised.words) {
        if (word.box.isNull() || word.text.isEmpty()) {
            continue;
        }
        read.append({QRect(qRound(word.box.x() * scaleX),
                           qRound(word.box.y() * scaleY),
                           qRound(word.box.width() * scaleX),
                           qRound(word.box.height() * scaleY)),
                     word.text});
    }

    pushUndo();
    int filled = 0;
    for (TranscribedVerse &verse : page->verses) {
        for (TranscribedWord &word : verse.words) {
            if (word.box.isNull() || !word.recognised.isEmpty()) {
                continue;
            }
            // By where it sits rather than by an exact rectangle: the same model
            // over the same picture draws the same boxes, but a rounding away is
            // not worth losing a reading over.
            for (const QPair<QRect, QString> &candidate : read) {
                if (candidate.first.contains(word.box.center())
                    || word.box.contains(candidate.first.center())) {
                    word.recognised = candidate.second;
                    ++filled;
                    break;
                }
            }
        }
    }

    // The line geometry too, which a folio read before Milah kept it also lacks
    // — and it is what the training strips are cut from.
    if (page->lines.isEmpty()) {
        for (const RecognisedLine &line : recognised.lines) {
            TranscribedLine kept;
            kept.index = line.index;
            for (const QPoint &point : line.baseline) {
                kept.baseline.append(
                    QPoint(qRound(point.x() * scaleX), qRound(point.y() * scaleY)));
            }
            for (const QPoint &point : line.boundary) {
                kept.boundary.append(
                    QPoint(qRound(point.x() * scaleX), qRound(point.y() * scaleY)));
            }
            page->lines.append(kept);
        }
    }

    setDirty(true);
    emit versesChanged();
    setMessage(QStringLiteral("Recovered what the machine read at %1 box(es). The "
                              "text on the folio is unchanged.")
                   .arg(filled));
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
    work.descriptions.insert(
        QStringLiteral("x-folios"), WorkDescription{m_document.metadata.folios, {}});
    work.descriptions.insert(
        QStringLiteral("x-material"), WorkDescription{m_document.metadata.material, {}});
    work.descriptions.insert(
        QStringLiteral("x-provenance"),
        WorkDescription{m_document.metadata.provenance, {}});
    // The only one of these carrying a verdict as well as an answer: what the
    // Hebrew renders is a different question from whether that is settled, and
    // for several of these manuscripts the second is the whole argument.
    work.descriptions.insert(
        QStringLiteral("x-translated-from"),
        WorkDescription{
            m_document.metadata.translatedFrom,
            translationSubType(m_document.metadata.translatedFromCertainty)});
    work.descriptions.insert(
        QStringLiteral("x-exemplar"), WorkDescription{m_document.metadata.exemplar, {}});

    return work;
}

TranscriptionController::Exportable TranscriptionController::exportable() const
{
    // The transcription is shaped into the same drafts the edition exports, so
    // the OSIS a folio produces and the OSIS an edition produces are written by
    // one function and cannot drift apart.
    Exportable out;

    // The marginalia come out of the running text and go in as notes on the
    // lines they stand beside: they are on the leaf, but they are not words of
    // any verse of the work.
    const TranscriptionDocument document = withoutMarginalia(m_document);
    for (const TranscribedPage &page : document.pages) {
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
    // The header still comes from the Manuscript panel; only the filename is
    // the book and the manuscript.
    const QString suggested =
        QStringLiteral("%1.osis").arg(transcriptionFileStem(m_document, m_currentPage));
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

    // The same projection the OSIS export goes through: marginalia are notes on
    // the lines they stand beside, not words of the running text.
    const TranscriptionDocument document = withoutMarginalia(m_document);

    const DocxDocument reading = readingWordDocument(document);
    if (reading.blocks.isEmpty()) {
        QMessageBox::warning(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral("Nothing has been read off this manuscript yet, so "
                           "there is nothing to put in a document."));
        return;
    }

    const QString suggested =
        QStringLiteral("%1.docx").arg(transcriptionFileStem(m_document, m_currentPage));
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
    if (!writeDocx(interlinearPath, interlinearWordDocument(document), &error)) {
        QMessageBox::warning(m_dialogParent, QStringLiteral("Milah"), error);
        setMessage(error);
        return;
    }

    QSettings().setValue(QStringLiteral("paths/lastDirectory"), chosen.absolutePath());

    const int unnamed = unnamedVerseCount(document);
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

    // Before the comparison below, and on purpose. The grid commits a cell when
    // the caret leaves it, whether or not anything was typed — so this is the
    // moment a transcriber has finished looking at this word, which is what
    // checking a machine's reading of it consists of. Requiring an edit would
    // mean a word the recogniser got right stayed unchecked for ever, and the
    // count would never reach zero for the folios that went best.
    //
    // No pushUndo: having read a word is not a change to undo, and a stack full
    // of them would bury the edits that are.
    if (word.unchecked) {
        word.unchecked = false;
        setDirty(true);
        emit wordChecked(verse, column);
    }

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

void TranscriptionController::setLineBreak(int verse, int column, bool endsLine)
{
    if (!isValid(verse, column)) {
        return;
    }
    TranscribedPage *page = mutablePage();
    TranscribedWord &word = page->verses[verse].words[column];
    if (word.endsLine == endsLine) {
        return;
    }
    pushUndo();
    word.endsLine = endsLine;
    setDirty(true);
    // Nothing on screen shows a break — the grid's bands are Milah's own
    // wrapping and have nothing to do with the folio's lines, so drawing one
    // there would say something untrue. What changes is the count of finished
    // lines, which the Save this folio entry reads when its menu opens.
    emit versesChanged();
}

void TranscriptionController::setMarginal(int verse, int column, bool marginal)
{
    if (!isValid(verse, column)) {
        return;
    }
    TranscribedPage *page = mutablePage();
    TranscribedWord &word = page->verses[verse].words[column];
    if (word.marginal == marginal) {
        return;
    }
    const int line = word.line;
    // Attempted whenever this transcription was filled from somewhere and the
    // box is on a line at or below where the pour began. A folio with no
    // recorded start line is attempted too, and reflowFrom() says why it cannot
    // — silence there was the whole of the last complaint.
    const bool poured = !m_document.fillSource.isEmpty() && line >= 0
        && (page->fillStartLine < 0 || line >= page->fillStartLine);

    // Asked before anything is done, because the answer decides whether to do
    // any of it — and because a folio somebody has walked through should not
    // lose that work to a menu tick.
    if (poured && hasCheckedWordsFrom(line)) {
        const auto answer = QMessageBox::question(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral(
                "Holding this box out moves every word below it up one place, so "
                "the lines from here down are laid again from the transcription — "
                "and some of them you have already checked.<p>Your readings on "
                "those lines will be replaced. Ctrl+Z puts everything back.</p>"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    // One step for the whole of it: the mark, the reading that comes back, and
    // the re-flow. Ctrl+Z is one press because it was one decision.
    pushUndo();
    word.marginal = marginal;
    if (marginal && !word.recognised.isEmpty()) {
        // The word of the work that was poured here belongs further down the
        // passage; what belongs here is what the machine read.
        word.hebrew = word.recognised;
        word.english = suggestedGloss(word.hebrew);
        word.englishIsOwn = false;
        word.unchecked = true;
    }
    setDirty(true);

    if (poured) {
        // Laid again from this line to the foot of the leaf, so the word taken
        // off the note is given back to the passage. Everything above is left
        // exactly as it is, corrections included.
        reflowFrom(line);
    }

    // The overlay draws a held-out box differently, and the fill counts one line
    // shorter, so both have to hear about it.
    emit versesChanged();
}

bool TranscriptionController::hasCheckedWordsFrom(int line) const
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return false;
    }
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line >= line && !word.marginal && !word.unchecked
                && !word.hebrew.isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

void TranscriptionController::reflowFrom(int line)
{
    const TranscribedPage *page = currentPage();
    if (!page) {
        return;
    }

    QFile file(m_document.fillSource);
    if (!file.open(QIODevice::ReadOnly)) {
        // The edition has moved since the fill. The mark stands — it is a
        // statement about the folio — but nothing can be laid again without it.
        setMessage(QStringLiteral("%1 could not be re-opened, so the text below "
                                  "was left as it is.")
                       .arg(QFileInfo(m_document.fillSource).fileName()));
        return;
    }
    SourceDocument source;
    try {
        source = parseOsis(QString::fromUtf8(file.readAll()), ParseOptions{});
    } catch (const OsisError &failure) {
        setMessage(QStringLiteral("That stands, but %1 could not be read, so the "
                                  "text below was left as it is. %2")
                       .arg(QFileInfo(m_document.fillSource).fileName(),
                            failure.message()));
        return;
    }

    // This folio's own record first. The leaf before it is only a fallback, and
    // only ever answered for a continuation — the first filled folio of a
    // document has nothing behind it, which is why this used to do nothing at
    // all and say nothing about it.
    QString startVerse = page->fillStartVerse;
    int startWord = page->fillStartWord;
    if (startVerse.isEmpty() || startWord < 0) {
        const ResumePoint resume = resumePoint();
        startVerse = resume.verse;
        startWord = resume.word;
    }
    if (startVerse.isEmpty() || startWord < 0) {
        setMessage(QStringLiteral(
            "This folio was filled before Milah recorded where its text began, so "
            "the words below were not laid again — fill it again to re-flow them."));
        return;
    }

    // Where this folio began, plus everything already standing above the line
    // being laid again.
    const Passage passage = gatherPassage(
        source,
        startVerse.section(QLatin1Char('.'), 0, 0),
        startVerse.section(QLatin1Char('.'), 1, 1).toInt(),
        startVerse.section(QLatin1Char('.'), 2, 2).toInt(),
        startWord + pouredWordsBefore(*page, line));
    if (passage.isEmpty()) {
        setMessage(QStringLiteral("The transcription has no more text after this "
                                  "point, so nothing was laid again."));
        return;
    }

    const QMap<int, QList<QRect>> lines = fillableLines(*page);
    // The box count, or what the transcriber has said the line holds instead.
    // A re-flow rebuilding these from the boxes is what used to throw that
    // answer away every time a box was held out somewhere above it.
    const QMap<int, int> held = wordCounts(*page);
    QList<int> indices;
    QList<int> counts;
    for (auto entry = lines.constBegin(); entry != lines.constEnd(); ++entry) {
        if (entry.key() < line) {
            continue;
        }
        indices.append(entry.key());
        counts.append(held.value(entry.key(), int(entry.value().size())));
    }
    if (indices.isEmpty()) {
        // The marked box was the only thing left on the last line of the folio.
        setMessage(QStringLiteral("There is nothing below that on this folio to "
                                  "lay again."));
        return;
    }

    QList<FilledLine> filled;
    QStringList wordVerses;
    for (const LineFill::Laid &laid :
         LineFill::layOut(counts, 0, 0, int(passage.words.size()))) {
        FilledLine row;
        row.index = indices.at(laid.line);
        row.words = passage.words.mid(laid.from, laid.count);
        row.boxes = LineFill::place(lines.value(row.index), laid.count);
        if (row.boxes.size() != row.words.size()) {
            continue;
        }
        wordVerses += passage.verses.mid(laid.from, laid.count);
        filled.append(row);
    }
    if (filled.isEmpty()) {
        setMessage(QStringLiteral("That stands, but nothing could be laid into the "
                                  "lines below it."));
        return;
    }

    // Through the same path a fill goes through, so the two cannot come out
    // differently. It pushes no undo of its own — this is one step.
    applyFill(filled, wordVerses, m_document.fillSource, QString(), false);
}

bool TranscriptionController::wordAt(const QRect &box, int *verse, int *column) const
{
    const TranscribedPage *page = currentPage();
    if (!page || box.isNull()) {
        return false;
    }
    // By its box, because the callers are the folio's own right-click and what
    // it has is a place on the picture. A recogniser's boxes are distinct, so a
    // box names a word.
    for (int index = 0; index < page->verses.size(); ++index) {
        const QList<TranscribedWord> &words = page->verses.at(index).words;
        for (int at = 0; at < words.size(); ++at) {
            if (words.at(at).box == box) {
                *verse = index;
                *column = at;
                return true;
            }
        }
    }
    return false;
}

bool TranscriptionController::setMarginalAt(const QRect &box, bool marginal)
{
    int verse = -1;
    int column = -1;
    if (!wordAt(box, &verse, &column)) {
        return false;
    }
    setMarginal(verse, column, marginal);
    return true;
}

bool TranscriptionController::setLineBreakAt(const QRect &box, bool endsLine)
{
    int verse = -1;
    int column = -1;
    if (!wordAt(box, &verse, &column)) {
        return false;
    }
    // The same path the text grid commits by. No re-flow: a break says what the
    // manuscript's line is, not where the words went, and every word is already
    // standing on the box it was poured onto. What it changes is the strip the
    // training export cuts, which is the whole reason for saying it.
    setLineBreak(verse, column, endsLine);
    return true;
}

bool TranscriptionController::setLineMarginal(int line, bool marginal)
{
    TranscribedPage *page = mutablePage();
    if (!page || line < 0) {
        return false;
    }

    bool any = false;
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line == line && !word.box.isNull() && word.marginal != marginal) {
                any = true;
                break;
            }
        }
        if (any) {
            break;
        }
    }
    if (!any) {
        return false;
    }

    // Same reasoning as setMarginal(), which this is the whole-line form of —
    // one undo step, one warning and one re-flow for what was one decision,
    // rather than nine of each for a nine-word margin note.
    const bool poured = !m_document.fillSource.isEmpty()
        && (page->fillStartLine < 0 || line >= page->fillStartLine);
    if (poured && hasCheckedWordsFrom(line)) {
        const auto answer = QMessageBox::question(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral(
                "Holding this whole line out moves every word below it up, so the "
                "lines from here down are laid again from the transcription — and "
                "some of them you have already checked.<p>Your readings on those "
                "lines will be replaced. Ctrl+Z puts everything back.</p>"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    pushUndo();
    for (TranscribedVerse &verse : page->verses) {
        for (TranscribedWord &word : verse.words) {
            if (word.line != line || word.box.isNull() || word.marginal == marginal) {
                continue;
            }
            word.marginal = marginal;
            if (marginal && !word.recognised.isEmpty()) {
                word.hebrew = word.recognised;
                word.english = suggestedGloss(word.hebrew);
                word.englishIsOwn = false;
                word.unchecked = true;
            }
        }
    }
    setDirty(true);

    if (poured) {
        reflowFrom(line);
    }
    emit versesChanged();
    return true;
}

bool TranscriptionController::joinLineAt(int line)
{
    TranscribedPage *page = mutablePage();
    if (!page) {
        return false;
    }

    // Asked before the undo step is pushed, because there is no popping one
    // back off. The menu only offers this where there is a line below, so
    // getting a no here means the folio changed under an open menu.
    bool here = false;
    bool below = false;
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.box.isNull()) {
                continue;
            }
            if (word.line == line) {
                here = true;
            } else if (word.line > line) {
                below = true;
            }
        }
    }
    if (!here || !below) {
        return false;
    }

    // A join changes how many boxes the line has, so the passage from here down
    // is laid again — which is the point of it: two side-by-side pieces of one
    // manuscript line only fall into a single right-to-left run once
    // LineFill::directionOf() sees their boxes together.
    const bool poured = !m_document.fillSource.isEmpty()
        && (page->fillStartLine < 0 || line >= page->fillStartLine);
    if (poured && hasCheckedWordsFrom(line)) {
        const auto answer = QMessageBox::question(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral(
                "Joining these lines lays the transcription out across them "
                "together, so the lines from here down are filled again — and "
                "some of them you have already checked.<p>Your readings on those "
                "lines will be replaced. Ctrl+Z puts everything back.</p>"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    pushUndo();
    joinLine(*page, line);
    setDirty(true);

    if (poured) {
        reflowFrom(line);
    }
    emit versesChanged();
    return true;
}

namespace {

/// Where `box` sits among the words a fill may lay into on its own line, or -1
/// when it is not one of them.
///
/// The same rule fillableLines() uses, applied to one word: marginalia are not
/// slots, so a note beside the text does not count towards a line's length and a
/// break asked for on one means nothing.
int placeOnLine(const TranscribedPage &page, const QRect &box, int *line)
{
    int found = -1;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.box == box && word.line >= 0 && !word.marginal) {
                found = word.line;
                break;
            }
        }
        if (found >= 0) {
            break;
        }
    }
    if (found < 0) {
        return -1;
    }
    if (line) {
        *line = found;
    }
    int at = 0;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line != found || word.box.isNull() || word.marginal) {
                continue;
            }
            if (word.box == box) {
                return at;
            }
            ++at;
        }
    }
    return -1;
}

/// How many words of the poured text stand on `line` and everything below it.
/// The difference across a re-flow is what fell off the foot of the leaf.
int pouredFrom(const TranscribedPage &page, int line)
{
    int words = 0;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line >= line && !word.marginal && !word.box.isNull()) {
                ++words;
            }
        }
    }
    return words;
}

} // namespace

bool TranscriptionController::markLineChecked(int line)
{
    TranscribedPage *page = mutablePage();
    if (!page) {
        return false;
    }
    const int vouched = vouchForLine(*page, line);
    if (vouched == 0) {
        return false;
    }
    setDirty(true);

    // No pushUndo, for the same reason setWord() gives: having read a word is
    // not a change to undo, and a stack full of them would bury the edits that
    // are. This is that decision made once for a line instead of once a word.
    //
    // versesChanged rather than wordChecked, which is the opposite of what the
    // grid wants while somebody is typing in it. wordChecked exists so that a
    // caret leaving a cell does not rebuild the grid underneath the Tab key —
    // but nobody is in the grid here, the folio has the focus, and a whole line
    // of cells has just changed.
    emit versesChanged();

    // What still stands between this line and the training set. Marginalia are
    // the usual answer and are not what the key just vouched for.
    int left = 0;
    for (const TranscribedVerse &verse : page->verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line == line && !word.box.isNull() && !word.hebrew.isEmpty()
                && word.unchecked) {
                ++left;
            }
        }
    }
    setMessage(
        left > 0
            ? QStringLiteral("Line %1 read. %2 word(s) on it are still marginal "
                             "notes nobody has transcribed, so it will be trimmed "
                             "to what is vouched for.")
                  .arg(line + 1)
                  .arg(left)
            : QStringLiteral("Line %1 read — %2 word(s), and it now counts towards "
                             "the training set.")
                  .arg(line + 1)
                  .arg(vouched));
    return true;
}

bool TranscriptionController::breakLineBefore(const QRect &box)
{
    TranscribedPage *page = mutablePage();
    if (!page) {
        return false;
    }
    int line = -1;
    const int at = placeOnLine(*page, box, &line);
    if (at < 0) {
        setMessage(QStringLiteral(
            "That box is not one a transcription is laid into, so there is no "
            "line length to correct."));
        return false;
    }
    if (page->lineWords.value(line, -1) == at) {
        return false;
    }

    // Same warning as every other re-flow: the lines below are laid again from
    // the transcription, and anything already checked down there is replaced.
    if (hasCheckedWordsFrom(line)) {
        const auto answer = QMessageBox::question(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral(
                "Ending this line here moves the rest of it down, so the lines "
                "from here to the foot of the leaf are laid again from the "
                "transcription — and some of them you have already checked."
                "<p>Your readings on those lines will be replaced. Ctrl+Z puts "
                "everything back.</p>"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    const int before = pouredFrom(*page, line);
    pushUndo();
    page->lineWords.insert(line, at);
    setDirty(true);
    reflowFrom(line);
    emit versesChanged();

    // What no longer fits. The words are not lost — the folio's recorded end
    // point moves back with them, so the next leaf's continuation begins exactly
    // there — but nobody would guess that from a leaf quietly getting shorter.
    const TranscribedPage *after = currentPage();
    const int lost = after ? before - pouredFrom(*after, line) : 0;
    if (lost > 0) {
        setMessage(QStringLiteral("%1 word(s) moved past the foot of the leaf. "
                                  "Continue the next folio to lay them down.")
                       .arg(lost));
    }
    return true;
}

bool TranscriptionController::pullWordUp(const QRect &box)
{
    TranscribedPage *page = mutablePage();
    if (!page) {
        return false;
    }
    int line = -1;
    const int at = placeOnLine(*page, box, &line);
    if (at < 0) {
        return false;
    }
    if (at != 0) {
        setMessage(QStringLiteral(
            "Only the first word of a line can be pulled up onto the line "
            "above. Select it and press Backspace again."));
        return false;
    }

    // The line above as the folio has them, which after a join is not always
    // this one's number less one.
    const QMap<int, int> counts = wordCounts(*page);
    int above = -1;
    for (auto entry = counts.constBegin(); entry != counts.constEnd(); ++entry) {
        if (entry.key() < line && (above < 0 || entry.key() > above)) {
            above = entry.key();
        }
    }
    if (above < 0) {
        setMessage(QStringLiteral(
            "There is no line above this one on the folio to pull the word up "
            "onto."));
        return false;
    }

    if (hasCheckedWordsFrom(above)) {
        const auto answer = QMessageBox::question(
            m_dialogParent,
            QStringLiteral("Milah"),
            QStringLiteral(
                "Pulling this word up lengthens the line above it, so the lines "
                "from there down are laid again from the transcription — and "
                "some of them you have already checked.<p>Your readings on those "
                "lines will be replaced. Ctrl+Z puts everything back.</p>"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    pushUndo();
    page->lineWords.insert(above, counts.value(above) + 1);
    setDirty(true);
    reflowFrom(above);
    emit versesChanged();
    return true;
}

QString TranscriptionController::wordTextAt(const QRect &box) const
{
    int verse = -1;
    int column = -1;
    if (!wordAt(box, &verse, &column)) {
        return QString();
    }
    return currentPage()->verses.at(verse).words.at(column).hebrew;
}

bool TranscriptionController::setWordAt(const QRect &box, const QString &hebrew)
{
    int verse = -1;
    int column = -1;
    if (!wordAt(box, &verse, &column)) {
        return false;
    }
    // The same path the grid commits by, so a word corrected on the picture and
    // one corrected in the text cannot come to mean different things — including
    // that editing it is what counts as checking it.
    setWord(verse, column, hebrew);
    return true;
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

void TranscriptionController::removeWord(int verse, int column)
{
    if (!isValid(verse, column)) {
        return;
    }
    pushUndo();
    mutablePage()->verses[verse].words.removeAt(column);
    // Which puts an empty cell back when that was the last word, so the verse
    // still has somewhere to type rather than becoming unreachable.
    ensureTypingRoom();
    // The word the caret was in has gone and every column after it has moved
    // down one, so the remembered position no longer names what it named.
    m_selectedVerse = -1;
    m_selectedColumn = -1;
    setDirty(true);
    emit versesChanged();
    emit selectionChanged();
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
