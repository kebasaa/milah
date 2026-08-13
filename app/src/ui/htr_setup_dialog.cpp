#include "ui/htr_setup_dialog.h"

#include "ui/htr_progress.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Written out rather than measured. Nothing upstream states a figure, and
/// nearly all of it is PyTorch, so this is an estimate said as one.
///
/// ASCII, and the hyphen is the point. QLatin1String means one byte, one
/// character, so the en dash this used to hold arrived on screen as the three
/// Latin-1 characters of its UTF-8 encoding: `3â€"4 GB`. A hyphen cannot be got
/// wrong by any encoding, and reads the same in a dialog, a log and a terminal.
const QLatin1String kDownloadSize("3-4 GB");

QString requirementsText(const KrakenEnvironment &environment)
{
    const QString where = KrakenEnvironment::usesSubsystem()
        ? QStringLiteral("inside WSL, in a folder of Milah's own")
        : QStringLiteral("in Milah's own application-data folder");

    return QStringLiteral(
               "<p>Kraken reads handwriting. It is a Python program, so Milah "
               "installs it %1 — with a Python of its own, downloaded for the "
               "purpose, so nothing depends on what your distribution ships and "
               "nothing is added to it.</p>"
               "<ul>"
               "<li>About <b>%2</b> is downloaded, nearly all of it PyTorch.</li>"
               "<li>A folio takes <b>minutes</b> on a processor and seconds on a "
               "graphics card.</li>"
               "<li>File ▸ Handwriting recognition ▸ Remove Kraken… takes it all "
               "away again. WSL2 and your Linux distribution are left alone.</li>"
               "</ul>"
               "<p>%3</p>")
        .arg(where, QString(kDownloadSize), KrakenEnvironment::describe(environment.state()));
}

} // namespace

HtrSetupDialog::HtrSetupDialog(KrakenEnvironment *environment, QWidget *parent)
    : QDialog(parent)
    , m_environment(environment)
{
    setWindowTitle(QStringLiteral("Handwriting recognition"));
    setModal(true);
    resize(680, 560);

    m_question = new QLabel;
    m_question->setWordWrap(true);
    QFont heading = m_question->font();
    heading.setBold(true);
    heading.setPointSizeF(heading.pointSizeF() * 1.15);
    m_question->setFont(heading);

    m_requirements = new QLabel;
    m_requirements->setWordWrap(true);
    m_requirements->setTextFormat(Qt::RichText);

    m_progress = new HtrProgress;

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    // Its output rather than a spinner: a silent five-minute progress bar is
    // indistinguishable from a hang, and pip has plenty to say for itself.
    m_log->setPlaceholderText(
        QStringLiteral("What the installer prints will appear here."));

    m_installButton = new QPushButton(QStringLiteral("Install"));
    m_installButton->setDefault(true);
    connect(m_installButton, &QPushButton::clicked, this, &HtrSetupDialog::startInstall);
    m_closeButton = new QPushButton(QStringLiteral("Cancel"));
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);

    auto *buttons = new QDialogButtonBox;
    buttons->addButton(m_installButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(m_closeButton, QDialogButtonBox::RejectRole);

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(m_question);
    outer->addWidget(m_requirements);
    outer->addWidget(m_progress);
    outer->addWidget(m_log, 1);
    outer->addWidget(buttons);

    updateForState();
}

bool HtrSetupDialog::isReady() const
{
    return m_environment->state() == KrakenEnvironment::State::Ready;
}

QString HtrSetupDialog::question() const
{
    switch (m_environment->state()) {
    case KrakenEnvironment::State::NoSubsystem:
        // The only case that names WSL2, because it is the only case in which
        // saying yes installs it.
        return QStringLiteral(
            "Install WSL2 and Kraken to automatically attempt transcriptions?");
    case KrakenEnvironment::State::AwaitingRestart:
        return QStringLiteral(
            "Restart Windows to finish installing WSL2, then press Transcribe "
            "again.");
    case KrakenEnvironment::State::NotInstalled:
        return QStringLiteral(
            "Install Kraken to automatically attempt transcriptions?");
    case KrakenEnvironment::State::NoModel:
        // Installed, and this dialog's work is therefore done. Choosing a model
        // is a different job in a different window — see HtrModelsDialog.
        return QStringLiteral(
            "Kraken is installed. Choose a model under File ▸ Handwriting "
            "recognition ▸ Manage models…");
    case KrakenEnvironment::State::Ready:
        return QStringLiteral("Kraken is ready to read a folio.");
    }
    return QString();
}

