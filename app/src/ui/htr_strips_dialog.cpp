#include "ui/htr_strips_dialog.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

namespace milah {
namespace {

/// The widest a strip is drawn. A line cut from a 3932-pixel master is some
/// three thousand pixels across and forty tall, which at its own size is a
/// thread across the screen and off both ends of it. Scaled to fit, the shape
/// of the line — straight or sheared, one row of writing or two — is what shows,
/// and that is what this exists to let somebody judge.
constexpr int WidestStrip = 900;

/// Below this a strip is drawn at its own size instead of being stretched up to
/// the width above, which would show nothing but interpolation.
constexpr int NotWorthEnlarging = 40;

} // namespace

HtrStripsDialog::HtrStripsDialog(
    const QString &folio,
    const QString &note,
    const QList<TrainingStrip> &strips,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("What training will be shown — %1").arg(folio));

    auto *outer = new QVBoxLayout(this);

    int cut = 0;
    for (const TrainingStrip &strip : strips) {
        if (strip.refused.isEmpty()) {
            ++cut;
        }
    }

    auto *heading = new QLabel;
    heading->setWordWrap(true);
    heading->setTextFormat(Qt::RichText);
    heading->setText(
        QStringLiteral("<b>%1 line(s) cut, %2 refused.</b> Each picture below is "
                       "one line as the model will be shown it — straightened "
                       "along its baseline and masked to its outline, which is "
                       "what training does and not an imitation of it. Under each "
                       "is the text the model is told it says.%3")
            .arg(cut)
            .arg(strips.size() - cut)
            .arg(note.isEmpty() ? QString()
                                : QStringLiteral("<p>%1</p>").arg(note.toHtmlEscaped())));
    outer->addWidget(heading);

    auto *inner = new QWidget;
    auto *rows = new QVBoxLayout(inner);
    rows->setSpacing(14);

    for (const TrainingStrip &strip : strips) {
        auto *number = new QLabel(QStringLiteral("Line %1").arg(strip.line));
        QFont small = number->font();
        small.setPointSizeF(std::max(7.0, small.pointSizeF() * 0.85));
        number->setFont(small);
        number->setEnabled(false);
        rows->addWidget(number);

        if (strip.refused.isEmpty()) {
            auto *picture = new QLabel;
            picture->setFrameShape(QFrame::StyledPanel);
            QPixmap drawn = QPixmap::fromImage(strip.image);
            if (drawn.width() > WidestStrip || drawn.height() < NotWorthEnlarging) {
                drawn = drawn.scaledToWidth(
                    std::min(WidestStrip, std::max(drawn.width(), WidestStrip / 2)),
                    Qt::SmoothTransformation);
            }
            picture->setPixmap(drawn);
            rows->addWidget(picture);
        } else {
            // Kept in its place in the list rather than left out, because a line
            // silently missing from a training set is the whole reason for this
            // window. What was wrong with it is the useful part.
            auto *why = new QLabel(strip.refused);
            why->setWordWrap(true);
            why->setFrameShape(QFrame::StyledPanel);
            rows->addWidget(why);
        }

        auto *truth = new QLabel(strip.text);
        truth->setWordWrap(true);
        truth->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rows->addWidget(truth);
    }
    rows->addStretch();

    auto *area = new QScrollArea;
    area->setWidget(inner);
    area->setWidgetResizable(true);
    outer->addWidget(area, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    resize(760, 640);
}

} // namespace milah
