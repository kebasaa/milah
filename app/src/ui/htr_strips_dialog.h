#pragma once

#include <QDialog>
#include <QImage>
#include <QList>
#include <QString>

namespace milah {

/// One line as training will be shown it.
struct TrainingStrip
{
    /// The recogniser's index for the line, one-based for reading.
    int line = 0;
    /// The ground truth that goes with the picture — what the model is told the
    /// strip says.
    QString text;
    /// The strip itself, dewarped and masked. Null where kraken refused it.
    QImage image;
    /// Why kraken would not cut it, or empty where it did.
    QString refused;
};

/// What the model will actually be shown, folio by folio.
///
/// **The one part of the pipeline nobody could see.** Training does not learn
/// from the folio or from the word boxes; it learns from a strip per line, cut
/// by dewarping the ink along the line's baseline and zeroing everything outside
/// its boundary. Every quality question about a training set is a question about
/// those strips — is this one straight, does it hold one line of writing or two,
/// has the mask taken in the line above — and until now not one of them had ever
/// been looked at. The geometry could only be argued about.
///
/// It also shows the refusals, which are the other silence. Kraken skips a line
/// it cannot cut and carries on with a log warning, and ketos compile drops a
/// line whose text is empty, so a transcriber can save sixty lines, train on
/// forty-five, and never be told. Here a refused line keeps its place in the
/// list and says what was wrong with it.
class HtrStripsDialog final : public QDialog
{
    Q_OBJECT

public:
    /// `note` is what the picture was — the library's largest scan, or the copy
    /// on screen and why. The strips are cut from whatever the save would use,
    /// so the preview cannot drift from the thing it is a preview of, and which
    /// of the two it was belongs on screen beside them.
    HtrStripsDialog(
        const QString &folio,
        const QString &note,
        const QList<TrainingStrip> &strips,
        QWidget *parent = nullptr);
};

} // namespace milah
