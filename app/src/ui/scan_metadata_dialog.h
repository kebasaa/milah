#pragma once

#include "core/scan_catalogue.h"
#include "core/transcription.h"

#include <QDialog>
#include <QList>

class QCheckBox;

namespace milah {

/// What the library says about a manuscript, and which of it to take.
///
/// Shown before a scan is opened rather than applied quietly, because the
/// library's record and the transcriber's own judgement are both worth
/// something and only one of them is in the room. Rows are ticked to begin with
/// only where the transcription has nothing in that field, so re-opening a scan
/// never proposes to write over what somebody wrote.
class ScanMetadataDialog final : public QDialog
{
    Q_OBJECT

public:
    ScanMetadataDialog(
        const ScanEntry &scan,
        const TranscriptionMetadata &current,
        QWidget *parent = nullptr);

    /// `current` with the ticked fields replaced by the library's.
    TranscriptionMetadata merged() const;

private:
    /// One offered field: where it goes, what the library says, and whether it
    /// is to be taken.
    struct Row
    {
        QString TranscriptionMetadata::*field = nullptr;
        QString value;
        QCheckBox *take = nullptr;
    };

    TranscriptionMetadata m_current;
    QList<Row> m_rows;
};

} // namespace milah
