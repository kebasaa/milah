#include "ui/download_manuscripts_dialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Where the manuscripts are published. One constant, so pointing Milah
/// somewhere else is one edit.
constexpr auto kDefaultBaseUrl =
    "https://raw.githubusercontent.com/kebasaa/nt_hebrew_manuscripts/main/";
constexpr auto kManifestName = "manifest.json";
/// The texts sit in a folder of their own within the repository.
constexpr auto kDataFolder = "data/";

/// A manifest that has grown far beyond a catalogue is not one, and reading an
/// unbounded reply from the network into memory is how that becomes a problem.
constexpr qint64 kManifestSizeLimit = 1 * 1024 * 1024;

/// How long a request may make no progress before it is given up on.
///
/// Generous on purpose. Where a network advertises an IPv6 route that does not
/// carry traffic, the first connection spends roughly twenty seconds per
/// address before falling back to one that works — a minute is normal there and
/// it does eventually succeed. Cutting it short would turn a slow success into
/// a failure, which is the worse of the two.
constexpr int kTransferTimeoutMs = 120 * 1000;
/// After this long with nothing to show, say so rather than look frozen.
constexpr int kWaitingNoticeMs = 6 * 1000;
/// No published text is anywhere near this; the largest is under a quarter of a
/// megabyte.
constexpr qint64 kFileSizeLimit = 32 * 1024 * 1024;

QString humanSize(qint64 bytes)
{
    if (bytes <= 0) {
        return QStringLiteral("unknown size");
    }
    if (bytes < 1024) {
        return QStringLiteral("%1 bytes").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg((bytes + 512) / 1024);
    }
    return QStringLiteral("%1.%2 MB")
        .arg(bytes / (1024 * 1024))
        .arg((bytes % (1024 * 1024)) * 10 / (1024 * 1024));
}

} // namespace

QString DownloadManuscriptsDialog::baseUrl()
{
    const QString override = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("MILAH_MANUSCRIPT_URL"));
    if (!override.isEmpty()) {
        return override.endsWith(QLatin1Char('/')) ? override
                                                   : override + QLatin1Char('/');
    }
    return QString::fromLatin1(kDefaultBaseUrl);
}

DownloadManuscriptsDialog::DownloadManuscriptsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Download manuscripts"));
    setObjectName(QStringLiteral("downloadManuscriptsDialog"));

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("manuscriptCatalogue"));
    m_tree->setHeaderLabels(
        {QStringLiteral("Manuscript"), QStringLiteral("Covers"), QStringLiteral("Size")});
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    // The texts are Latin-titled and the sizes are numbers, so this one panel
    // reads left to right whatever the manuscripts inside it do.
    m_tree->setLayoutDirection(Qt::LeftToRight);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this] { updateDownloadButton(); });

    m_progress = new QProgressBar;
    m_progress->setVisible(false);

    m_status = new QLabel(QStringLiteral("Reading the catalogue…"));
    m_status->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    m_download = buttons->addButton(
        QStringLiteral("Download"), QDialogButtonBox::AcceptRole);
    m_download->setEnabled(false);
    // Nothing is fetched until this is pressed, and nothing closes the window
    // behind the editor's back while a download is running.
    connect(m_download, &QPushButton::clicked, this, [this] {
        if (!m_busy) {
            startNextDownload();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);
    layout->addWidget(buttons);

    resize(760, 520);

    m_network = new QNetworkAccessManager(this);
    fetchCatalogue();
}

void DownloadManuscriptsDialog::fetchCatalogue()
{
    const QUrl url(baseUrl() + QString::fromLatin1(kManifestName));
    if (!url.isValid()) {
        report(QStringLiteral("The catalogue address is not a valid URL."), true);
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(kTransferTimeoutMs);

    startWaitingNotice();
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        stopWaitingNotice();
        if (reply->error() != QNetworkReply::NoError) {
            report(QStringLiteral("Could not read the catalogue: %1")
                       .arg(reply->errorString()),
                   true);
            return;
        }
        const QByteArray body = reply->read(kManifestSizeLimit);
        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (!document.isObject()) {
            report(QStringLiteral("The catalogue is not readable."), true);
            return;
        }
        m_catalogue = ManuscriptCatalogue::fromJson(document.object());
        showCatalogue();
    });
}

void DownloadManuscriptsDialog::showCatalogue()
{
    if (m_catalogue.isEmpty()) {
        report(QStringLiteral("The catalogue lists no manuscripts."), true);
        return;
    }

    const QString library = manuscriptWriteDirectory();
    const QStringList held = m_catalogue.installedFiles(library);

    // Grouped by the book they belong to, so a witness and its translation sit
    // together and the two Revelation manuscripts can be told apart by title.
    QHash<QString, QTreeWidgetItem *> books;
    for (const CatalogueEntry &entry : m_catalogue.entries()) {
        QTreeWidgetItem *book = books.value(entry.book);
        if (!book) {
            book = new QTreeWidgetItem(m_tree, {entry.book});
            book->setFirstColumnSpanned(true);
            book->setExpanded(true);
            // A heading is not a thing to download.
            book->setFlags(book->flags() & ~Qt::ItemIsUserCheckable);
            books.insert(entry.book, book);
        }

        const bool alreadyHeld = held.contains(entry.file);
        auto *row = new QTreeWidgetItem(book);
        row->setText(0,
                     entry.isTranslation()
                         ? QStringLiteral("%1  (translation)").arg(entry.title)
                         : entry.title);
        row->setText(1, entry.covers);
        row->setText(2, alreadyHeld ? QStringLiteral("held") : humanSize(entry.bytes));
        row->setToolTip(0,
                        entry.date.isEmpty()
                            ? entry.file
                            : QStringLiteral("%1\n%2").arg(entry.date, entry.file));
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        // Already held is left unchecked: downloading again would only spend
        // somebody's connection to arrive at the same file.
        row->setCheckState(0, Qt::Unchecked);
        row->setDisabled(alreadyHeld);
        row->setData(0, Qt::UserRole, entry.file);
    }

    m_tree->resizeColumnToContents(0);
    report(QStringLiteral("%1 manuscripts available. Choose what to download.")
               .arg(m_catalogue.entries().size()));
    updateDownloadButton();
}

