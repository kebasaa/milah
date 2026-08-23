#include "ui/htr_training_dialog.h"

#include "ui/htr_progress.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Letters, digits and dashes: this becomes a directory name and a model name
/// inside the environment, and it is typed by a person.
QString asModelName(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar character : text) {
        if (character.isLetterOrNumber() || character == QLatin1Char('-')) {
            out.append(character);
        } else if (character.isSpace() && !out.isEmpty() && !out.endsWith(QLatin1Char('-'))) {
            out.append(QLatin1Char('-'));
        }
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    return out;
}

} // namespace

HtrTrainingDialog::HtrTrainingDialog(KrakenEnvironment *environment, QWidget *parent)
    : QDialog(parent)
    , m_environment(environment)
{
    setWindowTitle(QStringLiteral("Train a recognition model"));
    resize(680, 620);

    auto *heading = new QLabel(QStringLiteral(
        "Kraken's Hebrew models are trained on square bookhands. Shown a hand "
        "they have not seen — a cursive, a scribe with habits of their own — "
        "they produce noise, and the way through is to show one this hand. "
        "Below is what you have corrected and saved."));
    heading->setWordWrap(true);

    m_sets = new QListWidget;
    m_sets->setToolTip(QStringLiteral(
        "Tick everything written in the same hand. Two shelfmarks can be one "
        "scribe, and a model shown both sees more of them than a model shown "
        "either."));
    connect(m_sets, &QListWidget::itemChanged, this, [this] { updateReadiness(); });

    m_summary = new QLabel;
    m_summary->setWordWrap(true);

    m_name = new QLineEdit;
    m_name->setToolTip(QStringLiteral(
        "What the model is called in the list. Name it after the hand rather "
        "than the manuscript — it will be offered on every manuscript you open, "
        "which is the point of training it."));
    auto *nameRow = new QWidget;
    auto *nameLayout = new QVBoxLayout(nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->addWidget(new QLabel(QStringLiteral("Call the model")));
    nameLayout->addWidget(m_name);

    m_warning = new QLabel;
    m_warning->setWordWrap(true);

    m_progress = new HtrProgress;
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);

    m_train = new QPushButton(QStringLiteral("Train"));
    m_stop = new QPushButton(QStringLiteral("Stop"));
    m_openFolder = new QPushButton(QStringLiteral("Open folder"));
    connect(m_train, &QPushButton::clicked, this, &HtrTrainingDialog::start);
    connect(m_stop, &QPushButton::clicked, this, [this] {
        if (m_process) {
            // Killed, not asked. ketos catches SIGTERM and writes an abort
            // checkpoint, which is a slower way to the same place.
            m_process->kill();
        }
    });
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        const QListWidgetItem *item = m_sets->currentItem();
        const int row = item ? m_sets->row(item) : 0;
        if (row >= 0 && row < m_known.size()) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(TrainingSet::directoryOf(m_known.at(row).slug)));
        }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addButton(m_train, QDialogButtonBox::ActionRole);
    buttons->addButton(m_stop, QDialogButtonBox::ActionRole);
    buttons->addButton(m_openFolder, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(heading);
    outer->addWidget(m_sets, 1);
    outer->addWidget(m_summary);
    outer->addWidget(nameRow);
    outer->addWidget(m_warning);
    outer->addWidget(m_progress);
    outer->addWidget(m_log, 1);
    outer->addWidget(buttons);

    refreshSets();
    setBusy(false);
}

void HtrTrainingDialog::refreshSets()
{
    m_known = TrainingSet::known();
    m_sets->clear();
    for (const TrainingSet::Set &set : m_known) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 — %2 line(s) off %3 folio(s), %4")
                .arg(set.label)
                .arg(set.lines)
                .arg(set.folios)
                .arg(QLocale().formattedDataSize(set.bytes)));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // The one worth training on opens ticked, so the common case is one
        // press. Everything else is a deliberate choice about a shared hand.
        item->setCheckState(
            set.lines >= TrainingSet::EnoughLines ? Qt::Checked : Qt::Unchecked);
        m_sets->addItem(item);
    }
    updateReadiness();
}

QString HtrTrainingDialog::defaultModelName() const
{
    QStringList parts;
    for (int row = 0; row < m_sets->count(); ++row) {
        if (m_sets->item(row)->checkState() == Qt::Checked && row < m_known.size()) {
            parts.append(asModelName(m_known.at(row).label));
        }
    }
    parts.removeAll(QString());
    return parts.isEmpty() ? QStringLiteral("tuned")
                           : parts.join(QLatin1Char('-')) + QStringLiteral("-tuned");
}

