#include "ui/kraken_environment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cstdint>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace milah {
namespace {

const QLatin1String kModelKey("htr/model");
const QLatin1String kModelsKey("htr/models");
const QLatin1String kUnusableKey("htr/unusable");
const QLatin1String kAwaitingRestartKey("htr/awaitingRestart");

/// Long enough for `wsl.exe` to start a distro that has not been used since the
/// machine was switched on, which is the slow case and takes seconds rather
/// than milliseconds. Nothing here is on a path anybody is waiting on except
/// the setup dialog, which says what it is doing while it waits.
constexpr int ProbeTimeoutMs = 30000;

/// The Python the venv is built with. kraken supports 3.10 to 3.13, and this
/// is the one with the widest coverage of built PyTorch wheels — which is what
/// actually decides whether the install finishes.
///
/// Not the distribution's own Python, whatever that is. uv downloads this one,
/// so a distribution that ships 3.14, or 3.9, or nothing, makes no difference.
const QLatin1String kPythonVersion("3.12");

/// The model-repository helper, run inside the venv.
///
/// It exists because `kraken list` prints a table drawn with `rich` — lovely in
/// a terminal, unparseable and unclickable anywhere else. htrmopo, which kraken
/// depends on and which is therefore already in the venv, hands over the
/// records themselves: `get_listing(callback)` and `get_model(doi, path,
/// callback)`, both of which report `(total, advance)` as they go. That callback
/// is where the progress bar's numbers come from.
///
/// A transport and nothing more: it fetches records and hands them over as they
/// are. Deciding which of them read Hebrew, or read anything at all, belongs to
/// the dialog — the question has a parameter now (which script?), and one rule
/// in one place beats the same rule stated in two languages.
const char *const kHelperSource = R"PY(import dataclasses
import glob
import json
import os
import sys

try:
    from htrmopo import get_listing, get_model
except Exception as problem:
    print("MILAH-ERROR htrmopo is not available: %s" % problem, file=sys.stderr)
    raise SystemExit(1)


def reporter():
    """htrmopo counts in advances; a progress bar wants a running total."""
    done = [0]

    def callback(total, advance):
        done[0] += advance or 0
        print("MILAH-PROGRESS %d %d" % (done[0], total or 0),
              file=sys.stderr, flush=True)

    return callback


def as_list(value):
    if value is None:
        return []
    if isinstance(value, (list, tuple, set)):
        return [str(item) for item in value if item is not None]
    return [str(value)]


def creator_names(data):
    names = []
    for creator in as_list_of(data.get("creators")):
        if isinstance(creator, dict):
            names.append(str(creator.get("name")
                             or creator.get("creatorName") or "").strip())
        else:
            names.append(str(creator).strip())
    return ", ".join(name for name in names if name)


def as_list_of(value):
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        return list(value)
    return [value]


def distributed(data):
    """The files a record ships, by name and size.

    The name is the point. A record's summary is prose — "Medieval Hebrew
    manuscripts version 1.0" — while the thing everybody actually calls it is
    the file: BiblIA_01. Three of the Party records share one summary between
    them and are told apart by nothing else at all.
    """
    files = []
    for item in (data.get("distribution") or []):
        if not isinstance(item, dict):
            continue
        url = str(item.get("url") or "")
        name = url.rsplit("/", 1)[-1]
        if not name:
            continue
        try:
            size = int(item.get("size") or 0)
        except (TypeError, ValueError):
            size = 0
        files.append({"name": name, "size": size})
    return files


def flatten(record):
    if dataclasses.is_dataclass(record):
        data = dataclasses.asdict(record)
    elif isinstance(record, dict):
        data = dict(record)
    else:
        data = {key: getattr(record, key)
                for key in dir(record) if not key.startswith("_")}

    metrics = {}
    for key, value in (data.get("metrics") or {}).items():
        try:
            metrics[str(key).lower()] = float(value)
        except (TypeError, ValueError):
            pass

