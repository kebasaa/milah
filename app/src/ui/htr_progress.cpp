#include "ui/htr_progress.h"

#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace milah {
namespace {

/// Once past this, seconds stop being the useful unit and a ticking count of
/// them starts to read as a stopwatch rather than as information.
constexpr qint64 MinutesInsteadOfSeconds = 10 * 60 * 1000;

} // namespace

HtrProgress::HtrProgress(QWidget *parent)
    : QWidget(parent)
{
    m_label = new QLabel;
    m_label->setWordWrap(true);

    m_bar = new QProgressBar;
    m_bar->setTextVisible(true);

    m_detail = new QLabel;
    m_detail->setWordWrap(false);
    m_detail->setTextFormat(Qt::PlainText);
    // The last line of a subprocess can be any length at all, and a label that
    // grows the dialog to fit it would make the window jump about while it
    // works.
    m_detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont quiet = m_detail->font();
    quiet.setPointSizeF(quiet.pointSizeF() * 0.9);
    m_detail->setFont(quiet);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);
    outer->addWidget(m_label);
    outer->addWidget(m_bar);
    outer->addWidget(m_detail);

    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &HtrProgress::refresh);

    finish();
}

void HtrProgress::begin(const QString &what)
{
    m_what = what;
    m_running = true;
    m_done = -1;
    m_total = -1;
    m_step = 0;
    m_steps = 0;
    m_detail->clear();
    m_clock.start();
    m_tick->start();

    m_bar->setVisible(true);
    // Busy until something says otherwise, which is at least honest about not
    // knowing.
    m_bar->setRange(0, 0);
    m_label->setVisible(true);
    refresh();
}

void HtrProgress::beginStep(const QString &what, int step, int count)
{
    const bool sameRun = m_running;
    if (!sameRun) {
        begin(what);
    }
    m_what = what;
    m_step = step;
    m_steps = count;
    m_done = -1;
    m_total = -1;
    // By step, and the label says that is what it is measuring, so a bar at
    // three quarters is not read as three quarters of the download.
    m_bar->setRange(0, std::max(1, count));
    m_bar->setValue(std::max(0, step));
    refresh();
}

void HtrProgress::setProgress(qint64 done, qint64 total, Unit unit)
{
    m_done = done;
    m_total = total;
    m_unit = unit;
    if (total > 0) {
        // Scaled into ints, because a model is measured in bytes and a
        // QProgressBar is not.
        m_bar->setRange(0, 1000);
        m_bar->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
    } else {
        m_bar->setRange(0, 0);
    }
    refresh();
}

void HtrProgress::setDetail(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    m_detail->setText(trimmed);
    m_detail->setToolTip(trimmed);
}

void HtrProgress::finish(const QString &what)
{
    m_running = false;
    m_tick->stop();
    m_bar->setVisible(false);
    m_detail->clear();
    m_detail->setToolTip(QString());
    m_label->setText(what);
    m_label->setVisible(!what.isEmpty());
    m_what.clear();
    m_step = 0;
    m_steps = 0;
}

QString HtrProgress::elapsedText(qint64 milliseconds)
{
    const qint64 seconds = milliseconds / 1000;
    if (milliseconds >= MinutesInsteadOfSeconds) {
        return QStringLiteral("%1 minutes").arg(seconds / 60);
    }
    return QStringLiteral("%1:%2")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString HtrProgress::amountText(qint64 done, qint64 total, Unit unit)
{
    if (unit == Unit::Bytes) {
        const QLocale locale;
        return QStringLiteral("%1 of %2")
            .arg(locale.formattedDataSize(done, 1, QLocale::DataSizeTraditionalFormat),
                 locale.formattedDataSize(total, 1, QLocale::DataSizeTraditionalFormat));
    }
    return QStringLiteral("%1 of %2").arg(done).arg(total);
}

void HtrProgress::refresh()
{
    if (!m_running) {
        return;
    }

    QStringList parts{m_what};
    if (m_steps > 0) {
        parts.append(QStringLiteral("step %1 of %2").arg(m_step + 1).arg(m_steps));
    }
    if (m_total > 0 && m_done >= 0) {
        parts.append(amountText(m_done, m_total, m_unit));
        parts.append(QStringLiteral("%1%").arg(m_done * 100 / m_total));
    }
    parts.append(QStringLiteral("%1 elapsed").arg(elapsedText(m_clock.elapsed())));

    m_label->setText(parts.join(QStringLiteral(" — ")));
}

} // namespace milah