void DownloadManuscriptsDialog::updateDownloadButton()
{
    m_queue.clear();
    qint64 total = 0;

    for (int book = 0; book < m_tree->topLevelItemCount(); ++book) {
        QTreeWidgetItem *heading = m_tree->topLevelItem(book);
        for (int index = 0; index < heading->childCount(); ++index) {
            QTreeWidgetItem *row = heading->child(index);
            if (row->checkState(0) != Qt::Checked || row->isDisabled()) {
                continue;
            }
            const QString file = row->data(0, Qt::UserRole).toString();
            for (const CatalogueEntry &entry : m_catalogue.entries()) {
                if (entry.file == file) {
                    m_queue.append(entry);
                    total += entry.bytes;
                    break;
                }
            }
        }
    }

    m_download->setEnabled(!m_queue.isEmpty() && !m_busy);
    // Say what pressing it will do, before it is pressed.
    m_download->setText(m_queue.isEmpty()
                            ? QStringLiteral("Download")
                            : QStringLiteral("Download %1 %2 (%3)")
                                  .arg(m_queue.size())
                                  .arg(m_queue.size() == 1 ? QStringLiteral("file")
                                                           : QStringLiteral("files"),
                                       humanSize(total)));
}

void DownloadManuscriptsDialog::startNextDownload()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        m_progress->setVisible(false);
        report(m_downloaded == 1
                   ? QStringLiteral("One manuscript downloaded.")
                   : QStringLiteral("%1 manuscripts downloaded.").arg(m_downloaded));
        // Re-read what is held so the list stops offering what has arrived.
        m_tree->clear();
        showCatalogue();
        return;
    }

    if (!m_busy) {
        m_busy = true;
        m_queued = m_queue.size();
        m_progress->setVisible(true);
        m_progress->setRange(0, m_queued);
        m_download->setEnabled(false);
    }
    m_progress->setValue(m_queued - m_queue.size());

    const CatalogueEntry entry = m_queue.first();
    report(QStringLiteral("Downloading %1…").arg(entry.title));

    const QUrl url(baseUrl() + QString::fromLatin1(kDataFolder) + entry.file);
    QNetworkRequest request(url);
    // A redirect that drops to plain HTTP, or wanders to another host, is not
    // followed: the editor asked for one place and should get it or nothing.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    // The connection is already open by now, so this only guards against a
    // transfer that stalls part way rather than against the slow first reach.
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        finishDownload(reply);
    });
}

void DownloadManuscriptsDialog::finishDownload(QNetworkReply *reply)
{
    reply->deleteLater();
    const CatalogueEntry entry = m_queue.takeFirst();

    if (reply->error() != QNetworkReply::NoError) {
        m_busy = false;
        m_queue.clear();
        m_progress->setVisible(false);
        report(QStringLiteral("Could not download %1: %2")
                   .arg(entry.title, reply->errorString()),
               true);
        updateDownloadButton();
        return;
    }

    const QByteArray body = reply->read(kFileSizeLimit);
    if (body.isEmpty()) {
        m_busy = false;
        m_queue.clear();
        m_progress->setVisible(false);
        report(QStringLiteral("%1 arrived empty; nothing was written.").arg(entry.title),
               true);
        updateDownloadButton();
        return;
    }

    const QString directory = manuscriptWriteDirectory();
    QDir().mkpath(directory);

    // Through a QSaveFile so an interrupted download leaves nothing behind: a
    // half-written manuscript in the library would be worse than none, because
    // it would look like one.
    QSaveFile file(QDir(directory).filePath(entry.file));
    if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size()
        || !file.commit()) {
        m_busy = false;
        m_queue.clear();
        m_progress->setVisible(false);
        report(QStringLiteral("Could not save %1 into your library.").arg(entry.title),
               true);
        updateDownloadButton();
        return;
    }

    ++m_downloaded;
    startNextDownload();
}

void DownloadManuscriptsDialog::startWaitingNotice()
{
    m_waited.start();
    m_progress->setVisible(true);
    // Indeterminate: there is nothing to measure until the host answers, and a
    // bar sitting at zero reads as broken rather than as waiting.
    m_progress->setRange(0, 0);

    if (!m_waiting) {
        m_waiting = new QTimer(this);
        m_waiting->setInterval(1000);
        connect(m_waiting, &QTimer::timeout, this, [this] {
            const int seconds = int(m_waited.elapsed() / 1000);
            if (seconds * 1000 < kWaitingNoticeMs) {
                return;
            }
            // Said plainly, because the honest answer is that it is the network
            // and not Milah, and the editor can otherwise only conclude that
            // the application has hung.
            report(QStringLiteral(
                       "Still waiting for the catalogue (%1s). The first "
                       "connection is slow on some networks; later downloads "
                       "will be quick.")
                       .arg(seconds));
        });
    }
    m_waiting->start();
}

void DownloadManuscriptsDialog::stopWaitingNotice()
{
    if (m_waiting) {
        m_waiting->stop();
    }
    m_progress->setVisible(false);
    m_progress->setRange(0, 1);
}

void DownloadManuscriptsDialog::report(const QString &message, bool failure)
{
    m_status->setText(message);
    m_status->setStyleSheet(failure ? QStringLiteral("color: palette(link-visited);")
                                    : QString());
}

} // namespace milah