void HtrSetupDialog::updateForState()
{
    const KrakenEnvironment::State state = m_environment->state();
    m_question->setText(question());
    m_requirements->setText(requirementsText(*m_environment));

    switch (state) {
    case KrakenEnvironment::State::NoSubsystem:
        m_installButton->setText(QStringLiteral("Install WSL2 and Kraken"));
        m_installButton->setEnabled(true);
        break;
    case KrakenEnvironment::State::AwaitingRestart:
        m_installButton->setEnabled(false);
        break;
    case KrakenEnvironment::State::NotInstalled:
        m_installButton->setText(QStringLiteral("Install Kraken"));
        m_installButton->setEnabled(true);
        break;
    case KrakenEnvironment::State::NoModel:
    case KrakenEnvironment::State::Ready:
        // Both mean Kraken is installed, which is all this dialog was for. A
        // missing model is not something to reinstall a runtime over.
        m_installButton->setEnabled(false);
        m_closeButton->setText(QStringLiteral("Close"));
        break;
    }
}

void HtrSetupDialog::startInstall()
{
    if (m_environment->state() == KrakenEnvironment::State::NoSubsystem) {
        // Windows raises its own elevation prompt from here. Declining it is a
        // decision rather than a fault, so it gets no error box — the dialog
        // simply stays as it was, offering to try again.
        if (!m_environment->installSubsystem()) {
            appendLog(QStringLiteral(
                "WSL2 was not installed. Nothing on this machine has changed."));
            return;
        }
        QMessageBox::information(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("WSL2 is being installed. <b>Windows has to be "
                           "restarted</b> before it will answer.<p>Milah will "
                           "not restart it for you. After the restart, press "
                           "Transcribe again and the rest of the setup will "
                           "carry on from here.</p>"));
        updateForState();
        return;
    }

    m_steps = m_environment->installSteps();
    if (m_steps.isEmpty()) {
        return;
    }
    m_stepIndex = -1;
    setBusy(true);
    runNextStep();
}

void HtrSetupDialog::runNextStep()
{
    ++m_stepIndex;
    if (m_stepIndex >= m_steps.size()) {
        setBusy(false);
        m_progress->finish(QStringLiteral("Kraken is installed."));
        m_environment->refresh();
        updateForState();
        return;
    }

    const KrakenEnvironment::Step &step = m_steps.at(m_stepIndex);
    // uv and pip report no total worth having, so the bar fills by step and the
    // label says that is what it is measuring. An elapsed clock next to it is
    // the honest alternative to a percentage invented to fill the gap.
    m_progress->beginStep(step.label, m_stepIndex, m_steps.size());
    appendLog(QStringLiteral("\n== %1").arg(step.label));

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        const QString said = QString::fromUtf8(m_process->readAll());
        appendLog(said);
        // The last line beside the bar as well as in the log, so that something
        // visibly moves during the five minutes pip spends on PyTorch.
        m_progress->setDetail(said.section(QLatin1Char('\n'), -2, -1));
    });
    connect(
        m_process,
        &QProcess::finished,
        this,
        &HtrSetupDialog::stepFinished);
    m_process->start(step.program, step.arguments);
}

void HtrSetupDialog::stepFinished(int code, QProcess::ExitStatus status)
{
    const KrakenEnvironment::Step step = m_steps.value(m_stepIndex);
    m_process->deleteLater();
    m_process = nullptr;

    if (status == QProcess::CrashExit) {
        // Which is also what Cancel looks like, because Cancel kills it.
        appendLog(QStringLiteral("\nStopped."));
        setBusy(false);
        m_progress->finish(QStringLiteral("Stopped. What was installed is left "
                                          "where it is."));
        m_environment->refresh();
        updateForState();
        return;
    }

    if (code != 0) {
        appendLog(QStringLiteral("\nThat step failed (exit code %1).").arg(code));
        setBusy(false);
        m_progress->finish(
            QStringLiteral("%1 — failed. What it printed is below.").arg(step.label));
        if (step.mayNeedRoot) {
            offerRootHandoff(step);
        }
        m_environment->refresh();
        updateForState();
        return;
    }

    runNextStep();
}

