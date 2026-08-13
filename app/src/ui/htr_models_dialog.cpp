#include "ui/htr_models_dialog.h"

#include "ui/htr_repository_dialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace milah {

HtrModelsDialog::HtrModelsDialog(KrakenEnvironment *environment, QWidget *parent)
    : QDialog(parent)
    , m_environment(environment)
{
    setWindowTitle(QStringLiteral("Handwriting recognition models"));
    setModal(true);
    resize(620, 380);

    m_summary = new QLabel;
    m_summary->setWordWrap(true);

    m_unused = new QLabel;
    m_unused->setWordWrap(true);
    m_cleanUpButton = new QPushButton(QStringLiteral("Clean up"));
    connect(m_cleanUpButton, &QPushButton::clicked, this, &HtrModelsDialog::cleanUpUnused);

    auto *unusedRow = new QHBoxLayout;
    unusedRow->addWidget(m_unused, 1);
    unusedRow->addWidget(m_cleanUpButton);

    m_list = new QListWidget;
    connect(m_list, &QListWidget::itemSelectionChanged, this, &HtrModelsDialog::updateButtons);
    // Double-clicking a model is the obvious way to say "use that one", and
    // obvious things should work.
    connect(m_list, &QListWidget::itemDoubleClicked, this, &HtrModelsDialog::useSelected);

    m_useButton = new QPushButton(QStringLiteral("Use selected"));
    m_useButton->setToolTip(
        QStringLiteral("Makes this the model Transcribe runs."));
    connect(m_useButton, &QPushButton::clicked, this, &HtrModelsDialog::useSelected);

    m_repositoryButton = new QPushButton(QStringLiteral("Add from repository…"));
    m_repositoryButton->setToolTip(QStringLiteral(
        "Lists what Kraken's model repository holds and downloads the one you "
        "pick. Adds to this list rather than replacing what is here."));
    connect(
        m_repositoryButton, &QPushButton::clicked, this, &HtrModelsDialog::addFromRepository);

    m_fileButton = new QPushButton(QStringLiteral("Add from file…"));
    m_fileButton->setToolTip(
        QStringLiteral("Uses a Kraken model already on this machine. The one "
                       "route that needs no network."));
    connect(m_fileButton, &QPushButton::clicked, this, &HtrModelsDialog::addFromFile);

    m_removeButton = new QPushButton(QStringLiteral("Remove…"));
    connect(m_removeButton, &QPushButton::clicked, this, &HtrModelsDialog::removeSelected);

    auto *side = new QVBoxLayout;
    side->addWidget(m_useButton);
    side->addSpacing(12);
    side->addWidget(m_repositoryButton);
    side->addWidget(m_fileButton);
    side->addWidget(m_removeButton);
    side->addStretch(1);

    auto *middle = new QHBoxLayout;
    middle->addWidget(m_list, 1);
    middle->addLayout(side);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(m_summary);
    outer->addLayout(unusedRow);
    outer->addLayout(middle, 1);
    outer->addWidget(buttons);

    refresh();
}

void HtrModelsDialog::refresh()
{
    const QString active = KrakenEnvironment::modelPath();
    const QList<KrakenEnvironment::InstalledModel> models =
        KrakenEnvironment::installedModels();

    m_list->clear();
    for (const KrakenEnvironment::InstalledModel &model : models) {
        const bool running = model.path == active;
        // A tick and bold rather than a separate column: which one runs is one
        // fact about one row, and a column of empty cells to hold it is a
        // column of empty cells.
        auto *item = new QListWidgetItem(
            running ? QStringLiteral("✓  %1").arg(model.label) : model.label);
        item->setData(Qt::UserRole, model.path);
        item->setToolTip(model.path);
        if (running) {
            QFont bold = item->font();
            bold.setBold(true);
            item->setFont(bold);
        }
        m_list->addItem(item);
        if (running) {
            m_list->setCurrentItem(item);
        }
    }

    if (models.isEmpty()) {
        m_summary->setText(QStringLiteral(
            "No models installed. Kraken reads nothing until one is."));
    } else if (active.isEmpty()) {
        m_summary->setText(QStringLiteral(
            "%1 installed, and none of them chosen to run.").arg(models.size()));
    } else {
        m_summary->setText(
            QStringLiteral("%1 installed. The ticked one is what Transcribe runs.")
                .arg(models.size()));
    }

    // Anything downloaded that no model on the list points at. Said out loud
    // rather than left on disk: this is the only route to files that never
    // reached the list, and without it they cannot be reached at all.
    qint64 wasted = 0;
    for (const KrakenEnvironment::UnusedDownload &download :
         m_environment->unusedDownloads()) {
        wasted += download.bytes;
    }
    const bool anyWaste = wasted > 0;
    m_unused->setVisible(anyWaste);
    m_cleanUpButton->setVisible(anyWaste);
    if (anyWaste) {
        m_unused->setText(
            QStringLiteral("%1 downloaded and not in use.")
                .arg(QLocale().formattedDataSize(
                    wasted, 1, QLocale::DataSizeTraditionalFormat)));
    }

    updateButtons();
}

