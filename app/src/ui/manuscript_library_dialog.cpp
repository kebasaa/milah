#include "ui/manuscript_library_dialog.h"

#include "core/manuscript_catalogue.h"
#include "core/osis.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace milah {
namespace {

/// The edition's own name, read out of the file's OSIS header.
///
/// Only the header is read — enough of the file to reach the end of it — because
/// a library of a dozen texts should not cost a dozen full parses to list. The
/// title wanted is the one inside <work>; the manuscripts carry several others,
/// one of which is an entire verse.
QString titleOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QFileInfo(path).fileName();
    }

    const QByteArray head = file.read(8 * 1024);
    const int workAt = head.indexOf("<work");
    if (workAt >= 0) {
        const int openAt = head.indexOf("<title", workAt);
        const int textAt = openAt < 0 ? -1 : head.indexOf('>', openAt);
        const int closeAt = textAt < 0 ? -1 : head.indexOf("</title>", textAt);
        if (closeAt > textAt) {
            const QString title =
                QString::fromUtf8(head.mid(textAt + 1, closeAt - textAt - 1)).trimmed();
            if (!title.isEmpty()) {
                return title;
            }
        }
    }
    return QFileInfo(path).fileName();
}

} // namespace

ManuscriptLibraryDialog::ManuscriptLibraryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Load manuscripts"));
    setObjectName(QStringLiteral("manuscriptLibraryDialog"));

    const QStringList library = installedManuscripts();
    m_empty = library.isEmpty();

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("manuscriptLibrary"));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setLayoutDirection(Qt::LeftToRight);
    for (const QString &path : library) {
        auto *item = new QListWidgetItem(titleOf(path), m_list);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
    }
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        m_load->setEnabled(!m_list->selectedItems().isEmpty());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

    auto *hint = new QLabel(
        m_empty ? QStringLiteral(
                      "Your library is empty. Use File ▸ Download manuscripts to "
                      "fetch the published texts, or browse for an OSIS file of "
                      "your own.")
                : QStringLiteral(
                      "Manuscripts and translations you have downloaded. Milah "
                      "knows which each one is, so they load the right way round."));
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_load = buttons->addButton(QStringLiteral("Load"), QDialogButtonBox::AcceptRole);
    m_load->setEnabled(false);

    // The escape hatch: the repository corpus and anything from pdf2osis never
    // passes through the library.
    QPushButton *browse =
        buttons->addButton(QStringLiteral("Browse for a file…"), QDialogButtonBox::ActionRole);
    connect(browse, &QPushButton::clicked, this, [this] {
        m_browse = true;
        accept();
    });

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    layout->addWidget(m_list, 1);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    resize(560, 420);
}

QStringList ManuscriptLibraryDialog::chosenFiles() const
{
    QStringList paths;
    // Row order rather than click order, so a selection reads the way the list
    // does.
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item->isSelected()) {
            paths.append(item->data(Qt::UserRole).toString());
        }
    }
    return paths;
}

} // namespace milah