    published = data.get("publication_date")
    return {
        "doi": str(data.get("doi") or ""),
        "concept_doi": str(data.get("concept_doi") or ""),
        # No record carries a short name, so the summary is the only thing there
        # is to put in a "Model" column.
        "summary": str(data.get("summary") or "").strip(),
        "script": as_list(data.get("script")),
        "language": as_list(data.get("language")),
        "model_type": as_list(data.get("model_type")),
        "keywords": as_list(data.get("keywords")),
        "license": str(data.get("license") or ""),
        "creators": creator_names(data),
        "metrics": metrics,
        "published": str(published or "")[:10],
        "version": str(data.get("version") or ""),
        # Shown, not filtered on. It cannot be trusted for filtering: the Party
        # base model at 10.5281/zenodo.20642057 says "kraken" here and kraken
        # cannot load it. Only trying to load it settles that — see verify().
        "software": str(data.get("software_name") or ""),
        "files": distributed(data),
    }


def do_list():
    listing = get_listing(reporter())
    models = []
    for versions in listing.values():
        if not isinstance(versions, dict):
            continue
        # Newest description of a model wins; the same work is often published
        # under both schemas.
        record = versions.get("v1") or versions.get("v0")
        if record is None:
            continue
        flat = flatten(record)
        if flat["doi"]:
            models.append(flat)
    json.dump({"models": models}, sys.stdout)
    sys.stdout.write("\n")


def verify(path):
    """Whether this kraken can actually load that file.

    The repository holds recognition models for more than one program. Party
    models sit alongside kraken's own, and kraken cannot load them — it gets as
    far as reading the file and stops at "PartyModel is not in model registry".
    Nothing in the record predicts it: the Party base model declares
    `software_name: kraken`, and a Party model and a kraken model can both be
    safetensors, so neither the field nor the file extension tells them apart.

    Trying it does. It costs seconds here and saves the transcriber finding out
    on their first folio, several minutes into a recognition that was never
    going to work.
    """
    try:
        from kraken.models.loaders import load_models
    except Exception:
        # A kraken whose innards have moved. Not knowing is not the same as
        # knowing it is broken, so say nothing rather than block the download.
        return True, ""
    try:
        load_models(path)
        return True, ""
    except Exception as problem:
        return False, str(problem).strip().replace("\n", " ")


def do_fetch(doi, into):
    target = os.path.join(into, "".join(
        character if character.isalnum() or character in "._-" else "_"
        for character in doi))
    os.makedirs(target, exist_ok=True)
    where = str(get_model(doi, path=target, callback=reporter()))

    if os.path.isfile(where):
        chosen = where
    else:
        chosen = ""
        for pattern in ("*.mlmodel", "*.safetensors"):
            found = sorted(glob.glob(os.path.join(where, "**", pattern),
                                     recursive=True))
            if found:
                chosen = found[0]
                break
        if not chosen:
            # Whatever came down, largest first. kraken will say so if it is
            # not a model, which is a better failure than Milah guessing that
            # nothing arrived.
            files = [path for path in
                     glob.glob(os.path.join(where, "**", "*"), recursive=True)
                     if os.path.isfile(path)]
            files.sort(key=os.path.getsize, reverse=True)
            chosen = files[0] if files else ""

    loadable, why = verify(chosen) if chosen else (False, "nothing was downloaded")
    json.dump({"path": chosen, "directory": where,
               "loadable": loadable, "why": why}, sys.stdout)
    sys.stdout.write("\n")


def do_check(path):
    """The same question do_fetch asks of a download, asked of a file already
    on disk — a model somebody chose themselves, or one training just made."""
    loadable, why = verify(path)
    json.dump({"path": path, "loadable": loadable, "why": why}, sys.stdout)
    sys.stdout.write("\n")


try:
    if len(sys.argv) > 1 and sys.argv[1] == "list":
        do_list()
    elif len(sys.argv) > 3 and sys.argv[1] == "fetch":
        do_fetch(sys.argv[2], sys.argv[3])
    elif len(sys.argv) > 2 and sys.argv[1] == "check":
        do_check(sys.argv[2])
    else:
        print("MILAH-ERROR unknown request", file=sys.stderr)
        raise SystemExit(2)
except SystemExit:
    raise
except Exception as problem:
    print("MILAH-ERROR %s" % problem, file=sys.stderr)
    raise SystemExit(1)
)PY";

