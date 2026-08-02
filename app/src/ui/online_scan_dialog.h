#pragma once

#include "core/scan_catalogue.h"

#include <QDialog>
#include <QPointer>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace milah {

/// What can be transcribed without downloading a codex first.
///
/// Reads the published scan catalogue and lists what the holding libraries
/// offer. Nothing is fetched but the catalogue itself: choosing a manuscript
/// records where its folios live, and a folio is fetched when it is opened.
class OnlineScanDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit OnlineScanDialog(QWidget *parent = nullptr);

    /// The manuscript the reader chose. Its id is empty when they chose none.
    ScanEntry chosen() const { return m_chosen; }

    /// Where the catalogue is published — the same base the manuscripts come
    /// from, so pointing Milah elsewhere is still one environment variable.
    static QString baseUrl();

private:
    void fetchCatalogue();
    void readCatalogueReply(QNetworkReply *reply);
    void showCatalogue();
    void updateOpenButton();
    void report(const QString &message, bool failure = false);

    QNetworkAccessManager *m_network = nullptr;
    /// Both attempts are held weakly: whichever answers first aborts the other,
    /// and the loser is deleted underneath these.
    QPointer<QNetworkReply> m_primary;
    QPointer<QNetworkReply> m_fallback;
    /// True once one of them has answered, so the loser's failure is discarded
    /// rather than reported over a catalogue that already arrived.
    bool m_settled = false;
    bool m_preferIPv4 = false;

    QTreeWidget *m_tree = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_open = nullptr;
    /// Reads the catalogue again. The window already reads it every time it is
    /// opened — it is built fresh each time — so this is for the case of a scan
    /// published while it was sitting open.
    QPushButton *m_refresh = nullptr;

    ScanCatalogue m_catalogue;
    ScanEntry m_chosen;
};

} // namespace milah
