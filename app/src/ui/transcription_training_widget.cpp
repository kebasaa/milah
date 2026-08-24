#include "ui/transcription_training_widget.h"

#include "core/line_fill.h"
#include "core/transcription.h"
#include "core/books.h"
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

    m_do = new QPushButton;
    connect(m_do, &QPushButton::clicked, this, &TranscriptionTrainingWidget::act);
    inner->addWidget(m_do);

    // Quieter than the one above it: flat and a size smaller, because it is not
    // the next thing to do — it is the thing that becomes possible, and stays
    // possible, once the hand has enough.
    m_train = new QPushButton(QStringLiteral("Train a model… (hours)"));
    m_train->setFlat(true);
    QFont smaller = m_train->font();
    smaller.setPointSizeF(std::max(7.0, smaller.pointSizeF() * 0.9));
    m_train->setFont(smaller);
    m_train->setToolTip(QStringLiteral(
        "Teaches a model this hand from everything you have saved. It runs on "
        "the processor and takes hours; what comes out becomes the model "
        "Transcribe uses."));
    m_train->hide();
    connect(m_train, &QPushButton::clicked, this, [this] {
        m_controller->showTraining();
        refresh();
    });
    inner->addWidget(m_train);

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
        m_step = Step::Nothing;
        m_do->setText(QStringLiteral("Save this folio"));
        m_do->setEnabled(false);
        m_do->setToolTip(QString());
        // A set is a hand's, not a folio's. With nothing open there is nothing
        // to say about a folio and still, possibly, something to train.
        const TrainingSet::Set set =
            TrainingSet::contentsOf(TrainingSet::slugFor(m_controller->metadata()));
        m_train->setVisible(set.lines >= TrainingSet::EnoughLines);
        return;
    }

    // Both numbers out of one call, so nothing can put two different ideas of
    // what a line is either side of an "of" — which is how this came to read
    // "35 of 31" on a folio whose lines had been split.
    const FolioProgress progress = m_controller->trainingProgress();
    m_folioSaid->setText(
        progress.lines > 0
            ? QStringLiteral("This folio — %1 of %2 line(s) read")
                  .arg(progress.finished)
                  .arg(progress.lines)
            : QStringLiteral("This folio — not read yet"));
    m_folioBar->setRange(0, std::max(1, progress.lines));
    m_folioBar->setValue(progress.finished);

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

    // The path, in the order it is walked. Whichever step this folio is at, the
    // button is that step — the panel is a second route to what the toolbar and
    // the right-click already do, never a second way of doing it.
    if (!m_controller->hasRecognisedWords()) {
        m_step = Step::Read;
        m_state->setText(
            QStringLiteral("Nothing has read this folio, so there are no lines to "
                           "teach from yet."));
        m_do->setText(QStringLiteral("Read this folio (minutes)"));
        m_do->setToolTip(QStringLiteral(
            "The same as Transcribe in the toolbar. Kraken looks at the folio and "
            "draws a box round each word it finds; on a processor that is minutes."));
    } else if (progress.finished == 0 && !m_controller->canFillFromOsis()) {
        // Boxes on the folio but nothing a fill could use — an imported layout
        // that numbered no lines. Nothing to offer but the reading.
        m_step = Step::Nothing;
        m_state->setText(QStringLiteral("This folio's boxes carry no line numbers, "
                                        "so a transcription cannot be laid onto it."));
        m_do->setText(QStringLiteral("Save this folio"));
    } else if (progress.finished == 0 && page->fillStartLine < 0) {
        const ResumePoint resume = m_controller->resumePoint();
        m_step = resume.isValid() ? Step::Continue : Step::Fill;
        m_state->setText(QStringLiteral("Read, and waiting for a transcription to "
                                        "be laid onto it."));
        m_do->setText(
            resume.isValid()
                ? QStringLiteral("Continue from %1").arg(readableVerseId(resume.verse))
                : QStringLiteral("Fill from a transcription…"));
        m_do->setToolTip(QStringLiteral(
            "The same as right-clicking the folio. Lays a published transcription "
            "onto the boxes, so that reading a line is checking it rather than "
            "typing it."));
    } else if (progress.finished == 0) {
        m_step = Step::Nothing;
        m_state->setText(QStringLiteral("Nothing read yet. In the line view, read a "
                                        "line against the ink and press Space."));
        m_do->setText(QStringLiteral("Save this folio"));
    } else if (saved.stale) {
        m_step = Step::Save;
        m_state->setText(QStringLiteral("⚠ Saved, but you have corrected it since."));
        m_do->setText(QStringLiteral("Save again"));
    } else if (!saved.present) {
        m_step = Step::Save;
        m_state->setText(progress.finished < progress.lines
                             ? QStringLiteral("More lines to read, but these can go "
                                              "in now — saving again replaces them.")
                             : QStringLiteral("Every line read."));
        m_do->setText(QStringLiteral("Save %1 line(s)").arg(progress.finished));
    } else if (set.lines >= TrainingSet::EnoughLines) {
        // This folio is in and up to date, and the hand has enough. The one
        // thing left is the hours.
        m_step = Step::Train;
        m_state->setText(QStringLiteral("%1 line(s) of this folio are in the set, "
                                        "and %2 has enough to train on.")
                             .arg(saved.lines)
                             .arg(name));
        m_do->setText(QStringLiteral("Train a model… (hours)"));
        m_do->setToolTip(QStringLiteral(
            "Teaches a model this hand from what you have saved. It runs on the "
            "processor and takes hours; what comes out becomes the model "
            "Transcribe uses."));
    } else {
        m_step = Step::Save;
        m_state->setText(QStringLiteral("%1 line(s) of this folio are in the set. "
                                        "%2 more for the hand.")
                             .arg(saved.lines)
                             .arg(TrainingSet::EnoughLines - set.lines));
        m_do->setText(QStringLiteral("Save this folio"));
    }

    if (m_step == Step::Save || m_step == Step::Nothing) {
        m_do->setEnabled(progress.finished > 0);
        m_do->setToolTip(
            progress.finished > 0
                ? QStringLiteral("Puts this folio's %1 read line(s) into %2's "
                                 "training set.")
                      .arg(progress.finished)
                      .arg(name)
                : QStringLiteral("No line of this folio has been read all the way "
                                 "through yet. In the line view, read a line "
                                 "against the ink and press Space over it."));
    } else {
        m_do->setEnabled(true);
    }

    // Offered whenever the hand has enough, whatever this folio is doing — and
    // hidden when the button above it already says the same thing.
    m_train->setVisible(set.lines >= TrainingSet::EnoughLines && m_step != Step::Train);
}

void TranscriptionTrainingWidget::act()
{
    switch (m_step) {
    case Step::Read:
        m_controller->transcribeFolio();
        break;
    case Step::Fill:
        m_controller->fillFromOsis();
        break;
    case Step::Continue:
        m_controller->fillFromOsis(QPoint(), true);
        break;
    case Step::Train:
        m_controller->showTraining();
        break;
    case Step::Save:
    case Step::Nothing:
        m_controller->saveFolioForTraining();
        break;
    }
    // Straight away rather than on the timer: something has just been pressed
    // and the button's own label is one of the things that changes.
    refresh();
}

} // namespace milah
