#include "ui/training_set_dialog.h"

#include "ui/training_set.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Where a row keeps the stem its files are filed under.
constexpr int StemRole = Qt::UserRole + 1;

enum Column { Folio = 0, Lines, Saved, State };

} // namespace

TrainingSetDialog::TrainingSetDialog(
    const QString &slug,
    const QString &label,
    const QHash<QString, bool> &staleness,
    QWidget *parent)
    : QDialog(parent)
    , m_slug(slug)
    , m_label(label)
    , m_staleness(staleness)
{
    setWindowTitle(QStringLiteral("Training set — %1").arg(label));

    auto *outer = new QVBoxLayout(this);

    m_heading = new QLabel;
    m_heading->setWordWrap(true);
    m_heading->setTextFormat(Qt::RichText);
    outer->addWidget(m_heading);

    m_folios = new QTreeWidget;
    m_folios->setRootIsDecorated(false);
    m_folios->setSelectionMode(QAbstractItemView::SingleSelection);
    m_folios->setHeaderLabels({QStringLiteral("Folio"), QStringLiteral("Lines"),
                               QStringLiteral("Saved"), QString()});
    m_folios->header()->setSectionResizeMode(Folio, QHeaderView::Stretch);
    m_folios->header()->setStretchLastSection(false);
    outer->addWidget(m_folios, 1);

    m_strips = new QPushButton(QStringLiteral("Show strips…"));
    m_strips->setToolTip(QStringLiteral(
        "Cuts this folio's lines out of the set's own files the way training cuts "
        "them, and shows what a model would be taught from it."));
    m_remove = new QPushButton(QStringLiteral("Take out of the set"));
    m_remove->setToolTip(QStringLiteral(
        "Deletes this folio's lines and picture from the set. The transcription is "
        "not touched, and saving the folio again puts it back."));

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_strips);
    actions->addWidget(m_remove);
    actions->addStretch();
    outer->addLayout(actions);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(m_folios, &QTreeWidget::itemSelectionChanged, this,
            &TrainingSetDialog::updateButtons);
    connect(m_strips, &QPushButton::clicked, this, [this] {
        const QTreeWidgetItem *item = m_folios->currentItem();
        if (item) {
            emit stripsWanted(m_slug, item->data(Folio, StemRole).toString(),
                              item->text(Folio));
        }
    });
    connect(m_remove, &QPushButton::clicked, this, &TrainingSetDialog::removeChosen);

    reload();
    resize(560, 420);
}

void TrainingSetDialog::reload()
{
    m_folios->clear();
    int lines = 0;
    int stale = 0;
    const QLocale locale;
    for (const TrainingSet::Folio &folio : TrainingSet::foliosOf(m_slug)) {
        auto *item = new QTreeWidgetItem(m_folios);
        item->setText(Folio, folio.label);
        item->setData(Folio, StemRole, folio.stem);
        item->setToolTip(Folio, folio.identity);
        item->setText(Lines, QString::number(folio.lines));
        item->setTextAlignment(Lines, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(Saved, locale.toString(folio.saved, QLocale::ShortFormat));
        // Only for a folio this transcription has: anything else belongs to a
        // transcription that is not open, and nothing here knows what it says.
        if (m_staleness.value(folio.stem, false)) {
            item->setText(State, QStringLiteral("changed since"));
            item->setToolTip(State, QStringLiteral(
                "Corrected since it was saved. Save the folio again to put the "
                "corrections into the set."));
            ++stale;
        }
        lines += folio.lines;
    }
    for (int column = Lines; column <= State; ++column) {
        m_folios->resizeColumnToContents(column);
    }

    const int folios = m_folios->topLevelItemCount();
    m_heading->setText(
        folios == 0
            ? QStringLiteral("<b>%1</b> holds nothing yet.").arg(m_label.toHtmlEscaped())
            : QStringLiteral("<b>%1</b> — %2 line(s) off %3 folio(s), against the %4 "
                             "that open training.%5")
                  .arg(m_label.toHtmlEscaped())
                  .arg(lines)
                  .arg(folios)
                  .arg(TrainingSet::EnoughLines)
                  .arg(stale > 0 ? QStringLiteral(" %1 of them have been corrected "
                                                  "since they were saved.")
                                       .arg(stale)
                                 : QString()));
    updateButtons();
}

void TrainingSetDialog::updateButtons()
{
    const bool chosen = m_folios->currentItem() && m_folios->currentItem()->isSelected();
    m_strips->setEnabled(chosen);
    m_remove->setEnabled(chosen);
}

void TrainingSetDialog::removeChosen()
{
    const QTreeWidgetItem *item = m_folios->currentItem();
    if (!item) {
        return;
    }
    const QString folio = item->text(Folio);
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Milah"),
        QStringLiteral("Take %1 out of the training set?<p>Its %2 line(s) will no "
                       "longer be taught. The transcription is not touched, and "
                       "saving the folio again puts it back.</p>")
            .arg(folio.toHtmlEscaped(), item->text(Lines)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (TrainingSet::remove(m_slug, item->data(Folio, StemRole).toString())) {
        m_changed = true;
    }
    reload();
}

} // namespace milah
