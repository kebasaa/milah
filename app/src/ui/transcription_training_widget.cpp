#include "ui/transcription_training_widget.h"

#include "core/line_fill.h"
#include "core/transcription.h"
#include "transcription_controller.h"
#include "ui/training_set.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace milah {
namespace {

/// How long the panel waits before believing the folio has stopped changing.
/// See TranscriptionTrainingWidget::m_soon.
constexpr int SettlingTime = 250;

QProgressBar *bar()
{
    auto *made = new QProgressBar;
    made->setTextVisible(false);
    made->setMaximumHeight(6);
    return made;
}

QLabel *quiet(const QString &text = QString())
{
    auto *made = new QLabel(text);
    made->setWordWrap(true);
    QFont small = made->font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() * 0.9));
    made->setFont(small);
    return made;
}

} // namespace

TranscriptionTrainingWidget::TranscriptionTrainingWidget(
    TranscriptionController *controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *box = new QGroupBox(QStringLiteral("Training"));
    auto *inner = new QVBoxLayout(box);
    inner->setSpacing(4);

    m_folioSaid = quiet();
    m_folioBar = bar();
    m_setSaid = quiet();
    m_setBar = bar();
    m_state = quiet();
    inner->addWidget(m_folioSaid);
    inner->addWidget(m_folioBar);
    inner->addSpacing(4);
    inner->addWidget(m_setSaid);
    inner->addWidget(m_setBar);
    inner->addWidget(m_state);

    m_save = new QPushButton;
    connect(m_save, &QPushButton::clicked, m_controller, [this] {
        m_controller->saveFolioForTraining();
        // Straight away rather than on the timer: the button has just been
        // pressed and its own label is one of the things that changes.
        refresh();
    });
    inner->addWidget(m_save);

    outer->addWidget(box);

    m_soon = new QTimer(this);
    m_soon->setSingleShot(true);
    m_soon->setInterval(SettlingTime);
    connect(m_soon, &QTimer::timeout, this, &TranscriptionTrainingWidget::refresh);

    const auto later = [this] { m_soon->start(); };
    connect(m_controller, &TranscriptionController::pageChanged, this, later);
    connect(m_controller, &TranscriptionController::documentChanged, this, later);
    connect(m_controller, &TranscriptionController::versesChanged, this, later);
    connect(m_controller, &TranscriptionController::wordChecked, this, later);

    refresh();
}

void TranscriptionTrainingWidget::refresh()
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        m_folioSaid->setText(QStringLiteral("No folio open."));
        m_folioBar->setRange(0, 1);
        m_folioBar->setValue(0);
        m_setSaid->clear();
        m_setBar->setRange(0, 1);
        m_setBar->setValue(0);
        m_state->clear();
        m_save->setText(QStringLiteral("Save this folio"));
        m_save->setEnabled(false);
        return;
    }

    // The lines a fill may lay into, which is the honest denominator: a line
    // that is nothing but marginalia is not a line of the work and was never
    // going to be trained on.
    const int lines = int(fillableLines(*page).size());
    const int finished = m_controller->trainableLineCount();
    m_folioSaid->setText(
        lines > 0
            ? QStringLiteral("This folio — %1 of %2 line(s) read").arg(finished).arg(lines)
            : QStringLiteral("This folio — nothing read off it yet"));
    m_folioBar->setRange(0, std::max(1, lines));
    m_folioBar->setValue(finished);

    const TrainingSet::Set set =
        TrainingSet::contentsOf(TrainingSet::slugFor(m_controller->metadata()));
    const QString name =
        set.label.isEmpty() ? TrainingSet::labelFor(m_controller->metadata()) : set.label;
    m_setSaid->setText(QStringLiteral("%1 — %2 of %3 line(s) saved")
                           .arg(name)
                           .arg(set.lines)
                           .arg(TrainingSet::EnoughLines));
    m_setBar->setRange(0, TrainingSet::EnoughLines);
    m_setBar->setValue(std::min(set.lines, TrainingSet::EnoughLines));

    const TrainingSet::Saved saved =
        TrainingSet::savedFolio(*page, m_controller->metadata());
    if (saved.stale) {
        m_state->setText(QStringLiteral("⚠ Saved, but you have corrected it since."));
        m_save->setText(QStringLiteral("Save again"));
    } else if (saved.present) {
        m_state->setText(QStringLiteral("%1 line(s) of this folio are in the set.")
                             .arg(saved.lines));
        m_save->setText(QStringLiteral("Save this folio"));
    } else if (set.lines >= TrainingSet::EnoughLines) {
        // Nothing to say about this folio, and something worth saying about the
        // set: it is ready, and the entry that acts on it is three levels into
        // a menu.
        m_state->setText(
            QStringLiteral("Enough to train on — File ▸ Handwriting recognition ▸ "
                           "Train a model…."));
        m_save->setText(QStringLiteral("Save this folio"));
    } else {
        m_state->clear();
        m_save->setText(QStringLiteral("Save this folio"));
    }
    // Greyed for the same reason the menu entry is, and the tooltip says the
    // same thing: a line counts once every word on it has been read.
    m_save->setEnabled(finished > 0);
    m_save->setToolTip(
        finished > 0
            ? QStringLiteral("Puts this folio's %1 finished line(s) into %2's "
                             "training set.")
                  .arg(finished)
                  .arg(name)
            : QStringLiteral("No line of this folio has been read all the way "
                             "through yet. In the line view, read a line against "
                             "the ink and press Space over it."));
}

} // namespace milah
