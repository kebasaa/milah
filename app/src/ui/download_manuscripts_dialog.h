#pragma once

#include "core/manuscript_catalogue.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QString>

class QTimer;

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QUrl;

namespace milah {

/// Offers the published manuscripts for download into the local library.
///
/// The only part of Milah that reaches the internet. It contacts one host, only
/// when the editor presses Download, and writes only into the library folder.
class DownloadManuscriptsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DownloadManuscriptsDialog(QWidget *parent = nullptr);

    /// True when at least one file was written, so the caller knows whether the
    /// library is worth re-reading.
    bool downloadedAnything() const { return m_downloaded > 0; }

private:
    /// Where the catalogue and its texts are published. The environment
    /// override is what lets this be exercised against a local copy, and what
    /// lets an institution serve its own set.
    static QString baseUrl();

    void fetchCatalogue();
    /// Whichever attempt arrives first, ordinary or fallen back.
    void readCatalogueReply(QNetworkReply *reply);
    void showCatalogue();
    /// Ticks every row whose held copy differs from the published one.
    void selectUpdates();

    /// Issues a GET through ui/network_fetch.h, which is where the redirect,
    /// timeout and IPv4-fallback policy now lives — shared with the scan
    /// catalogue, which needs exactly the same handling.
    QNetworkReply *request(const QUrl &url);
    /// Keeps the window honest while the first connection is being made. That
    /// can take a minute on a network whose IPv6 route is advertised but does
    /// not carry traffic — every address is tried in turn before the working
    /// one is reached, and nothing arrives until it is.
    void startWaitingNotice();
    void stopWaitingNotice();
    void startNextDownload();
    void finishDownload(QNetworkReply *reply);
    void updateDownloadButton();
    void report(const QString &message, bool failure = false);

    QNetworkAccessManager *m_network = nullptr;
    QTreeWidget *m_tree = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_selectUpdates = nullptr;

    QTimer *m_waiting = nullptr;
    QElapsedTimer m_waited;
    /// Set once the fallback has been shown to be the one that works, so the
    /// downloads that follow go straight there rather than each paying the
    /// wait again.
    bool m_preferIPv4 = false;

    /// The two attempts at the catalogue, racing. The second exists only when
    /// the first has gone quiet long enough to be doubted, and either may win.
    /// Held by QPointer because the loser is deleteLater'd out from under this.
    QPointer<QNetworkReply> m_primary;
    QPointer<QNetworkReply> m_fallback;
    /// True once one of them has produced a catalogue, or once the last one
    /// standing has failed. Whatever arrives afterwards is no longer news.
    bool m_settled = false;

    ManuscriptCatalogue m_catalogue;
    /// What Download will fetch, drained as it goes.
    QList<CatalogueEntry> m_queue;
    int m_queued = 0;
    int m_downloaded = 0;
    bool m_busy = false;
};

} // namespace milah
