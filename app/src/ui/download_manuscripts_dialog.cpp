#include "ui/download_manuscripts_dialog.h"

#include "ui/network_fetch.h"

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
    "https://raw.githubusercontent.com/kebasaa/hebrew_manuscripts/main/";
/// The catalogue, at the root of the published set.
constexpr auto kManifestName = "manifest_manuscripts.json";
/// The texts, in a folder of their own below it.
///
/// The manifest names a file, not a path, so this is the one place that knows
/// where the texts are kept — moving them is this line rather than every entry
/// in the catalogue.
constexpr auto kTextFolder = "manuscripts/";

/// A manifest that has grown far beyond a catalogue is not one, and reading an
/// unbounded reply from the network into memory is how that becomes a problem.
constexpr qint64 kManifestSizeLimit = 1 * 1024 * 1024;

/// After this long with nothing to show, say so rather than look frozen.
constexpr int kWaitingNoticeMs = 6 * 1000;

} // namespace

namespace {
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
    // ResetRole keeps these to the left of Download on every platform, which is
    // where a thing you do *before* deciding belongs.
    m_refresh = buttons->addButton(
        QStringLiteral("Refresh"), QDialogButtonBox::ResetRole);
    m_refresh->setToolTip(
        QStringLiteral("Read the catalogue again, to see texts added or corrected "
                       "since this window opened."));
    connect(m_refresh, &QPushButton::clicked, this, [this] {
        if (!m_busy) {
            fetchCatalogue();
        }
    });

    m_selectUpdates = buttons->addButton(
        QStringLiteral("Select updates"), QDialogButtonBox::ResetRole);
    m_selectUpdates->setEnabled(false);
    m_selectUpdates->setToolTip(
        QStringLiteral("Tick every text whose published version has changed since "
                       "it was downloaded."));
    connect(m_selectUpdates, &QPushButton::clicked, this, [this] { selectUpdates(); });

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

// Nothing here tries to defeat the CDN, because nothing can. The catalogue is
// served with Cache-Control: max-age=300, and both of the usual answers were
// measured against the published host and both failed: a unique ?t= query is
// stripped from the cache key (a random query returned the stale copy at the
// same moment a commit-SHA URL returned the new one), and Cache-Control:
// no-cache on the request is ignored outright (X-Cache: HIT either way). Only
// an immutable commit-SHA URL is reliably fresh, and reaching one means asking
// the GitHub API what the branch points at — a second host, a rate limit, and
// the end of MILAH_MANUSCRIPT_URL pointing anywhere that is not GitHub.
//
// So a text corrected minutes ago may take up to five to appear. Refresh is
// still worth its place: it is what shows a library going stale after a text
// was corrected an hour or a month ago, which is the case this is actually for.
QNetworkReply *DownloadManuscriptsDialog::request(const QUrl &url)
{
    // Same host only: these addresses are the published repository's, and a
    // redirect off it is not something to follow with a text that will be
    // written into somebody's library.
    return fetch(m_network, url, Redirects::SameHost, m_preferIPv4);
}

void DownloadManuscriptsDialog::fetchCatalogue()
{
    const QUrl url(baseUrl() + QString::fromLatin1(kManifestName));
    if (!url.isValid()) {
        report(QStringLiteral("The catalogue address is not a valid URL."), true);
        return;
    }

    m_tree->clear();
    if (m_refresh) {
        m_refresh->setEnabled(false);
    }

    m_settled = false;
    m_fallback = nullptr;
    startWaitingNotice();

    QNetworkReply *reply = request(url);
    m_primary = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        readCatalogueReply(reply);
    });

    // The route probe. If nothing at all has arrived by the time this fires,
    // the addresses being tried are not carrying traffic, and waiting longer
    // only wastes the editor's time.
    if (m_preferIPv4) {
        return;
    }
    auto *probe = new QTimer(reply);
    probe->setSingleShot(true);
    connect(probe, &QTimer::timeout, this, [this, reply] {
        if (m_settled || reply->bytesAvailable() > 0 || reply->isFinished()) {
            return;
        }
        // The first attempt is deliberately left running rather than aborted.
        // Three seconds of silence is a suspicion, not a verdict: the route may
        // be merely slow, and on the evidence it usually is. Racing the two and
        // taking whichever answers first means a wrong suspicion costs one
        // spare request, where aborting would have thrown away the attempt that
        // was going to work and left a failed fallback with nothing behind it.
        QNetworkReply *direct =
            fetchOverIPv4(m_network, reply->request().url(), Redirects::SameHost);
        if (!direct) {
            // An IPv6-only network has nothing to fall back to. Nothing to say
            // either: the first attempt is still running and may yet arrive.
            return;
        }
        m_fallback = direct;
        report(QStringLiteral("The connection is slow; trying a direct route…"));
        connect(direct, &QNetworkReply::finished, this, [this, direct] {
            readCatalogueReply(direct);
        });
    });
    probe->start(RouteProbeMs);
}