/// A path or word made safe to put inside a `/bin/sh -c` line.
///
/// Single quotes, because inside them the shell expands nothing at all — and
/// the paths going through here include Windows user names and a Linux `$HOME`,
/// either of which may contain a character the shell would otherwise read.
QString quoted(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1String("'"), QLatin1String("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace

KrakenEnvironment::KrakenEnvironment()
{
    refresh();
}

bool KrakenEnvironment::usesSubsystem()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString KrakenEnvironment::describe(State state)
{
    switch (state) {
    case State::NoSubsystem:
        return QStringLiteral("WSL2 is not installed, so there is nowhere for "
                              "Kraken to run.");
    case State::AwaitingRestart:
        return QStringLiteral("WSL2 has been installed. Windows needs to be "
                              "restarted before it will answer.");
    case State::NotInstalled:
        return QStringLiteral("Kraken is not installed yet.");
    case State::NoModel:
        return QStringLiteral("Kraken is installed, but no recognition model "
                              "has been chosen.");
    case State::Ready:
        return QStringLiteral("Kraken is ready.");
    }
    return QString();
}

bool KrakenEnvironment::awaitingRestart()
{
    return QSettings().value(kAwaitingRestartKey, false).toBool();
}

void KrakenEnvironment::setAwaitingRestart(bool waiting)
{
    QSettings settings;
    if (waiting) {
        settings.setValue(kAwaitingRestartKey, true);
    } else {
        settings.remove(kAwaitingRestartKey);
    }
}

QString KrakenEnvironment::firstDistro()
{
#ifdef Q_OS_WIN
    QProcess wsl;
    wsl.start(QStringLiteral("wsl.exe"), {QStringLiteral("-l"), QStringLiteral("-q")});
    if (!wsl.waitForFinished(ProbeTimeoutMs) || wsl.exitCode() != 0) {
        return QString();
    }
    const QByteArray raw = wsl.readAllStandardOutput();

    // `wsl -l -q` answers in UTF-16, which is not what any other program on the
    // machine does and is the single commonest way of ending up with a distro
    // name that looks right and matches nothing. Told apart by the embedded
    // NULs rather than assumed, so that a Windows which one day stops doing it
    // is read correctly too.
    const bool utf16 = raw.contains('\0');
    QString listing = utf16
        ? QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData()),
                             raw.size() / 2)
        : QString::fromUtf8(raw);
    // And a byte-order mark, which is whitespace to nobody.
    listing.remove(QChar(0xFEFF));

    for (const QString &line : listing.split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (!name.isEmpty()) {
            return name;
        }
    }
    return QString();
#else
    return QString();
#endif
}

KrakenEnvironment::State KrakenEnvironment::refresh()
{
    if (usesSubsystem()) {
        m_distro = firstDistro();
        if (m_distro.isEmpty()) {
            // Either WSL2 was never installed, or it has been and the machine
            // has not been restarted. The two look identical from here, which
            // is exactly why the flag exists.
            m_state = awaitingRestart() ? State::AwaitingRestart : State::NoSubsystem;
            return m_state;
        }
        // It answered, so whatever restart was owed has happened.
        setAwaitingRestart(false);
    }

    // All three questions in one launch. Under WSL each of these costs a
    // second or so of starting a distribution, and they are asked every time
    // the Handwriting recognition menu is opened.
    QString found;
    run(QStringLiteral("[ -d %1 ] && echo folder\n"
                       "[ -x %2 ] && echo kraken\n"
                       "[ -f %3 ] && echo model\n"
                       "exit 0\n")
            .arg(quotedPath(),
                 quotedPath(QStringLiteral("venv/bin/kraken")),
                 quoted(modelPath())),
        &found);

    // Anything at all on disk, whether or not it works. What lets Remove
    // Kraken clear up after an installation that stopped halfway — which is
    // the state somebody is in precisely when they most need it, since a
    // half-made venv is what the next attempt trips over.
    m_hasFiles = found.contains(QLatin1String("folder"));

    if (!found.contains(QLatin1String("kraken"))) {
        m_state = State::NotInstalled;
        return m_state;
    }
    if (!found.contains(QLatin1String("model"))) {
        m_state = State::NoModel;
        return m_state;
    }

    m_state = State::Ready;
    return m_state;
}

QStringList KrakenEnvironment::shellInvocation() const
{
#ifdef Q_OS_WIN
    // --exec rather than --. `wsl.exe <command>` runs the command through the
    // distribution's *login shell*, so every line would be expanded twice —
    // once by that shell and once by the /bin/sh below it. It happened to give
    // the right answer for `$HOME` and would have given a very wrong one for
    // anything with a `$` in it that was meant to survive. --exec skips the
    // login shell, leaving exactly one shell interpreting exactly one line.
    return {QStringLiteral("wsl.exe"),
            QStringLiteral("-d"),
            m_distro,
            QStringLiteral("--exec"),
            QStringLiteral("/bin/sh"),
            QStringLiteral("-c")};
#else
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c")};
#endif
}

QStringList KrakenEnvironment::commandFor(const QString &line) const
{
    QStringList command = shellInvocation();
    command.append(line);
    return command;
}

QString KrakenEnvironment::pathFor(const QString &local)
{
#ifdef Q_OS_WIN
    const QString absolute = QDir::toNativeSeparators(QFileInfo(local).absoluteFilePath());
    // C:\Users\… → /mnt/c/Users/…
    if (absolute.size() > 2 && absolute.at(1) == QLatin1Char(':')) {
        QString converted = absolute.mid(2);
        converted.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return QStringLiteral("/mnt/%1%2")
            .arg(absolute.at(0).toLower())
            .arg(converted);
    }
    // A UNC or mapped path, which nothing here produces. Handed over unchanged
    // rather than mangled, so that if it ever does arise it fails as a file
    // that cannot be opened rather than as a file opened somewhere else.
    return absolute;
#else
    return QFileInfo(local).absoluteFilePath();
#endif
}

