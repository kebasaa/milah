#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTimer;

namespace milah {

/// What a long-running thing looks like while it runs: what it is doing, how far
/// along it is if that can be known, how long it has been going, and its last
/// line of output.
///
/// One widget, shared by the setup dialog and the model chooser, because the
/// rule they both have to obey is the same: **no button may be pressed and then
/// sit there.** A press that produces nothing on screen is indistinguishable
/// from a press that did nothing, and the transcriber presses it again.
///
/// The elapsed clock is here on purpose. Downloading a model reports real bytes
/// and gets a real percentage; `uv` and `pip` report nothing usable, and for
/// those the honest answer is which step is running and how long it has been
/// running for — not a percentage invented to fill the bar.
class HtrProgress final : public QWidget
{
    Q_OBJECT

public:
    /// What the numbers count, which decides how they are read out.
    enum class Unit
    {
        Items,
        Bytes,
    };

    explicit HtrProgress(QWidget *parent = nullptr);

    /// Starts reporting `what`, with the bar busy until a total arrives.
    void begin(const QString &what);
    /// One step of a sequence whose steps have no measurable size — the bar
    /// fills by step rather than by work, which it says in as many words.
    void beginStep(const QString &what, int step, int count);
    /// Real numbers from the thing being waited on. A zero or negative total
    /// leaves the bar busy rather than showing a false 100%.
    void setProgress(qint64 done, qint64 total, Unit unit = Unit::Items);
    /// The last thing the process said, under the bar.
    void setDetail(const QString &line);
    /// Stops the clock and either says how it went or clears itself.
    void finish(const QString &what = QString());

    bool isRunning() const { return m_running; }

private:
    void refresh();
    /// "1:23", or "14 minutes" once that stops being worth counting in seconds.
    static QString elapsedText(qint64 milliseconds);
    static QString amountText(qint64 done, qint64 total, Unit unit);

    QLabel *m_label = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_detail = nullptr;

    QElapsedTimer m_clock;
    QTimer *m_tick = nullptr;

    QString m_what;
    bool m_running = false;
    qint64 m_done = -1;
    qint64 m_total = -1;
    Unit m_unit = Unit::Items;
    int m_step = 0;
    int m_steps = 0;
};

} // namespace milah