void HtrSetupDialog::offerRootHandoff(const KrakenEnvironment::Step &step)
{
    Q_UNUSED(step)

    const KrakenEnvironment::Missing missing = m_environment->findMissing();

    if (missing.package.isEmpty()) {
        // Nothing the distribution is short of, so the step failed for its own
        // reasons and the log is the only honest thing to point at. Inventing
        // an apt line here would send somebody to install what they have.
        QMessageBox::warning(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("That step failed, and not for anything Milah can "
                           "name — nothing it needs from the distribution is "
                           "missing. What the installer printed is below."));
        return;
    }

    // The only thing root is ever wanted for now that Milah brings its own
    // Python: something has to fetch the first file. `sudo` asks for the
    // distribution's own Linux password, which is not the Windows one, and
    // piped through QProcess that prompt is invisible and the install merely
    // appears to hang — so Milah never runs sudo blind.
    const QString command =
        QStringLiteral("sudo apt install -y %1").arg(missing.package);

    const QString why =
        QStringLiteral("Milah downloads everything else itself, including the "
                       "Python that Kraken needs — but <code>%1</code> is not "
                       "installed, and something has to fetch the first file.")
            .arg(missing.what);

    const QString linux = QStringLiteral(
        "<p>%1</p><p>That needs your <b>Linux</b> password — not your Windows "
        "one%2.</p>");

    if (!KrakenEnvironment::usesSubsystem()) {
        // A Linux user with a package manager needs the line, not a window
        // driven for them.
        QMessageBox::warning(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("%1<p>Run:</p><pre>%2</pre><p>then press Install "
                           "again.</p>")
                .arg(why, command));
        return;
    }

    if (QMessageBox::question(
            this,
            QStringLiteral("Milah"),
            linux.arg(why, QStringLiteral(", and Milah cannot ask for it. It "
                                          "will open a console window where you "
                                          "can"))
                + QStringLiteral("<p>Open it now?</p>"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Yes)
        != QMessageBox::Yes) {
        return;
    }

    const QString line = QStringLiteral(
                             "echo 'Milah is installing what Kraken needs.'\n"
                             "echo\n"
                             "%1\n"
                             "echo\n"
                             "echo 'Done. Press Enter to close this window, then "
                             "press Install again in Milah.'\n"
                             "read -r _\n")
                             .arg(command);

    if (!m_environment->openConsole(line)) {
        // It used to end here in silence, having promised a window that never
        // came. A promise Milah cannot keep has to be taken back out loud.
        QMessageBox::warning(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("Milah could not open a console window. Open a "
                           "terminal in your distribution yourself and run:"
                           "<pre>%1</pre>then press Install again.")
                .arg(command));
        return;
    }

    appendLog(QStringLiteral(
                  "A console window is open. Run through it, then press "
                  "Install again.\n    %1")
                  .arg(command));
}




void HtrSetupDialog::appendLog(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    m_log->appendPlainText(text.trimmed());
}

void HtrSetupDialog::setBusy(bool busy)
{
    m_installButton->setEnabled(!busy);
    m_closeButton->setText(busy ? QStringLiteral("Stop") : QStringLiteral("Close"));
}

void HtrSetupDialog::closeEvent(QCloseEvent *event)
{
    if (!m_process) {
        QDialog::closeEvent(event);
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Milah"),
        QStringLiteral("The installation is still running. Stop it?<p>What has "
                       "been installed so far is left where it is, and pressing "
                       "Install again carries on from there.</p>"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        event->ignore();
        return;
    }

    m_process->kill();
    m_process->waitForFinished(3000);
    QDialog::closeEvent(event);
}

} // namespace milah