QString KrakenEnvironment::writeHelperScript() const
{
    const QString local =
        QDir(QDir::tempPath()).filePath(QStringLiteral("milah-htr-models.py"));
    QFile file(local);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write(kHelperSource);
    file.close();
    // As the environment will see it: under WSL the temporary directory is on
    // the Windows disk, which the distribution reaches through /mnt.
    return pathFor(local);
}

QStringList KrakenEnvironment::listCommand(const QString &script) const
{
    // PYTHONUNBUFFERED as well as the script's own flushing, so a progress line
    // cannot sit in a pipe buffer while the bar stands still.
    return commandFor(QStringLiteral("PYTHONUNBUFFERED=1 %1 %2 list")
                          .arg(quotedPath(QStringLiteral("venv/bin/python")),
                               quoted(script)));
}

QStringList KrakenEnvironment::fetchCommand(const QString &script, const QString &doi) const
{
    return commandFor(QStringLiteral("PYTHONUNBUFFERED=1 %1 %2 fetch %3 %4")
                          .arg(quotedPath(QStringLiteral("venv/bin/python")),
                               quoted(script),
                               quoted(doi),
                               quotedPath(QStringLiteral("models"))));
}

QStringList KrakenEnvironment::recognitionCommand(
    const QString &image,
    const QString &alto) const
{
    // --base-dir is what turns the bidi reordering on, and leaving it off is
    // not the neutral choice it looks like. Kraken's --reorder already defaults
    // to on, but kraken.py then does
    //
    //     if config.bidi_reordering and params['base_dir'] != 'auto':
    //         config.bidi_reordering = params['base_dir']
    //
    // and an absent --base-dir is None, which is not 'auto', so the reordering
    // is quietly replaced by None and switched off altogether. Every Hebrew
    // word then came back in display order — reversed, final letters first.
    //
    // `auto` rather than `R`: Kraken reads the direction off the first strong
    // letter of each line, so a Hebrew hand comes back right-to-left without
    // breaking a Latin-script model somebody has installed.
    return commandFor(
        QStringLiteral("%1 -a -i %2 %3 segment -bl ocr -m %4 --base-dir auto")
            .arg(quotedPath(QStringLiteral("venv/bin/kraken")),
                 quoted(pathFor(image)),
                 quoted(pathFor(alto)),
                 quoted(modelPath())));
}

QStringList KrakenEnvironment::verifyCommand(
    const QString &script, const QString &model) const
{
    return commandFor(QStringLiteral("%1 %2 check %3")
                          .arg(quotedPath(QStringLiteral("venv/bin/python")),
                               quoted(script),
                               quoted(model)));
}

QString KrakenEnvironment::trainingOutputDirectory() const
{
    return rootDirectory() + QStringLiteral("/training/out");
}

QString KrakenEnvironment::trainedModelPath(const QString &name) const
{
    return QStringLiteral("%1/models/%2/%2.mlmodel").arg(rootDirectory(), name);
}

QStringList KrakenEnvironment::trainingCommand(
    const QStringList &sets, const QString &base) const
{
    const QString work = quotedPath(QStringLiteral("training"));

    // Emptied first. A set the transcriber has since removed a folio from must
    // not go on training from the copy of it left behind last time.
    QStringList lines{
        QStringLiteral("rm -rf %1 && mkdir -p %1").arg(work),
    };
    for (const QString &set : sets) {
        lines.append(
            QStringLiteral("cp %1/* %2/").arg(quoted(pathFor(set)), work));
    }

    // -d cpu said out loud rather than left to auto: there is no CUDA here, and
    // a device that cannot be found is an error hours after the button.
    lines.append(
        QStringLiteral("%1 train -f alto --resize new -d cpu -i %2 -o %3 %4/*.xml")
            .arg(quotedPath(QStringLiteral("venv/bin/ketos")),
                 quoted(base),
                 quotedPath(QStringLiteral("training/out")),
                 work));

    return commandFor(lines.join(QStringLiteral(" && ")));
}

QStringList KrakenEnvironment::checkpointsCommand() const
{
    // The metric is in the file's own name — checkpoint_<epoch>-<metric>.ckpt —
    // so which one is best is read rather than guessed. Sorted so the last line
    // is the one to take.
    return commandFor(
        QStringLiteral(
            "for f in %1/checkpoint_*.ckpt; do "
            "[ -e \"$f\" ] || continue; "
            "m=${f##*-}; m=${m%%.ckpt}; "
            "printf '%s %s\\n' \"$m\" \"$f\"; "
            "done | sort -n")
            .arg(quotedPath(QStringLiteral("training/out"))));
}