void HtrTrainingDialog::updateReadiness()
{
    int lines = 0;
    int folios = 0;
    int ticked = 0;
    for (int row = 0; row < m_sets->count() && row < m_known.size(); ++row) {
        if (m_sets->item(row)->checkState() == Qt::Checked) {
            lines += m_known.at(row).lines;
            folios += m_known.at(row).folios;
            ++ticked;
        }
    }

    if (m_known.isEmpty()) {
        m_summary->setText(QStringLiteral(
            "Nothing saved yet. Correct a folio's lines, then File ▸ Handwriting "
            "recognition ▸ Save this folio for HTR training."));
    } else if (ticked == 0) {
        m_summary->setText(QStringLiteral("Nothing ticked."));
    } else {
        m_summary->setText(QStringLiteral("%1 line(s) off %2 folio(s), from %3 set(s).")
                               .arg(lines)
                               .arg(folios)
                               .arg(ticked));
    }

    const bool enough = lines >= TrainingSet::EnoughLines;
    if (m_name->text().isEmpty() || !m_name->isModified()) {
        m_name->setText(defaultModelName());
        m_name->setModified(false);
    }

    // Said before the button, not after. Committing somebody to hours without
    // telling them is the one failure a progress bar cannot make up for.
    m_warning->setText(
        enough
            ? QStringLiteral(
                  "<b>This will take hours.</b> There is no CUDA graphics card here, "
                  "so it runs on the processor. Milah stays usable, Stop leaves the "
                  "model you are using untouched, and %1 folio(s) is a fair start — "
                  "five or more is where it starts to tell.")
                  .arg(folios)
            : QStringLiteral(
                  "%1 of %2 lines. Kraken keeps a tenth of the data back to measure "
                  "the model against, so a handful of lines leaves nothing to "
                  "measure with.")
                  .arg(lines)
                  .arg(TrainingSet::EnoughLines));

    if (!m_process) {
        m_train->setEnabled(enough);
    }
    m_openFolder->setEnabled(!m_known.isEmpty());
}

void HtrTrainingDialog::start()
{
    m_chosenSets.clear();
    for (int row = 0; row < m_sets->count() && row < m_known.size(); ++row) {
        if (m_sets->item(row)->checkState() == Qt::Checked) {
            m_chosenSets.append(TrainingSet::directoryOf(m_known.at(row).slug));
        }
    }
    if (m_chosenSets.isEmpty()) {
        return;
    }

    m_modelName = asModelName(m_name->text());
    if (m_modelName.isEmpty()) {
        m_modelName = defaultModelName();
    }

    const QString base = KrakenEnvironment::modelPath();
    if (base.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Milah"),
            QStringLiteral(
                "Fine-tuning starts from a model, and none is in use.<p>Install "
                "one under Manage models… first — one marked <b>Dedicated</b> for "
                "your script is the one to start from.</p>"));
        return;
    }

    m_helperScript = m_environment->writeHelperScript();
    m_log->clear();
    appendLog(QStringLiteral("Training %1 from %2\n").arg(m_modelName, base));
    runStage(Stage::Training);
}

void HtrTrainingDialog::runStage(Stage stage)
{
    m_stage = stage;
    m_output.clear();

    QStringList command;
    switch (stage) {
    case Stage::Training:
        m_progress->begin(QStringLiteral("Training %1").arg(m_modelName));
        m_progress->setDetail(QStringLiteral("This is the long one."));
        command = m_environment->trainingCommand(m_chosenSets, KrakenEnvironment::modelPath());
        break;
    case Stage::Choosing:
        m_progress->begin(QStringLiteral("Choosing the best round"));
        command = m_environment->checkpointsCommand();
        break;
    case Stage::Converting:
        m_progress->begin(QStringLiteral("Making a model out of it"));
        command = m_environment->convertCommand(m_bestCheckpoint, m_modelName);
        break;
    case Stage::Verifying:
        m_progress->begin(QStringLiteral("Checking Kraken can load it"));
        command = m_environment->verifyCommand(m_helperScript, m_modelPath);
        break;
    case Stage::Idle:
        return;
    }

    const QString program = command.takeFirst();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        const QString text = QString::fromUtf8(m_process->readAll());
        m_output += text;
        appendLog(text);
        const QString last = text.section(QLatin1Char('\n'), -2, -1).trimmed();
        if (!last.isEmpty()) {
            m_progress->setDetail(last);
        }
    });
    connect(m_process, &QProcess::finished, this, &HtrTrainingDialog::stageFinished);
    setBusy(true);
    m_process->start(program, command);
}

