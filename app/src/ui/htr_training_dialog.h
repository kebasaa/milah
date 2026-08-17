#pragma once

#include "ui/kraken_environment.h"
#include "ui/training_set.h"

#include <QDialog>
#include <QList>
#include <QProcess>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace milah {

class HtrProgress;

/// Teaching Kraken a hand it has never seen.
///
/// The measured problem this exists for: Kraken reads a square bookhand into
/// 60% real Hebrew Bible forms and MS Oo.1.32's Cochin cursive into 41% noise.
/// No shipped model has seen that hand, and resolution does not close the gap.
/// Fine-tuning does, and Kraken's own documentation says so.
///
/// Milah runs it rather than printing the command, because the command is
/// wrong in three different ways if written from memory: `-o` is a *directory*
/// of Lightning checkpoints in Kraken 7 rather than a model name, the best of
/// them has to be picked out by the metric in its filename, and turning it into
/// something `kraken ocr -m` will take is a second command in a different
/// serialisation format. Every one of those fails quietly.
///
/// **The sets are ticked, not chosen.** A scribe outlives a shelfmark — Oo.1.32
/// and Oo.1.16 are one hand in two bindings — so any combination can be trained
/// on together, and what comes out is named after the hand. It then joins the
/// installed models like any other and is offered on every manuscript.
class HtrTrainingDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit HtrTrainingDialog(KrakenEnvironment *environment, QWidget *parent = nullptr);

protected:
    /// Refuses to close mid-run, offering to stop first. Hours of somebody's
    /// processor should not be thrown away by a stray Escape.
    void closeEvent(QCloseEvent *event) override;

private:
    /// What the run is doing now. Three processes, one after another, and each
    /// one's failure means something different.
    enum class Stage
    {
        Idle,
        /// ketos train. The long one — hours, on this machine's processor.
        Training,
        /// Reading the checkpoints back, to find the best one.
        Choosing,
        /// ketos convert, into something kraken ocr will load.
        Converting,
        /// And trying to load it, the same check a download passes.
        Verifying,
    };

    void refreshSets();
    /// The ticked sets' totals, and whether that is enough to start.
    void updateReadiness();
    QString defaultModelName() const;

    void start();
    void runStage(Stage stage);
    void stageFinished(int code, QProcess::ExitStatus status);
    void finishWith(const QString &message, bool failed);

    void appendLog(const QString &text);
    void setBusy(bool busy);

    KrakenEnvironment *m_environment = nullptr;

    QListWidget *m_sets = nullptr;
    QLabel *m_summary = nullptr;
    QLineEdit *m_name = nullptr;
    QLabel *m_warning = nullptr;
    HtrProgress *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_train = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_openFolder = nullptr;

    QList<TrainingSet::Set> m_known;
    QProcess *m_process = nullptr;
    Stage m_stage = Stage::Idle;
    /// Kept for the stages after training: what was ticked, what to call the
    /// model, and where the pieces landed.
    QStringList m_chosenSets;
    QString m_modelName;
    QString m_helperScript;
    QString m_bestCheckpoint;
    QString m_modelPath;
    /// Everything the stage said, for reading the failure out of afterwards.
    QString m_output;
};

} // namespace milah