QStringList KrakenEnvironment::convertCommand(
    const QString &checkpoint, const QString &name) const
{
    const QString folder = QStringLiteral("models/%1").arg(name);
    return commandFor(
        QStringLiteral("mkdir -p %1 && %2 convert --weights-format coreml -o %3 %4")
            .arg(quotedPath(folder),
                 quotedPath(QStringLiteral("venv/bin/ketos")),
                 quotedPath(QStringLiteral("%1/%2.mlmodel").arg(folder, name)),
                 quoted(checkpoint)));
}

QString KrakenEnvironment::rootDirectory() const
{
#ifdef Q_OS_WIN
    // Inside the distro's own home rather than on the Windows disk. Everything
    // here is read and written by Linux processes thousands of times during an
    // install, and WSL's bridge to the Windows filesystem is famously slow.
    return QStringLiteral("$HOME/.milah-htr");
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("htr"));
#endif
}

QString KrakenEnvironment::krakenBinary() const
{
    return rootDirectory() + QStringLiteral("/venv/bin/kraken");
}

QString KrakenEnvironment::quotedPath(const QString &relative) const
{
    const QString tail = relative.isEmpty() ? QString() : QLatin1Char('/') + relative;
#ifdef Q_OS_WIN
    // Double quotes, so the shell still expands $HOME. Single ones would make
    // a directory literally called `$HOME` in whatever the working directory
    // happened to be.
    return QStringLiteral("\"%1\"%2").arg(rootDirectory(), tail);
#else
    // An absolute path with nothing in it to expand, and possibly a space in
    // it, so the opposite rule applies.
    return quoted(rootDirectory()) + tail;
#endif
}

QString KrakenEnvironment::modelPath()
{
    return QSettings().value(kModelKey).toString();
}

void KrakenEnvironment::setModelPath(const QString &path)
{
    QSettings settings;
    if (path.isEmpty()) {
        settings.remove(kModelKey);
    } else {
        settings.setValue(kModelKey, path);
    }
}

QList<KrakenEnvironment::InstalledModel> KrakenEnvironment::installedModels()
{
    // JSON in one setting rather than two parallel lists, so a path and the
    // name it is known by cannot come apart — which is what happens to parallel
    // lists the first time one of them is written and the other is not.
    const QJsonArray stored =
        QJsonDocument::fromJson(QSettings().value(kModelsKey).toString().toUtf8())
            .array();

    QList<InstalledModel> models;
    models.reserve(stored.size());
    for (const QJsonValue &value : stored) {
        const QJsonObject object = value.toObject();
        InstalledModel model;
        model.path = object.value(QStringLiteral("path")).toString();
        model.label = object.value(QStringLiteral("label")).toString();
        if (!model.path.isEmpty()) {
            if (model.label.isEmpty()) {
                model.label = model.path.section(QLatin1Char('/'), -1);
            }
            models.append(model);
        }
    }
    return models;
}

void KrakenEnvironment::rememberModel(const QString &path, const QString &label)
{
    if (path.isEmpty()) {
        return;
    }

    QList<InstalledModel> models = installedModels();
    // The same model chosen twice moves to the front rather than appearing
    // twice, which is the same rule the Open Recent list follows and for the
    // same reason: a second copy takes a place from something else.
    models.removeIf([&path](const InstalledModel &model) { return model.path == path; });

    InstalledModel added;
    added.path = path;
    added.label = label.trimmed().isEmpty() ? path.section(QLatin1Char('/'), -1)
                                            : label.trimmed();
    models.prepend(added);

    QJsonArray array;
    for (const InstalledModel &model : models) {
        array.append(QJsonObject{{QStringLiteral("path"), model.path},
                                 {QStringLiteral("label"), model.label}});
    }
    QSettings().setValue(
        kModelsKey,
        QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));

    setModelPath(path);
}

void KrakenEnvironment::forgetModels()
{
    QSettings settings;
    settings.remove(kModelsKey);
    settings.remove(kModelKey);
}

