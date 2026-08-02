#include "ui/online_scan_dialog.h"

#include "ui/network_fetch.h"

#include <QDialogButtonBox>
#include <QHash>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Beside the manuscript catalogue, in the same published repository.
constexpr auto kDefaultBaseUrl =
    "https://raw.githubusercontent.com/kebasaa/hebrew_manuscripts/main/";
constexpr auto kManifestName = "manifest_scans.json";

/// A codex of 328 folios comes to some 50 KB of addresses; a catalogue of many
/// is still far under this. Bounded because a reply is read into memory, and
/// what is on the other end is not this program's to trust.
constexpr qint64 kManifestSizeLimit = 8 * 1024 * 1024;

QString folioCount(int folios)
{
    return folios == 1 ? QStringLiteral("1 folio")
                       : QStringLiteral("%1 folios").arg(folios);
}

/// How far through the codex a scan reaches, for the Extent column.
///
/// The range is written as the manifest writes it but for the separator: ".."
/// is there so a folio label may contain a hyphen, which is a thing a file
/// format has to care about and a reader does not.
///
/// An unavailable entry has no folios to report — it says so instead, which is
/// the one visible sign in the tree that the row beneath it cannot be opened;
/// the rest of the row is disabled rather than differently coloured.
QString extentOf(const ScanEntry &entry)
{
    if (!entry.unavailable.isEmpty()) {
        return QStringLiteral("No scan available");
    }
    const QString count = folioCount(entry.pages.size());
    if (entry.folios.isEmpty()) {
        return count;
    }
    QString range = entry.folios;
    range.replace(QStringLiteral(".."), QStringLiteral("–"));
    return QStringLiteral("%1 (%2)").arg(count, range);
}

/// What to head a manuscript's books with.
///
/// The shelfmark, which is what a reader files a manuscript under and what the
/// books beneath share. A bare IIIF manifest may state none, so there are two
/// fallbacks: the repository at least says whose it is, and the title always
/// says something.
QString manuscriptHeading(const ScanEntry &entry)
{
    if (!entry.shelfmark.isEmpty()) {
        return entry.shelfmark;
    }
    if (!entry.repository.isEmpty()) {
        return entry.repository;
    }
    return entry.title;
}

/// What says two entries are of one manuscript.
///
/// The address they were read from, because that is the one thing a library
/// gives every entry and gives the same for every part of one codex. The
/// shelfmark would do where there is one, and there is not always one.
QString manuscriptKey(const ScanEntry &entry)
{
    if (!entry.source.isEmpty()) {
        return entry.source;
    }
    return manuscriptHeading(entry);
}

QString manuscriptCount(int manuscripts)
{
    return manuscripts == 1 ? QStringLiteral("1 manuscript")
                            : QStringLiteral("%1 manuscripts").arg(manuscripts);
}

/// How many things there are to open, which is no longer how many manuscripts
/// there are: a codex offered book by book is one manuscript and many scans.
QString scanCount(int scans)
{
    return scans == 1 ? QStringLiteral("1 scan")
                      : QStringLiteral("%1 scans").arg(scans);
}

/// Said only when it is not zero: most catalogues have nothing to report here,
/// and a status line that always mentions "0 not yet digitised" would train a
/// reader to stop reading it.
QString unavailableNote(int unavailable)
{
    if (unavailable == 0) {
        return QString();
    }
    return unavailable == 1
        ? QStringLiteral(" 1 of these has no scan yet.")
        : QStringLiteral(" %1 of these have no scan yet.").arg(unavailable);
}

} // namespace

QString OnlineScanDialog::baseUrl()
{
    const QString override = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("MILAH_MANUSCRIPT_URL"));
    if (!override.isEmpty()) {
        return override.endsWith(QLatin1Char('/')) ? override
                                                   : override + QLatin1Char('/');
    }
    return QString::fromLatin1(kDefaultBaseUrl);
}

OnlineScanDialog::OnlineScanDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Get online manuscript scan"));

    m_tree = new QTreeWidget;
    // A manuscript holds its books, so the list has two levels and says so.
    m_tree->setRootIsDecorated(true);
    // Off, unlike the flat list this used to be: banding a tree stripes the
    // headings along with their books and hides the shape it is there to show.
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({
        QStringLiteral("Manuscript"),
        QStringLiteral("Held at"),
        QStringLiteral("Date"),
        QStringLiteral("Extent"),
    });
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(
        m_tree,
        &QTreeWidget::itemSelectionChanged,
        this,
        &OnlineScanDialog::updateOpenButton);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (m_open->isEnabled()) {
            accept();
        }
    });

    m_progress = new QProgressBar;
    m_progress->setVisible(false);

    m_status = new QLabel;
    m_status->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_open = buttons->addButton(
        QStringLiteral("Open scan"), QDialogButtonBox::AcceptRole);
    m_open->setEnabled(false);

    m_refresh = buttons->addButton(
        QStringLiteral("Refresh"), QDialogButtonBox::ResetRole);
    m_refresh->setToolTip(QStringLiteral(
        "Reads the catalogue again. The list is already read afresh every time "
        "this window is opened; a scan published in the last few minutes may "
        "still take a moment to appear, because the catalogue is served from a "
        "cache Milah cannot ask to be skipped."));
    connect(m_refresh, &QPushButton::clicked, this, &OnlineScanDialog::fetchCatalogue);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);
    layout->addWidget(buttons);

    resize(820, 460);

    m_network = new QNetworkAccessManager(this);
    fetchCatalogue();
}

