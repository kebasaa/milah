#include "ui/scan_metadata_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace milah {

ScanMetadataDialog::ScanMetadataDialog(
    const ScanEntry &scan,
    const TranscriptionMetadata &current,
    QWidget *parent)
    : QDialog(parent)
    , m_current(current)
{
    setWindowTitle(QStringLiteral("What the library says"));

    auto *heading = new QLabel(
        QStringLiteral("%1 records the following about this manuscript. Take what "
                       "is useful; anything you have already written is left "
                       "unticked.")
            .arg(scan.repository.isEmpty() ? QStringLiteral("The library")
                                           : scan.repository));
    heading->setWordWrap(true);

    // Only what the sidebar has a field for, so nothing is offered that could
    // not be taken.
    const struct
    {
        const char *label;
        QString TranscriptionMetadata::*field;
        QString value;
    } offered[] = {
        {"Manuscript", &TranscriptionMetadata::manuscriptName, scan.title},
        {"Origin", &TranscriptionMetadata::origin, scan.origin},
        {"Library", &TranscriptionMetadata::libraryMark, scan.repository},
        {"Shelfmark", &TranscriptionMetadata::shelfmark, scan.shelfmark},
        {"Date", &TranscriptionMetadata::date, scan.date},
        {"Language", &TranscriptionMetadata::language, scan.language},
    };

    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    int row = 0;
    for (const auto &item : offered) {
        if (item.value.isEmpty()) {
            // The library says nothing about this, and an empty row would read
            // as though it had said nothing worth keeping.
            continue;
        }

        const QString held = m_current.*(item.field);
        auto *take = new QCheckBox;
        // Ticked only where there is nothing to lose. A field the transcriber
        // has filled in is theirs, and the library's version has to be asked
        // for rather than merely not refused.
        take->setChecked(held.trimmed().isEmpty());
        take->setToolTip(held.trimmed().isEmpty()
            ? QStringLiteral("Nothing written here yet.")
            : QStringLiteral("You wrote: %1").arg(held));

        auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(
            QString::fromLatin1(item.label)));
        auto *value = new QLabel(item.value);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);

        grid->addWidget(take, row, 0);
        grid->addWidget(label, row, 1, Qt::AlignTop);
        grid->addWidget(value, row, 2);
        ++row;

        m_rows.append(Row{item.field, item.value, take});
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    layout->addWidget(heading);
    layout->addLayout(grid);

    // Not offered as a choice, and not a field the sidebar owns: the images
    // stay on the library's server under the library's terms, so the terms
    // travel with the transcription whether or not anything else is taken.
    if (!scan.attribution.isEmpty() || !scan.licence.isEmpty()) {
        QStringList terms;
        if (!scan.attribution.isEmpty()) {
            terms.append(scan.attribution);
        }
        if (!scan.licence.isEmpty()) {
            terms.append(scan.licence);
        }
        auto *rights = new QLabel(terms.join(QStringLiteral(" ")));
        rights->setWordWrap(true);
        rights->setObjectName(QStringLiteral("emptyState"));
        layout->addWidget(rights);
    }

    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Open scan"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(560, 380);
}

TranscriptionMetadata ScanMetadataDialog::merged() const
{
    TranscriptionMetadata metadata = m_current;
    for (const Row &row : m_rows) {
        if (row.take->isChecked()) {
            metadata.*(row.field) = row.value;
        }
    }
    return metadata;
}

} // namespace milah