void KrakenEnvironment::forgetModel(const QString &path)
{
    QList<InstalledModel> models = installedModels();
    models.removeIf([&path](const InstalledModel &model) { return model.path == path; });

    QJsonArray array;
    for (const InstalledModel &model : models) {
        array.append(QJsonObject{{QStringLiteral("path"), model.path},
                                 {QStringLiteral("label"), model.label}});
    }
    QSettings settings;
    if (array.isEmpty()) {
        settings.remove(kModelsKey);
    } else {
        settings.setValue(
            kModelsKey,
            QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    }

    // Removing the one that runs hands the job to whatever is left, rather than
    // leaving Transcribe pointed at nothing while models are still installed.
    if (modelPath() == path) {
        setModelPath(models.isEmpty() ? QString() : models.constFirst().path);
    }
}

QString KrakenEnvironment::lastRunDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString path = QDir(base).filePath(QStringLiteral("htr-last-run"));
    QDir().mkpath(path);
    return path;
}

QStringList KrakenEnvironment::unusableModels()
{
    return QSettings().value(kUnusableKey).toStringList();
}

void KrakenEnvironment::rememberUnusable(const QString &doi)
{
    if (doi.isEmpty()) {
        return;
    }
    QStringList known = unusableModels();
    if (known.contains(doi)) {
        return;
    }
    known.append(doi);
    QSettings().setValue(kUnusableKey, known);
}

QList<KrakenEnvironment::UnusedDownload> KrakenEnvironment::unusedDownloads() const
{
    QString listing;
    // Every folder under models/, with its size. Which of them are in use is
    // decided here rather than in the shell, because the answer lives in
    // QSettings and the shell has never heard of it.
    if (run(QStringLiteral("du -sb %1/*/ 2>/dev/null || true")
                .arg(quotedPath(QStringLiteral("models"))),
            &listing)
        != 0) {
        return {};
    }

    QStringList held;
    for (const InstalledModel &model : installedModels()) {
        held.append(model.path);
    }

    QList<UnusedDownload> unused;
    for (const QString &line : listing.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const qsizetype tab = line.indexOf(QLatin1Char('\t'));
        if (tab < 0) {
            continue;
        }
        bool ok = false;
        const qint64 bytes = line.left(tab).trimmed().toLongLong(&ok);
        // `du -sb dir/` prints a trailing slash, which is not how the stored
        // model paths spell their parent.
        QString directory = line.mid(tab + 1).trimmed();
        while (directory.endsWith(QLatin1Char('/'))) {
            directory.chop(1);
        }
        if (!ok || directory.isEmpty()) {
            continue;
        }

        const bool inUse = std::any_of(
            held.constBegin(), held.constEnd(), [&directory](const QString &path) {
                return path.startsWith(directory + QLatin1Char('/'));
            });
        if (!inUse) {
            unused.append({directory, bytes});
        }
    }
    return unused;
}

qint64 KrakenEnvironment::deleteUnusedDownloads() const
{
    qint64 freed = 0;
    for (const UnusedDownload &download : unusedDownloads()) {
        // Through deleteModelFiles' guard rather than around it: it takes the
        // path of a *file*, and every one of these is a directory, so a
        // sentinel inside it keeps the two-segment rule doing its work.
        if (deleteModelFiles(download.directory + QStringLiteral("/x"))) {
            freed += download.bytes;
        }
    }
    return freed;
}

bool KrakenEnvironment::deleteModelFiles(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }

    // The ownership test and the deletion in one place, inside the shell that
    // knows what $HOME is — a prefix test in C++ would be comparing an expanded
    // path against the unexpanded `$HOME/.milah-htr` that rootDirectory()
    // returns, and would answer no every time.
    //
    // Two path segments after models/ is deliberate. A single `*` would also
    // match a file sitting directly in models/, whose dirname is models/
    // itself — and the rm would then take every model there is. This is the one
    // destructive thing in the feature, so it is written to be incapable of
    // reaching past a single model's folder.
    QString said;
    run(QStringLiteral("case %1 in\n"
                       "  %2/models/*/*) rm -rf \"$(dirname %1)\" && echo deleted ;;\n"
                       "  *) echo kept ;;\n"
                       "esac\n")
            .arg(quoted(path), quotedPath()),
        &said);
    return said.contains(QLatin1String("deleted"));
}

int KrakenEnvironment::run(const QString &line, QString *output) const
{
    // Nothing can be run through a subsystem that has not been found. Without
    // this, `wsl.exe -d "" -- …` would be launched and its failure read as
    // kraken being absent, which happens to be true and for the wrong reason.
    if (usesSubsystem() && m_distro.isEmpty()) {
        return -1;
    }

    QStringList command = commandFor(line);
    const QString program = command.takeFirst();

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, command);
    if (!process.waitForFinished(ProbeTimeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        return -1;
    }
    if (output) {
        *output = QString::fromUtf8(process.readAll());
    }
    return process.exitCode();
}