void HtrModelsDialog::cleanUpUnused()
{
    const QList<KrakenEnvironment::UnusedDownload> unused = m_environment->unusedDownloads();
    qint64 total = 0;
    for (const KrakenEnvironment::UnusedDownload &download : unused) {
        total += download.bytes;
    }
    if (total <= 0) {
        return;
    }

    const QString size =
        QLocale().formattedDataSize(total, 1, QLocale::DataSizeTraditionalFormat);
    if (QMessageBox::question(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("Delete %1 of downloads that no installed model uses?"
                           "<p>The models in the list above are not touched.</p>")
                .arg(size),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel)
        != QMessageBox::Yes) {
        return;
    }

    const qint64 freed = m_environment->deleteUnusedDownloads();
    QMessageBox::information(
        this,
        QStringLiteral("Milah"),
        freed > 0 ? QStringLiteral("%1 freed.").arg(QLocale().formattedDataSize(
                        freed, 1, QLocale::DataSizeTraditionalFormat))
                  : QStringLiteral("Nothing could be deleted."));
    refresh();
}

QString HtrModelsDialog::selectedPath() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void HtrModelsDialog::useSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty()) {
        return;
    }
    KrakenEnvironment::setModelPath(path);
    m_environment->refresh();
    refresh();
}

void HtrModelsDialog::addFromRepository()
{
    HtrRepositoryDialog chooser(m_environment, this);
    if (chooser.exec() != QDialog::Accepted || chooser.chosenModel().isEmpty()) {
        return;
    }
    // Already a path as the environment sees it: the helper that downloaded it
    // was running inside the environment.
    KrakenEnvironment::rememberModel(chooser.chosenModel(), chooser.chosenLabel());
    m_environment->refresh();
    refresh();
}

void HtrModelsDialog::addFromFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Choose a Kraken model"),
        QSettings().value(QStringLiteral("paths/lastDirectory")).toString(),
        QStringLiteral("Kraken models (*.mlmodel *.safetensors);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QSettings().setValue(
        QStringLiteral("paths/lastDirectory"), QFileInfo(path).absolutePath());

    // Converted here, unlike a downloaded one: this path is Windows's, and
    // under WSL kraken calls it something else.
    KrakenEnvironment::rememberModel(
        KrakenEnvironment::pathFor(path), QFileInfo(path).fileName());
    m_environment->refresh();
    refresh();
}

void HtrModelsDialog::removeSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty()) {
        return;
    }
    const QString label = m_list->currentItem()->text();

    // Two different acts wearing one word, so the question says which. A model
    // Milah downloaded is Milah's to delete; a model somebody chose off their
    // own disk is theirs, and Milah forgets it rather than reaching into their
    // files.
    const bool ours = path.contains(QStringLiteral("/.milah-htr/models/"));
    const QString question = ours
        ? QStringLiteral("Delete <b>%1</b> and its files?<p>It can be downloaded "
                         "again from the repository.</p>")
              .arg(label.toHtmlEscaped())
        : QStringLiteral("Stop using <b>%1</b>?<p>Milah will forget it. The file "
                         "itself is yours and is left exactly where it is:"
                         "<br><code>%2</code></p>")
              .arg(label.toHtmlEscaped(), path.toHtmlEscaped());

    if (QMessageBox::question(
            this,
            QStringLiteral("Milah"),
            question,
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel)
        != QMessageBox::Yes) {
        return;
    }

    if (ours && !m_environment->deleteModelFiles(path)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("The files could not be deleted, so %1 has been left "
                           "as it is.")
                .arg(label.toHtmlEscaped()));
        return;
    }

    KrakenEnvironment::forgetModel(path);
    m_environment->refresh();
    refresh();
}

void HtrModelsDialog::updateButtons()
{
    const bool chosen = !selectedPath().isEmpty();
    m_useButton->setEnabled(chosen && selectedPath() != KrakenEnvironment::modelPath());
    m_removeButton->setEnabled(chosen);
}

} // namespace milah