void HtrTrainingDialog::stageFinished(int code, QProcess::ExitStatus status)
{
    const Stage stage = m_stage;
    m_process->deleteLater();
    m_process = nullptr;

    if (status == QProcess::CrashExit) {
        finishWith(QStringLiteral("Stopped. Nothing has been changed."), false);
        return;
    }
    if (code != 0) {
        finishWith(
            QStringLiteral("%1 failed.\n\n%2")
                .arg(
                    stage == Stage::Training  ? QStringLiteral("Training")
                        : stage == Stage::Choosing ? QStringLiteral("Reading the checkpoints")
                        : stage == Stage::Converting
                        ? QStringLiteral("Converting the checkpoint")
                        : QStringLiteral("Loading the model"),
                    m_output.trimmed().right(2000)),
            true);
        return;
    }

    switch (stage) {
    case Stage::Training:
        runStage(Stage::Choosing);
        return;

    case Stage::Choosing: {
        // Sorted by metric, so the last line is the best round. Read rather
        // than guessed: Kraken puts the number in the filename precisely so
        // that it can be.
        const QStringList lines =
            m_output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (int index = lines.size() - 1; index >= 0; --index) {
            const QString line = lines.at(index).trimmed();
            const int space = line.indexOf(QLatin1Char(' '));
            if (space > 0 && line.endsWith(QStringLiteral(".ckpt"))) {
                m_bestCheckpoint = line.mid(space + 1);
                break;
            }
        }
        if (m_bestCheckpoint.isEmpty()) {
            finishWith(
                QStringLiteral(
                    "Training finished but left no checkpoint to make a model "
                    "out of.\n\n%1")
                    .arg(m_output.trimmed().right(2000)),
                true);
            return;
        }
        appendLog(QStringLiteral("\nBest round: %1\n").arg(m_bestCheckpoint));
        m_modelPath = m_environment->trainedModelPath(m_modelName);
        runStage(Stage::Converting);
        return;
    }

    case Stage::Converting:
        runStage(Stage::Verifying);
        return;

    case Stage::Verifying: {
        // The same question a download has to answer. A model Milah made is
        // held to the standard a model Milah fetched is held to.
        const QJsonObject answer =
            QJsonDocument::fromJson(m_output.trimmed().toUtf8()).object();
        if (!answer.value(QStringLiteral("loadable")).toBool()) {
            finishWith(
                QStringLiteral("Kraken cannot load what came out of training, so it "
                               "has not been added.\n\n%1")
                    .arg(answer.value(QStringLiteral("why")).toString().isEmpty()
                             ? m_output.trimmed().right(2000)
                             : answer.value(QStringLiteral("why")).toString()),
                true);
            return;
        }

        KrakenEnvironment::rememberModel(m_modelPath, m_name->text().trimmed());
        // And run it. Remembering a model only puts it in the list, so this used
        // to end with hours of somebody's processor spent and the thing it made
        // sitting unused until they made a separate trip to a submenu — the next
        // folio still read by whatever was running before. Nobody opens this
        // window except to have a hand read better than it is being read.
        KrakenEnvironment::setModelPath(m_modelPath);
        finishWith(
            QStringLiteral("%1 is trained, and is now the model Transcribe uses. "
                           "It is offered on every manuscript, not just the ones "
                           "it learnt from, and any of the others is still one "
                           "click away under the arrow beside Transcribe.")
                .arg(m_name->text().trimmed()),
            false);
        return;
    }

    case Stage::Idle:
        return;
    }
}

void HtrTrainingDialog::finishWith(const QString &message, bool failed)
{
    m_stage = Stage::Idle;
    setBusy(false);
    m_progress->finish(failed ? QStringLiteral("Did not finish") : QString());
    appendLog(QStringLiteral("\n%1\n").arg(message));
    if (failed) {
        QMessageBox::warning(this, QStringLiteral("Milah"), message);
    } else {
        QMessageBox::information(this, QStringLiteral("Milah"), message);
    }
    refreshSets();
}

void HtrTrainingDialog::appendLog(const QString &text)
{
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->moveCursor(QTextCursor::End);
}

void HtrTrainingDialog::setBusy(bool busy)
{
    m_train->setEnabled(!busy);
    m_stop->setEnabled(busy);
    m_sets->setEnabled(!busy);
    m_name->setEnabled(!busy);
    if (!busy) {
        updateReadiness();
    }
}

void HtrTrainingDialog::closeEvent(QCloseEvent *event)
{
    if (!m_process) {
        event->accept();
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Milah"),
        QStringLiteral("Training is still running. Stop it?<p>What it has done so far "
                       "is thrown away; the model you are using is untouched either "
                       "way.</p>"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    m_process->kill();
    m_process->waitForFinished(5000);
    event->accept();
}

} // namespace milah