KrakenEnvironment::Missing KrakenEnvironment::findMissing() const
{
    Missing missing;
    // The only thing the distribution has to supply. uv brings the Python, the
    // Python brings pip, and pip brings kraken — but something has to fetch uv
    // itself, and neither curl nor wget can be assumed on a minimal image.
    if (run(QStringLiteral(
            "command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1"))
        != 0) {
        missing.what = QStringLiteral("curl");
        missing.package = QStringLiteral("curl");
    }
    return missing;
}

bool KrakenEnvironment::openConsole(const QString &line) const
{
#ifdef Q_OS_WIN
    if (m_distro.isEmpty()) {
        return false;
    }

    // Through a file rather than as an argument. The line has quotes, `$` and
    // semicolons in it, and it would otherwise be quoted once by Qt for
    // CreateProcess, once by cmd and once by the shell — which is three chances
    // to arrive as something else.
    const QString script =
        QDir(QDir::tempPath()).filePath(QStringLiteral("milah-kraken-setup.sh"));
    QFile file(script);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(line.toUtf8());
    file.close();

    QProcess console;
    console.setProgram(QStringLiteral("wsl.exe"));
    console.setArguments({QStringLiteral("-d"),
                          m_distro,
                          QStringLiteral("--exec"),
                          QStringLiteral("/bin/sh"),
                          pathFor(script)});
    // The reason the window never appeared: a console program started from a
    // windowed one inherits CREATE_NO_WINDOW, so kraken's installer ran with
    // its password prompt addressed to nobody. This asks for a console
    // explicitly.
    console.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *arguments) {
            arguments->flags &= ~CREATE_NO_WINDOW;
            arguments->flags |= CREATE_NEW_CONSOLE;
            arguments->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        });
    if (console.startDetached()) {
        return true;
    }

    // And if that is refused, the oldest way there is of opening a console on
    // Windows. Native arguments because cmd does its own parsing and Qt's
    // quoting is for programs that do not.
    QProcess fallback;
    fallback.setProgram(QStringLiteral("cmd.exe"));
    fallback.setNativeArguments(
        QStringLiteral("/c start \"Milah\" wsl.exe -d \"%1\" --exec /bin/sh \"%2\"")
            .arg(m_distro, pathFor(script)));
    return fallback.startDetached();
#else
    // No terminal on a Linux desktop can be assumed — there are half a dozen
    // and a headless machine has none. The dialog says the command instead,
    // which a Linux user missing curl can run for themselves.
    Q_UNUSED(line)
    return false;
#endif
}

QList<KrakenEnvironment::Step> KrakenEnvironment::installSteps() const
{
    QList<Step> steps;
    if (m_state == State::NoSubsystem || m_state == State::AwaitingRestart) {
        // Nothing can be run inside a subsystem that is not answering.
        return steps;
    }

    const QString root = quotedPath();
    const QString venv = quotedPath(QStringLiteral("venv"));
    const QString uv = quotedPath(QStringLiteral("bin/uv"));

    // Everything uv would otherwise scatter about the home directory, kept
    // under the one folder. The cache matters most: PyTorch's wheels are the
    // bulk of the download and uv would keep them in ~/.cache/uv, where
    // removing Kraken would leave gigabytes behind.
    const QString uvEnv = QStringLiteral(
                              "UV_CACHE_DIR=%1/cache "
                              "UV_PYTHON_INSTALL_DIR=%1/python "
                              "UV_PYTHON_DOWNLOADS=automatic "
                              "UV_NO_MODIFY_PATH=1")
                              .arg(root);

    const auto step = [this](const QString &label, const QString &line, bool mayNeedRoot) {
        QStringList command = commandFor(line);
        Step made;
        made.label = label;
        made.program = command.takeFirst();
        made.arguments = command;
        made.mayNeedRoot = mayNeedRoot;
        return made;
    };

    // Named so the log says which machine this is happening on. The paths that
    // scroll past are Linux paths either way, and on Windows that has read as
    // Milah having ignored WSL and gone looking for a Python of its own.
    const QString where = m_distro.isEmpty()
        ? QString()
        : QStringLiteral(" in %1").arg(m_distro);

    steps.append(step(
        QStringLiteral("Making the folder Kraken will live in%1").arg(where),
        QStringLiteral("mkdir -p %1/models %1/bin %1/python %1/cache").arg(root),
        false));

    steps.append(step(
        QStringLiteral("Fetching uv, which brings its own Python%1").arg(where),
        // The step that made asking the distribution for a Python unnecessary.
        // A distribution ships whatever Python it ships — this one's is 3.14,
        // which kraken does not support and whose -venv package the archive may
        // not even carry — and none of that is the transcriber's problem to
        // solve. uv downloads a standalone CPython of the version asked for,
        // needs no root, and puts it where it is told.
        //
        // UV_UNMANAGED_INSTALL is uv's own way of saying "here, and touch
        // nothing else": no PATH edits, no shell profiles rewritten, no
        // self-updater. Removal stays the deletion of one folder.
        QStringLiteral(
            "if [ -x %1 ]; then exit 0; fi\n"
            "if command -v curl >/dev/null 2>&1; then\n"
            "  curl -LsSf https://astral.sh/uv/install.sh\n"
            "elif command -v wget >/dev/null 2>&1; then\n"
            "  wget -qO- https://astral.sh/uv/install.sh\n"
            "else\n"
            "  echo 'Neither curl nor wget is installed, so uv cannot be "
            "downloaded.' >&2\n"
            "  exit 1\n"
            "fi | env UV_UNMANAGED_INSTALL=%2/bin sh\n")
            .arg(uv, root),
        true));

    steps.append(step(
        QStringLiteral("Fetching Python %1 and creating the environment%2")
            .arg(QString(kPythonVersion), where),
        // One step rather than two. `uv python install` would do the download
        // on its own, but it also puts shims in ~/.local/bin — outside the
        // folder Milah promises to be able to delete, and the source of a
        // warning about a PATH nobody here wants changed. Asked for as part of
        // the venv, uv fetches the interpreter and leaves nothing behind it.
        //
        // --clear because a half-finished attempt leaves a venv that uv will
        // not overwrite, and the transcriber would otherwise be stuck at
        // exactly the step they had already failed at once, with no way out
        // that Milah offers them.
        QStringLiteral("env %1 %2 venv --clear --python %3 %4")
            .arg(uvEnv, uv, QString(kPythonVersion), venv),
        false));

    steps.append(step(
        // The long one. Nearly all of the three to four gigabytes is PyTorch,
        // which kraken depends on and which nobody can make smaller.
        QStringLiteral("Installing Kraken and PyTorch (this takes a while)"),
        QStringLiteral("env %1 %2 pip install --python %3/bin/python kraken")
            .arg(uvEnv, uv, venv),
        false));
    return steps;
}