void OnlineScanDialog::report(const QString &message, bool failure)
{
    m_status->setText(message);
    m_status->setStyleSheet(
        failure ? QStringLiteral("color: palette(link-visited);") : QString());
}

void OnlineScanDialog::fetchCatalogue()
{
    const QUrl url(baseUrl() + QString::fromLatin1(kManifestName));
    if (!url.isValid()) {
        report(QStringLiteral("The catalogue address is not a valid URL."), true);
        return;
    }

    // Whatever the last attempt left behind. Refresh can be pressed while a
    // slow one is still in flight, and an old reply answering into a new
    // attempt would settle it with a catalogue nobody asked for.
    for (const QPointer<QNetworkReply> &previous : {m_primary, m_fallback}) {
        if (previous && !previous->isFinished()) {
            previous->abort();
        }
    }
    m_primary = nullptr;
    m_fallback = nullptr;

    m_settled = false;
    m_tree->clear();
    updateOpenButton();
    m_refresh->setEnabled(false);
    // Indeterminate rather than sitting at zero, which reads as broken rather
    // than as waiting.
    m_progress->setRange(0, 0);
    m_progress->setVisible(true);
    report(QStringLiteral("Reading the catalogue…"));

    // The reply is handed to its own handler rather than read back off the
    // member. Refresh replaces the member, and a lambda that looked it up would
    // then hand the previous attempt's answer to whichever reply is current —
    // reading a catalogue that had already been superseded.
    QNetworkReply *primary = fetch(m_network, url, Redirects::SameHost, m_preferIPv4);
    m_primary = primary;
    connect(primary, &QNetworkReply::finished, this, [this, primary] {
        readCatalogueReply(primary);
    });

    // Three seconds of silence is a suspicion, not a verdict: some networks
    // advertise an IPv6 route that drops packets, and the stack spends about
    // twenty seconds per address discovering it. Racing a second attempt rather
    // than aborting the first means a wrong suspicion costs one spare request.
    auto *probe = new QTimer(primary);
    probe->setSingleShot(true);
    connect(probe, &QTimer::timeout, this, [this, url, primary] {
        // Parented to the reply, so a superseded attempt's probe dies with it —
        // but it can still fire in the moment before that, and must not race a
        // fetch that has already moved on.
        if (m_settled || m_fallback || m_primary != primary) {
            return;
        }
        QNetworkReply *fallback = fetchOverIPv4(m_network, url, Redirects::SameHost);
        if (!fallback) {
            return;
        }
        m_fallback = fallback;
        connect(fallback, &QNetworkReply::finished, this, [this, fallback] {
            readCatalogueReply(fallback);
        });
    });
    probe->start(RouteProbeMs);
}

void OnlineScanDialog::readCatalogueReply(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (m_settled) {
        // The other attempt already answered; this one is the loser.
        return;
    }
    if (reply != m_primary && reply != m_fallback) {
        // Neither: it belongs to an attempt Refresh has since replaced, and its
        // answer is about a catalogue nobody is waiting for any more.
        return;
    }

    const QPointer<QNetworkReply> other = reply == m_primary ? m_fallback : m_primary;
    const bool aloneNow = !other || other->isFinished();

    if (reply->error() != QNetworkReply::NoError) {
        // Only a failure once nothing else is still trying: reporting it while
        // the other attempt is in flight would say the catalogue is unreachable
        // a moment before it arrives.
        if (aloneNow) {
            m_settled = true;
            m_progress->setVisible(false);
            m_refresh->setEnabled(true);
            report(
                QStringLiteral("Could not read the catalogue: %1").arg(reply->errorString()),
                true);
        }
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->read(kManifestSizeLimit));
    if (!document.isObject()) {
        if (aloneNow) {
            m_settled = true;
            m_progress->setVisible(false);
            m_refresh->setEnabled(true);
            report(QStringLiteral("The catalogue could not be understood."), true);
        }
        return;
    }

    m_settled = true;
    m_refresh->setEnabled(true);
    if (other && !other->isFinished()) {
        other->abort();
    }
    // Worth remembering for the rest of the session: if the fallback is what
    // answered, the ordinary route does not work on this network.
    m_preferIPv4 = (reply == m_fallback);

    m_progress->setVisible(false);
    m_catalogue = ScanCatalogue::fromJson(document.object());
    showCatalogue();
}

