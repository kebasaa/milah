#include "ui/transcription_training_widget.h"

#include "core/line_fill.h"
#include "core/transcription.h"
#include "core/books.h"
#include "core/training_export.h"
#include "transcription_controller.h"
#include "ui/training_set.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QMap>
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

    // Above everything else, because it is about the folio just left and is the
    // one thing here that will not still be true in a minute.
    m_notice = new QFrame;
    m_notice->setFrameShape(QFrame::StyledPanel);
    auto *noticeRows = new QVBoxLayout(m_notice);
    noticeRows->setContentsMargins(6, 4, 6, 4);
    m_noticeSaid = quiet();
    noticeRows->addWidget(m_noticeSaid);
    auto *noticeButtons = new QHBoxLayout;
    auto *update = new QPushButton(QStringLiteral("Update set"));
    auto *notNow = new QPushButton(QStringLiteral("Not now"));
    notNow->setFlat(true);
    noticeButtons->addWidget(update);
    noticeButtons->addWidget(notNow);
    noticeButtons->addStretch();
    noticeRows->addLayout(noticeButtons);
    m_notice->hide();
    inner->addWidget(m_notice);
    connect(notNow, &QPushButton::clicked, this, &TranscriptionTrainingWidget::hideNotice);
    connect(update, &QPushButton::clicked, this, [this] {
        // Only if it is still that folio: a notice nobody answered can outlive
        // the document it was about.
        const QList<TranscribedPage> &pages = m_controller->document().pages;
        if (m_noticePage >= 0 && m_noticePage < pages.size()
            && pages.at(m_noticePage).imageEntry == m_noticeEntry) {
            m_controller->saveFolioForTrainingAt(m_noticePage);
        }
        hideNotice();
        refresh();
    });

    m_folioSaid = quiet();
    m_folioBar = bar();
    m_breakdown = quiet();
    m_breakdown->setTextFormat(Qt::RichText);
    m_breakdown->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(m_breakdown, &QLabel::linkActivated, this, [this](const QString &link) {
        showNextRefused(link.section(QLatin1Char(':'), 1).toInt());
    });
    m_setSaid = quiet();
    m_setSaid->setTextFormat(Qt::RichText);
    m_setSaid->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(m_setSaid, &QLabel::linkActivated, this, [this] {
        m_controller->showTrainingSet(this);
        refresh();
    });
    m_setBar = bar();
    m_state = quiet();
    inner->addWidget(m_folioSaid);
    inner->addWidget(m_folioBar);
    inner->addWidget(m_breakdown);
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
    // The set's files changed — a save, a withdrawal, a folio taken out in the
    // set's own window. Nothing in the document records that, so nothing else
    // would make the panel look again.
    connect(m_controller, &TranscriptionController::trainingSetChanged, this, later);

    refresh();
}

void TranscriptionTrainingWidget::showNextRefused(int reason)
{
    const QList<LineVerdict> verdicts = m_controller->trainingProgress().verdicts;
    // Counting on from the caret, so that pressing the link again walks the
    // folio rather than going back to the first one every time.
    const QPair<int, int> from{m_controller->selectedVerse(), m_controller->selectedColumn()};
    const LineVerdict *first = nullptr;
    const LineVerdict *next = nullptr;
    for (const LineVerdict &verdict : verdicts) {
        if (verdict.isTaught() || int(verdict.reason) != reason || verdict.verse < 0) {
            continue;
        }
        if (!first) {
            first = &verdict;
        }
        if (!next && QPair<int, int>{verdict.verse, verdict.word} > from) {
            next = &verdict;
        }
    }
    const LineVerdict *chosen = next ? next : first;
    if (chosen) {
        m_controller->showWord(chosen->verse, chosen->word);
    }
}

void TranscriptionTrainingWidget::offerUpdate(
    int page, const QString &entry, const QString &label)
{
    m_noticePage = page;
    m_noticeEntry = entry;
    m_noticeSaid->setText(
        QStringLiteral("<b>%1</b> has changed since it was saved, and the training "
                       "set still teaches the older reading.")
            .arg(label.toHtmlEscaped()));
    m_notice->show();
}

void TranscriptionTrainingWidget::hideNotice()
{
    m_noticePage = -1;
    m_noticeEntry.clear();
    m_notice->hide();
}