bool KrakenEnvironment::installSubsystem()
{
#ifdef Q_OS_WIN
    // ShellExecute with `runas` rather than QProcess, because `wsl --install`
    // needs administrator rights and QProcess has no way to ask for them.
    // Windows raises its own prompt: the transcriber consents once to Milah and
    // once to Windows, which is the right number of times to be asked before a
    // system component is installed.
    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"runas",
        L"wsl.exe",
        L"--install",
        nullptr,
        SW_SHOWNORMAL);

    // ShellExecuteW returns a value greater than 32 on success. The one failure
    // worth telling apart is the transcriber declining the elevation prompt,
    // which is a decision and not a fault.
    const auto code = reinterpret_cast<std::intptr_t>(result);
    if (code <= 32) {
        return false;
    }

    // It will not answer until Windows has been restarted, and this is what
    // stops the next attempt offering to install it all over again.
    setAwaitingRestart(true);
    m_state = State::AwaitingRestart;
    return true;
#else
    // There is no subsystem on Linux and nothing to install.
    return false;
#endif
}

qint64 KrakenEnvironment::installedBytes() const
{
    QString output;
    // `du -sb` is coreutils, which every distro has and macOS does not — and
    // this only ever runs on Linux or inside WSL.
    if (run(QStringLiteral("du -sb %1").arg(quotedPath()), &output) != 0) {
        return -1;
    }
    bool ok = false;
    const qint64 bytes =
        output.section(QRegularExpression(QStringLiteral("\\s")), 0, 0).toLongLong(&ok);
    return ok ? bytes : -1;
}

bool KrakenEnvironment::remove(QString *error)
{
    QString output;
    // The venv and the models, and nothing else. Not WSL2, not the distro, not
    // any Python: a transcriber who asks Milah to remove Kraken has not asked
    // it to remove their Ubuntu.
    const int code = run(QStringLiteral("rm -rf %1").arg(quotedPath()), &output);
    if (code != 0) {
        if (error) {
            *error = output.trimmed().isEmpty()
                ? QStringLiteral("The folder could not be deleted.")
                : output.trimmed();
        }
        return false;
    }
    // The models went with the folder, so the menu must stop offering them.
    forgetModels();
    // And the verdicts go too. "This Kraken cannot load that" was learned about
    // the Kraken just deleted; the next one installed may be a version that
    // can, and a blocklist outliving the thing it was made against would be a
    // permanent answer to a temporary question.
    QSettings().remove(kUnusableKey);
    refresh();
    return true;
}

} // namespace milah