void OnlineScanDialog::showCatalogue()
{
    m_tree->clear();

    // A manuscript heads the books it holds. One binding may be offered whole
    // and book by book at once — Cambridge MS Oo.1.32 is twenty-seven entries of
    // the twenty-eight here — and twenty-seven rows each reading "MS Oo.1.32" is
    // a list nobody can find Romans in.
    //
    // Insertion order, not sorted: the manifest is written in the order a reader
    // would want to be offered these, books in the order the codex binds them,
    // and re-sorting here would scatter Philemon among the gospels alphabetically.
    QHash<QString, QTreeWidgetItem *> manuscripts;

    for (const ScanEntry &entry : m_catalogue.entries()) {
        const QString key = manuscriptKey(entry);
        QTreeWidgetItem *manuscript = manuscripts.value(key);
        if (!manuscript) {
            manuscript = new QTreeWidgetItem(m_tree);
            manuscript->setText(0, manuscriptHeading(entry));
            // Whose it is and when it was written belong to the manuscript, not
            // to each of its books, and printing them on every row would say the
            // same thing twenty-six times.
            manuscript->setText(1, entry.repository);
            manuscript->setText(2, entry.date);
            manuscript->setExpanded(true);
            // A manuscript is not a thing to open; the books under it are. It
            // carries no id, so updateOpenButton() would refuse it anyway — but
            // leaving it selectable would let a reader highlight a row and find
            // the Open button dead with nothing to say why.
            manuscript->setFlags(manuscript->flags() & ~Qt::ItemIsSelectable);
            manuscripts.insert(key, manuscript);
        }

        auto *row = new QTreeWidgetItem(manuscript);
        // The title alone: the shelfmark is on the heading just above it, and
        // displayTitle() would print it again on every book.
        row->setText(0, entry.title);
        row->setText(3, extentOf(entry));
        row->setData(0, Qt::UserRole, entry.id);

        if (!entry.unavailable.isEmpty()) {
            // Shown, not hidden: a transcriber should be able to see that this
            // manuscript exists without already knowing to look for it. But
            // there is nothing behind it, so it is disabled the same way a
            // manuscript heading is — no selection, and updateOpenButton()
            // then never finds it chosen, which is what keeps the Open button
            // dead without a second mechanism to keep in step with the first.
            row->setDisabled(true);
            row->setFlags(row->flags() & ~Qt::ItemIsSelectable);
        }

        // The terms are not decoration: the images stay on the library's own
        // server under the library's own licence, and a transcriber is entitled
        // to know what they are before opening anything.
        QStringList detail;
        if (!entry.unavailable.isEmpty()) {
            detail.append(entry.unavailable);
        }
        if (!entry.origin.isEmpty()) {
            detail.append(QStringLiteral("Written in %1.").arg(entry.origin));
        }
        if (!entry.material.isEmpty()) {
            detail.append(QStringLiteral("%1.").arg(entry.material));
        }
        if (!entry.provenance.isEmpty()) {
            detail.append(entry.provenance);
        }
        if (!entry.attribution.isEmpty()) {
            detail.append(entry.attribution);
        }
        if (!entry.licence.isEmpty()) {
            detail.append(entry.licence);
        }
        const QString tooltip = detail.join(QStringLiteral("\n"));
        for (int column = 0; column < m_tree->columnCount(); ++column) {
            row->setToolTip(column, tooltip);
            // The heading is the only place the manuscript's own details are
            // shown, so it carries them too.
            manuscript->setToolTip(column, tooltip);
        }
    }

    if (m_catalogue.isEmpty()) {
        report(QStringLiteral("The catalogue offers no scans."));
        return;
    }
    int unavailable = 0;
    for (const ScanEntry &entry : m_catalogue.entries()) {
        if (!entry.unavailable.isEmpty()) {
            ++unavailable;
        }
    }
    // Both counts, because they are no longer the same number and the
    // interesting one depends on what is being looked for. The unavailable
    // note is appended rather than folded into scanCount(), so a catalogue
    // with none of them reads exactly as it always did.
    report(QStringLiteral("%1, %2 to choose from. Opening one fetches folios as "
                          "you reach them; nothing is downloaded now.%3")
               .arg(
                   manuscriptCount(manuscripts.size()),
                   scanCount(m_catalogue.entries().size()),
                   unavailableNote(unavailable)));
    updateOpenButton();
}

void OnlineScanDialog::updateOpenButton()
{
    m_chosen = ScanEntry();

    const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
    if (!selected.isEmpty()) {
        const QString id = selected.constFirst()->data(0, Qt::UserRole).toString();
        for (const ScanEntry &entry : m_catalogue.entries()) {
            if (entry.id == id) {
                m_chosen = entry;
                break;
            }
        }
    }
    m_open->setEnabled(!m_chosen.id.isEmpty());
}

} // namespace milah
