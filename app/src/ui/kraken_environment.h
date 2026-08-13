#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace milah {

/// Where kraken lives, how to reach it, and how to put it there or take it away.
///
/// kraken is a Python program that runs on Linux and macOS and not on Windows,
/// so on Windows it lives inside WSL and every command goes through `wsl.exe`.
/// That is the whole of the platform difference, and it is confined here: the
/// rest of Milah asks for an invocation() and a pathFor() and never learns which
/// side of the line it is on.
///
/// Nothing is installed into a system Python. The venv sits in a directory of
/// Milah's own, which is what makes taking it away again the deletion of one
/// folder rather than an argument with pip — and an application that installs
/// three or four gigabytes ought to be able to take them back.
class KrakenEnvironment
{
public:
    enum class State
    {
        /// Windows with no WSL2. The one state that needs administrator rights
        /// and a restart to leave.
        NoSubsystem,
        /// `wsl --install` has run and Windows has not been restarted, so every
        /// `wsl` command still answers with an error. Without this the machine
        /// looks exactly as it did before, and the dialog would offer to
        /// install WSL2 a second time.
        AwaitingRestart,
        /// The subsystem is there; kraken is not.
        NotInstalled,
        /// kraken is there with nothing to recognise Hebrew with.
        NoModel,
        Ready,
    };

    KrakenEnvironment();

    /// What was found the last time anything looked. Cheap; ask refresh() for a
    /// fresh answer, which on Windows costs a `wsl.exe` launch.
    State state() const { return m_state; }
    State refresh();

    /// Whether there is anything on disk to delete, working or not.
    ///
    /// Deliberately not `state() >= NotInstalled`: an installation that stopped
    /// halfway leaves a folder and no kraken, and that is exactly when being
    /// able to clear it out matters, since the half-made virtual environment is
    /// what the next attempt trips over.
    bool hasInstallation() const { return m_hasFiles; }

    /// A sentence for the setup dialog and the status line.
    static QString describe(State state);

    /// Whether this platform needs WSL at all. False on Linux, where the whole
    /// subsystem question does not arise.
    static bool usesSubsystem();

    /// The program and leading arguments that run something inside the
    /// environment: a plain `/bin/sh -c` on Linux, and the same behind
    /// `wsl.exe -d <distro> --` on Windows. Append the shell line to run.
    QStringList shellInvocation() const;

    /// `line` run inside the environment, as a program and argument list ready
    /// for QProcess.
    QStringList commandFor(const QString &line) const;

    /// A local path as the environment will see it: unchanged on Linux, and
    /// `C:\folio.jpg` → `/mnt/c/folio.jpg` under WSL.
    ///
    /// The conversion is the drive-letter rule `wslpath` applies, done here
    /// rather than by asking `wslpath`, which would cost a process launch to
    /// answer a question with one right answer. It has no answer for a network
    /// path — and needs none, because the only paths handed to it are files
    /// Milah has just written to the local temporary directory.
    static QString pathFor(const QString &local);

    /// Writes the model-repository helper to the temporary directory and
    /// returns its path *as the environment sees it*. Empty when it could not
    /// be written.
    ///
    /// A Python script rather than a `kraken list`, because kraken's own
    /// listing is drawn for a terminal with `rich` — a table of box characters,
    /// which is a fine thing to read and a hopeless thing to parse or to click
    /// on. The library underneath it, htrmopo, hands over the records
    /// themselves and takes a progress callback while it fetches them. See
    /// listCommand().
    ///
    /// Through a file for the same reason openConsole() uses one: a multi-line
    /// script as a `-c` argument would be quoted by Qt for CreateProcess, then
    /// by wsl, then by the shell.
    QString writeHelperScript() const;

    /// Asks the repository what it holds. Writes one JSON object on stdout and
    /// `MILAH-PROGRESS <done> <total>` lines on stderr — two channels, so that
    /// a progress line can never land in the middle of the payload.
    QStringList listCommand(const QString &script) const;
    /// Downloads `doi` into the models folder, reporting progress the same way.
    /// Answers with the path of the model file it settled on.
    QStringList fetchCommand(const QString &script, const QString &doi) const;

