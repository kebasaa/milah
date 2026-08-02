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

/// One field of the `<work>` element in a file's OSIS header.
///
/// Only the header is read — enough of the file to reach the end of it —
/// because a library of a dozen texts should not cost a dozen full parses to
/// list. Scoped to `<work>` on purpose: the manuscripts carry several `<title>`
/// elements elsewhere, one of which is an entire verse of pointed Hebrew.
///
/// Empty when the file cannot be read or carries no such field.
QString headerFieldOf(const QString &path, const QByteArray &tag)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    const QByteArray head = file.read(8 * 1024);
    const int workAt = head.indexOf("<work");
    if (workAt < 0) {
        return QString();
    }

    const int openAt = head.indexOf("<" + tag, workAt);
    const int textAt = openAt < 0 ? -1 : head.indexOf('>', openAt);
    const int closeAt = textAt < 0 ? -1 : head.indexOf("</" + tag + ">", textAt);
    if (closeAt <= textAt) {
        return QString();
    }
    return QString::fromUtf8(head.mid(textAt + 1, closeAt - textAt - 1)).trimmed();
}

/// The edition's own name, or the file name when the header does not give one:
/// a row is never left nameless.
QString titleOf(const QString &path)
{
    const QString title = headerFieldOf(path, "title");
    return title.isEmpty() ? QFileInfo(path).fileName() : title;
}

/// Who holds the copyright and on what terms, as the header states it.
///
/// Shown here as well as in the download window because the terms are not
/// uniform across the library — some translations are "All rights reserved"
/// while the transcriptions beside them are CC BY-NC-SA — and a reader looking
/// at what they hold should not have to reopen a download window to find out
/// which is which.
QString rightsOf(const QString &path)
{
    return headerFieldOf(path, "rights");
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
        const QString rights = rightsOf(path);
        item->setToolTip(rights.isEmpty()
                             ? path
                             : QStringLiteral("Rights: %1\n%2").arg(rights, path));
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