void DownloadManuscriptsDialog::readCatalogueReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_settled) {
        // The other attempt already answered. This one is the loser of the
        // race, and whatever it has to say is no longer news.
        return;
    }

    // Whether anything else is still in flight decides what a failure means.
    const QNetworkReply *other = (reply == m_primary) ? m_fallback : m_primary;
    const bool aloneNow = !other || other->isFinished();

    const bool usable = reply->error() == QNetworkReply::NoError;
    QJsonDocument document;
    if (usable) {
        document = QJsonDocument::fromJson(reply->read(kManifestSizeLimit));
    }

    if (!usable || !document.isObject()) {
        if (!aloneNow) {
            // Say nothing: the other attempt is still running and is entitled
            // to answer. Reporting here would put a failure on screen moments
            // before the catalogue appeared.
            return;
        }
        m_settled = true;
        stopWaitingNotice();
        if (m_refresh) {
            m_refresh->setEnabled(true);
        }
        report(usable ? QStringLiteral("The catalogue is not readable.")
                      : QStringLiteral("Could not read the catalogue: %1")
                            .arg(reply->errorString()),
               true);
        return;
    }

    m_settled = true;
    stopWaitingNotice();
    if (m_refresh) {
        m_refresh->setEnabled(true);
    }
    // Whichever attempt lost has nothing left to contribute, and a request left
    // running would go on holding a connection open behind a finished window.
    if (other && !other->isFinished()) {
        const_cast<QNetworkReply *>(other)->abort();
    }
    // Decided by who won, and only when there was a race to win: the ordinary
    // route answering first is proof it works, and the twelve downloads that
    // follow should not be sent down a fallback it did not need. Left alone
    // when no fallback ran, so a preference learned earlier survives a Refresh.
    if (m_fallback) {
        m_preferIPv4 = (reply == m_fallback);
    }

    m_catalogue = ManuscriptCatalogue::fromJson(document.object());
    showCatalogue();
}

void DownloadManuscriptsDialog::showCatalogue()
{
    if (m_catalogue.isEmpty()) {
        report(QStringLiteral("The catalogue lists no manuscripts."), true);
        return;
    }

    const QString library = manuscriptWriteDirectory();
    const QStringList held = m_catalogue.installedFiles(library);
    // Held but no longer matching the published checksum: the text has been
    // corrected since it was taken.
    const QStringList stale = m_catalogue.updatableFiles(library);

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

        const bool updatable = stale.contains(entry.file);
        const bool alreadyHeld = held.contains(entry.file) && !updatable;
        auto *row = new QTreeWidgetItem(book);
        row->setText(0, entry.displayTitle());
        row->setText(1, entry.covers);
        row->setText(2,
                     updatable      ? QStringLiteral("update available")
                         : alreadyHeld ? QStringLiteral("held")
                                       : humanSize(entry.bytes));
        row->setToolTip(0,
                        entry.date.isEmpty()
                            ? entry.file
                            : QStringLiteral("%1\n%2").arg(entry.date, entry.file));
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        // Nothing is ticked for the editor, updates included: this window's
        // rule is that opening it puts nothing on the wire. Select updates is
        // there for the case where taking them all is what is wanted.
        row->setCheckState(0, Qt::Unchecked);
        row->setDisabled(alreadyHeld);
        row->setData(0, Qt::UserRole, entry.file);
        row->setData(0, Qt::UserRole + 1, updatable);
    }

    m_tree->resizeColumnToContents(0);
    if (m_selectUpdates) {
        m_selectUpdates->setEnabled(!stale.isEmpty());
    }

    QString found = QStringLiteral("%1 manuscripts available.")
                        .arg(m_catalogue.entries().size());
    if (!stale.isEmpty()) {
        // Said out loud rather than left to be discovered by reading down the
        // tree, which is where an update sits several headings from the top.
        found += stale.size() == 1
                     ? QStringLiteral(" 1 can be updated.")
                     : QStringLiteral(" %1 can be updated.").arg(stale.size());
    }
    report(found + QStringLiteral(" Choose what to download."));
    updateDownloadButton();
}

void DownloadManuscriptsDialog::selectUpdates()
{
    for (int book = 0; book < m_tree->topLevelItemCount(); ++book) {
        QTreeWidgetItem *heading = m_tree->topLevelItem(book);
        for (int index = 0; index < heading->childCount(); ++index) {
            QTreeWidgetItem *row = heading->child(index);
            if (row->data(0, Qt::UserRole + 1).toBool()) {
                row->setCheckState(0, Qt::Checked);
            }
        }
    }
    // Whatever was already ticked stays ticked: this adds the updates, it does
    // not decide on the editor's behalf what else they wanted.
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
    // Re-reading the catalogue mid-download would rebuild the tree under the
    // queue that is being drained from it.
    if (m_refresh) {
        m_refresh->setEnabled(!m_busy);
    }
    if (m_selectUpdates && m_busy) {
        m_selectUpdates->setEnabled(false);
    }
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

    const QUrl url(baseUrl() + QString::fromLatin1(kTextFolder) + entry.file);
    QNetworkReply *reply = request(url);
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
