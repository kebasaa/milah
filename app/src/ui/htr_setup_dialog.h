#pragma once

#include "ui/kraken_environment.h"

#include <QDialog>
#include <QList>
#include <QProcess>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace milah {

class HtrProgress;

/// The one question Milah asks before it installs anything, and the installer
/// behind it.
///
/// Raised by the first press of Transcribe, and reachable deliberately from
/// File ▸ Handwriting recognition ▸ Install Kraken… — so that setting it up can
/// be done once at a desk with time for it rather than in the middle of reading
/// a folio.
///
/// The question follows what is actually missing. Offering to install something
/// that is already there reads as though Milah has not looked, so the WSL2 half
/// of the sentence appears only on a Windows that has no WSL2.
///
/// **The runtime, and nothing else.** Models used to be chosen here too, which
/// put the way to a second model behind a dialog about installing something
/// already installed. They live in HtrModelsDialog now.
class HtrSetupDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit HtrSetupDialog(KrakenEnvironment *environment, QWidget *parent = nullptr);

    /// True when the environment came out of this dialog ready to transcribe.
    bool isReady() const;

protected:
    /// Refuses to close while an install is running, offering to stop it first:
    /// a pip killed halfway leaves a venv that is neither installed nor absent.
    void closeEvent(QCloseEvent *event) override;

private:
    /// The question, the requirements and which buttons are live, all from the
    /// environment's state. Called again after every step, so the dialog never
    /// says anything the machine has stopped agreeing with.
    void updateForState();
    QString question() const;

    void startInstall();
    void runNextStep();
    void stepFinished(int code, QProcess::ExitStatus status);
    /// What a failed step that needed root looks like from here: on Windows a
    /// console window the password prompt can be seen in, and on Linux the one
    /// command to run, because a Linux user with no python3-venv has a package
    /// manager and knows it.
    void offerRootHandoff(const KrakenEnvironment::Step &step);

    void appendLog(const QString &text);
    void setBusy(bool busy);

    KrakenEnvironment *m_environment = nullptr;

    QLabel *m_question = nullptr;
    QLabel *m_requirements = nullptr;
    HtrProgress *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;

    QPushButton *m_installButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    QList<KrakenEnvironment::Step> m_steps;
    int m_stepIndex = -1;
    QProcess *m_process = nullptr;
};

} // namespace milah