    /// The prefix a progress line carries on stderr, so the two readers agree.
    static const char *progressMarker() { return "MILAH-PROGRESS"; }
    static const char *errorMarker() { return "MILAH-ERROR"; }

    /// The command that reads one folio into an ALTO file, as a program and
    /// argument list ready for QProcess. Both paths are local; the conversion
    /// into what the environment sees is done here.
    ///
    /// `kraken -a -i <image> <alto> segment -bl ocr -m <model>`: `-a` asks for
    /// ALTO, which is the format that carries a box per word, and `segment -bl`
    /// is baseline segmentation, which is what these hands need.
    QStringList recognitionCommand(const QString &image, const QString &alto) const;

    /// The directory everything is installed into, as the environment sees it.
    QString rootDirectory() const;
    /// The kraken binary inside the venv, as the environment sees it.
    QString krakenBinary() const;

    /// `relative`, under the root, quoted for a shell line.
    ///
    /// Quoted here rather than at every use because the two platforms need
    /// different quotes and getting it wrong is silent: the root under WSL is
    /// `$HOME/.milah-htr`, which has to be left expandable, while on Linux it
    /// is an absolute path that must not be touched at all. Single quotes
    /// would make the first a directory literally called `$HOME`.
    QString quotedPath(const QString &relative = QString()) const;

    /// One model that has been installed: where it is, and what to call it in a
    /// menu.
    struct InstalledModel
    {
        /// As the environment sees it, which under WSL is not what Windows
        /// calls it.
        QString path;
        /// The repository's summary for a downloaded model, the file's own name
        /// for one chosen off the disk.
        QString label;
    };

    /// The model that runs when Transcribe is pressed. Empty until one has been
    /// chosen.
    ///
    /// Static, along with the three below, and that is the point: they read
    /// QSettings and nothing else, so the toolbar's model menu can be filled
    /// without constructing a KrakenEnvironment — which probes WSL and costs
    /// about a second. A dropdown that hesitates before opening is one nobody
    /// uses twice.
    static QString modelPath();
    /// Makes `path` the one that runs. Does not add it to the list; see
    /// rememberModel().
    static void setModelPath(const QString &path);

    /// Every model installed, newest first. Kept beside modelPath() rather than
    /// derived from the models folder, because a model chosen off the disk
    /// lives wherever its owner keeps it.
    static QList<InstalledModel> installedModels();
    /// Adds a model to the list and makes it the one that runs. Choosing the
    /// same path twice moves it to the front rather than listing it again.
    static void rememberModel(const QString &path, const QString &label);
    /// Forgets every model. Called when the installation is deleted, so that
    /// the menu does not go on offering files that have gone.
    static void forgetModels();
    /// Drops one from the list. Where it was the model that runs, the first of
    /// those left takes over — leaving nothing running when something is still
    /// installed would be a worse answer than choosing for them.
    static void forgetModel(const QString &path);

    /// Where the last recognition run is kept: the folio handed over, the
    /// command, what Kraken said, and the layout file that came back.
    ///
    /// A folder rather than a QTemporaryDir, and that is the point. The
    /// temporary directory died with the function that made it, so when a run
    /// produced nothing there was nothing whatever to look at — not the
    /// command, not the output, not the file. One run's worth, replaced each
    /// time, on the Windows side where Milah can read it without asking WSL.
    static QString lastRunDirectory();

    /// The DOIs of models this Kraken has been shown, by trying, that it cannot
    /// load. The repository holds recognition models for more than one program,
    /// and no field in a record reliably says which — the Party base model
    /// declares Kraken and is not one. Having tried is the only proof there is,
    /// so the result of trying is kept.
    static QStringList unusableModels();
    static void rememberUnusable(const QString &doi);

    /// A folder under `models/` that no installed model points at.
    struct UnusedDownload
    {
        /// As the environment sees it.
        QString directory;
        qint64 bytes = 0;
    };

