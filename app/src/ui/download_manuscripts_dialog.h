#pragma once

#include "core/manuscript_catalogue.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QList>
#include <QString>

class QTimer;

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

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
    void showCatalogue();
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

    QTimer *m_waiting = nullptr;
    QElapsedTimer m_waited;

    ManuscriptCatalogue m_catalogue;
    /// What Download will fetch, drained as it goes.
    QList<CatalogueEntry> m_queue;
    int m_queued = 0;
    int m_downloaded = 0;
    bool m_busy = false;
};

} // namespace milah