void TranscriptionTrainingWidget::refresh()
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        m_folioSaid->setText(QStringLiteral("No folio open."));
        m_folioBar->setRange(0, 1);
        m_folioBar->setValue(0);
        m_breakdown->clear();
        m_breakdown->hide();
        m_setSaid->clear();
        m_lastPage = -1;
        m_lastBehind = false;
        hideNotice();
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
        m_train->setVisible(TrainingSet::isTrainable(set));
        return;
    }

    // Both numbers out of one call, so nothing can put two different ideas of
    // what a line is either side of an "of" — which is how this came to read
    // "35 of 31" on a folio whose lines had been split.
    const FolioProgress progress = m_controller->trainingProgress();
    m_folioSaid->setText(
        !progress.pictured
            ? QStringLiteral("This folio — its picture is not loaded")
        : progress.lines > 0
            ? QStringLiteral("This folio — %1 of %2 line(s) read")
                  .arg(progress.finished)
                  .arg(progress.lines)
            : QStringLiteral("This folio — not read yet"));
    m_folioBar->setRange(0, std::max(1, progress.lines));
    m_folioBar->setValue(progress.finished);

    // Which lines are not taught, and why — each kind a link to the next line
    // of that kind. Only once there is something taught or refused to speak
    // of; a folio nothing has read has no lines to sort.
    {
        int ready = 0;
        int trimmed = 0;
        QMap<int, int> refused;
        for (const LineVerdict &verdict : progress.verdicts) {
            if (verdict.state == LineVerdict::State::Ready) {
                ++ready;
            } else if (verdict.state == LineVerdict::State::Trimmed) {
                ++trimmed;
            } else {
                refused[int(verdict.reason)] += 1;
            }
        }
        QStringList parts;
        if (ready > 0) {
            parts << QStringLiteral("%1 ready").arg(ready);
        }
        if (trimmed > 0) {
            parts << QStringLiteral("%1 trimmed of a note").arg(trimmed);
        }
        for (auto kind = refused.constBegin(); kind != refused.constEnd(); ++kind) {
            parts << QStringLiteral("<a href=\"reason:%1\">%2 %3</a>")
                         .arg(kind.key())
                         .arg(kind.value())
                         .arg(describe(LineVerdict::Reason(kind.key())).toHtmlEscaped());
        }
        m_breakdown->setText(parts.join(QStringLiteral(" · ")));
        m_breakdown->setToolTip(QStringLiteral(
            "What training will make of this folio's lines. Click a reason to go "
            "to the next line held back for it."));
        m_breakdown->setVisible(!refused.isEmpty() || trimmed > 0);
    }

    const TrainingSet::Set set =
        TrainingSet::contentsOf(TrainingSet::slugFor(m_controller->metadata()));
    const QString name =
        set.label.isEmpty() ? TrainingSet::labelFor(m_controller->metadata()) : set.label;
    m_setSaid->setText(
        QStringLiteral("%1 — %2 of %3 line(s) saved%4")
            .arg(name.toHtmlEscaped())
            .arg(set.lines)
            .arg(TrainingSet::EnoughLines)
            .arg(set.folios > 0 ? QStringLiteral(" · <a href=\"set\">what's in it…</a>")
                                : QString()));
    m_setBar->setRange(0, TrainingSet::EnoughLines);
    m_setBar->setValue(std::min(set.lines, TrainingSet::EnoughLines));

    const TrainingSet::Saved saved = m_controller->trainingSaved();
    // Held in the set but nothing finished on it any more: what the set teaches
    // of this folio is a reading the folio no longer stands behind.
    const bool withdrawable = progress.pictured && progress.finished == 0 && saved.present;

    // Leaving a folio the set is behind on is the moment the correction gets
    // forgotten, so it is said then — about that folio, with the means to put
    // it right, while the transcriber is already looking at the next one.
    const int index = m_controller->currentPageIndex();
    const QList<TranscribedPage> &pages = m_controller->document().pages;
    // Still the same folio at that place — not one of another document opened
    // since, which would have the panel offering to save a folio nobody left.
    const bool lastStillThere = m_lastPage >= 0 && m_lastPage < pages.size()
        && pages.at(m_lastPage).imageEntry == m_lastEntry;
    if (index != m_lastPage) {
        if (lastStillThere && m_lastBehind) {
            offerUpdate(m_lastPage, m_lastEntry, m_lastLabel);
        } else if (index == m_noticePage) {
            // Back on the folio the notice is about; the panel says it below.
            hideNotice();
        }
    }
    m_lastPage = index;
    m_lastEntry = page->imageEntry;
    m_lastLabel = page->imageLabel.isEmpty() ? page->imageName : page->imageLabel;
    m_lastBehind = saved.stale || withdrawable;
    if (index == m_noticePage && !m_lastBehind) {
        hideNotice();
    }

    // The path, in the order it is walked. Whichever step this folio is at, the
    // button is that step — the panel is a second route to what the toolbar and
    // the right-click already do, never a second way of doing it.
    if (!progress.pictured) {
        m_step = Step::Nothing;
        m_state->setText(QStringLiteral("Its lines cannot be measured until the picture "
                                        "is here. Turn to another folio and back once "
                                        "it has downloaded."));
        m_do->setText(QStringLiteral("Save this folio"));
    } else if (withdrawable) {
        m_step = Step::Withdraw;
        m_state->setText(QStringLiteral("⚠ Nothing on it is finished any more, but the "
                                        "set still holds %1 line(s) of it from before.")
                             .arg(saved.lines));
        m_do->setText(QStringLiteral("Take it out of the set"));
        m_do->setToolTip(QStringLiteral(
            "Removes this folio's old lines from %1's training set, so that a "
            "model is not taught a reading you have since taken back.")
                             .arg(name));
    } else if (!m_controller->hasRecognisedWords()) {
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
    } else if (TrainingSet::isTrainable(set)) {
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
    if (!progress.pictured) {
        m_do->setEnabled(false);
    }

    // Offered whenever the hand has enough, whatever this folio is doing — and
    // hidden when the button above it already says the same thing.
    m_train->setVisible(TrainingSet::isTrainable(set) && m_step != Step::Train);
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
    case Step::Withdraw:
    case Step::Nothing:
        m_controller->saveFolioForTraining();
        break;
    }
    // Straight away rather than on the timer: something has just been pressed
    // and the button's own label is one of the things that changes.
    refresh();
}

} // namespace milah