    /// Downloads that are taking up room and doing nothing.
    ///
    /// A download interrupted by a kill or a power cut leaves its folder behind,
    /// and — since the model never reached the installed list — nothing in the
    /// interface could otherwise see it, let alone remove it. That is exactly
    /// how 409 MB of a rejected Party model came to sit unreachable.
    QList<UnusedDownload> unusedDownloads() const;
    /// Deletes them all. Returns how many bytes went.
    qint64 deleteUnusedDownloads() const;

    /// Deletes a model Milah downloaded, and refuses to touch one it did not.
    ///
    /// True when the files were deleted, false when the model lives outside
    /// Milah's own folder and is therefore somebody's own file rather than
    /// Milah's to remove. Both are ordinary outcomes; the caller says which
    /// happened before it happens.
    bool deleteModelFiles(const QString &path) const;

    /// The one thing the unprivileged install cannot supply for itself.
    ///
    /// Milah brings its own Python — see installSteps() — so the distribution
    /// is asked for almost nothing. What it cannot bring is the means of
    /// fetching the first file, which is why this is only ever about curl.
    struct Missing
    {
        /// What is not there, in words, for the message.
        QString what;
        /// The package that would supply it, e.g. `curl`. Empty when nothing
        /// is missing and the failure was something else.
        QString package;
    };
    Missing findMissing() const;

    /// Runs `line` in a console window the transcriber can see and type into.
    ///
    /// The whole point is the password prompt: `sudo` asks for the distro's own
    /// Linux password, and piped through QProcess that prompt is invisible and
    /// the install merely appears to hang. False when no console could be
    /// opened, which must be reported rather than left looking like nothing
    /// happened.
    bool openConsole(const QString &line) const;

    /// One thing the installer does. Kept as a list rather than run here so the
    /// dialog can name each step as it starts it and show what it printed —
    /// a five-minute pip install behind a spinner is indistinguishable from a
    /// hang.
    struct Step
    {
        /// What to tell the transcriber is happening.
        QString label;
        QString program;
        QStringList arguments;
        /// True when this can fail because the distro is missing a package that
        /// only root can add. The dialog answers that with a console window the
        /// password prompt can be seen in, rather than with an error.
        bool mayNeedRoot = false;
    };

    /// Everything that has to happen to get from `state()` to Ready, in order.
    /// Empty when there is nothing to do.
    ///
    /// Does not include installing WSL2 itself: that needs elevation and a
    /// restart, so it is its own thing — see installSubsystem().
    QList<Step> installSteps() const;

    /// Asks Windows to install WSL2, which raises Windows' own elevation
    /// prompt. False when the transcriber declined it, which is not an error
    /// and deserves no dialog: they have just said no, and know.
    ///
    /// Never reboots anything. On success the machine needs a restart before
    /// `wsl` will answer, which is what AwaitingRestart records.
    bool installSubsystem();

    /// How much the installation weighs, in bytes; -1 when it cannot be
    /// measured. Asked before removing it, because disk is usually the reason.
    qint64 installedBytes() const;

    /// Deletes the venv and the models. Not WSL2, not the distro, not any
    /// Python: those were here before Milah took an interest and are very
    /// likely being used for something else.
    bool remove(QString *error);

private:
    /// The distro `wsl.exe` commands are addressed to. Empty on Linux, and
    /// empty on Windows when there is none — which is NoSubsystem.
    QString m_distro;
    State m_state = State::NotInstalled;
    bool m_hasFiles = false;

    /// Whether `wsl --install` has been run and the restart has not happened.
    /// Kept in QSettings rather than worked out, because between the install
    /// and the restart the machine gives the same answers it gave before it.
    static bool awaitingRestart();
    static void setAwaitingRestart(bool waiting);

    /// The first distro `wsl -l -q` lists, or empty when the command fails.
    static QString firstDistro();
    /// Runs `line` inside the environment and waits. Returns the exit code, or
    /// -1 when the process could not be started or timed out.
    int run(const QString &line, QString *output = nullptr) const;
};

} // namespace milah
