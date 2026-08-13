#include "ui/htr_repository_dialog.h"

#include "ui/htr_progress.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace milah {
namespace {

enum Column
{
    ModelColumn = 0,
    FileColumn,
    FitColumn,
    ScriptColumn,
    LanguageColumn,
    AccuracyColumn,
    SizeColumn,
    PublishedColumn,
    ColumnCount,
};

/// The model file out of everything a record distributes.
///
/// Records ship a README beside the model, and one of them ships nothing else
/// of interest, so the extension decides. `.mlmodel` first because where a
/// record offers both it is the one kraken has always been able to read.
QString modelFileOf(const QList<QPair<QString, qint64>> &files, qint64 *bytes)
{
    for (const QString &wanted : {QStringLiteral(".mlmodel"), QStringLiteral(".safetensors")}) {
        for (const auto &file : files) {
            if (file.first.endsWith(wanted, Qt::CaseInsensitive)) {
                if (bytes) {
                    *bytes = file.second;
                }
                return file.first;
            }
        }
    }
    return QString();
}

/// What CER is, said wherever the number appears.
///
/// The second sentence matters as much as the first. A figure that looks like a
/// score invites being sorted on and believed, and these were never measured
/// against one another.
const QLatin1String kCerTooltip(
    "Character error rate — the percentage of characters this model got wrong "
    "on its own test material. Lower is better.\n\n"
    "Not comparable between models: each was measured on its author's own "
    "manuscripts, so 2% here and 4% there may be no real difference at all.");

/// The scripts a manuscript project actually meets, by their ISO 15924 code.
///
/// Two jobs. It puts a readable name in the combo, and it folds the two ways
/// the repository spells a script into one entry: v1 records validate against
/// ISO 15924 and say `Hebr`, while the older v0 records predate that and say
/// "Hebrew" in prose.
struct ScriptEntry
{
    const char *code;
    const char *name;
    /// The language codes that go with this script where one language
    /// dominates it in practice, and nothing where several do. Latin is shared
    /// by hundreds of languages, so it is deliberately empty — and the Exact
    /// tier is then unreachable for it, which is the honest answer rather than
    /// a missing one.
    const char *languages;
};

constexpr ScriptEntry kScripts[] = {
    {"Hebr", "Hebrew", "heb"},
    {"Arab", "Arabic", ""},
    {"Armn", "Armenian", "hye"},
    {"Copt", "Coptic", "cop"},
    {"Cyrl", "Cyrillic", ""},
    {"Ethi", "Ethiopic", "gez"},
    {"Geor", "Georgian", "kat"},
    {"Grek", "Greek", "grc ell"},
    {"Latn", "Latin", ""},
    {"Syrc", "Syriac", "syr"},
};

/// The canonical code for however a record spelled a script. Anything the table
/// has never heard of keeps its own spelling rather than being guessed at.
QString canonicalScript(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    for (const ScriptEntry &entry : kScripts) {
        if (trimmed.compare(QLatin1String(entry.code), Qt::CaseInsensitive) == 0
            || trimmed.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(entry.code);
        }
    }
    return trimmed;
}

QString scriptName(const QString &code)
{
    for (const ScriptEntry &entry : kScripts) {
        if (code.compare(QLatin1String(entry.code), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(entry.name);
        }
    }
    return code;
}

QStringList scriptLanguages(const QString &code)
{
    for (const ScriptEntry &entry : kScripts) {
        if (code.compare(QLatin1String(entry.code), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(entry.languages)
                .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
    }
    return {};
}

/// A cell that sorts on a number it is given rather than on the words it shows.
///
/// QTableWidgetItem compares display roles, so "Declared" and "Exact" would sort
/// alphabetically — putting the best tier in the middle — and "4.2%" would sort
/// before "12.0%". Both columns want a key that is not what is written in them.
class SortableItem final : public QTableWidgetItem
{
public:
    SortableItem(const QString &text, double key)
        : QTableWidgetItem(text)
        , m_key(key)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        if (const auto *sortable = dynamic_cast<const SortableItem *>(&other)) {
            return m_key < sortable->m_key;
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    double m_key = 0.0;
};

QString orDash(const QString &text)
{
    return text.trimmed().isEmpty() ? QStringLiteral("—") : text.trimmed();
}

QStringList stringsOf(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            list.append(text);
        }
    }
    return list;
}

} // namespace

HtrRepositoryDialog::HtrRepositoryDialog(KrakenEnvironment *environment, QWidget *parent)
    : QDialog(parent)
    , m_environment(environment)
{
    setWindowTitle(QStringLiteral("Choose a recognition model"));
    setModal(true);
    resize(900, 620);

    m_filterField = new QLineEdit;
    m_filterField->setPlaceholderText(
        QStringLiteral("Filter by name, script, author or DOI"));
    m_filterField->setClearButtonEnabled(true);
    connect(m_filterField, &QLineEdit::textChanged, this, &HtrRepositoryDialog::applyFilter);

    // A list rather than a Hebrew yes/no, so that a transcriber whose next
    // manuscript is Syriac is not told the tool has only ever heard of one
    // script. It opens on Hebrew wherever there is Hebrew, so the default
    // behaviour is unchanged.
    m_scriptCombo = new QComboBox;
    m_scriptCombo->setToolTip(QStringLiteral(
        "Which script the models should read. The Fit column then says how "
        "firmly each one claims it."));
    connect(
        m_scriptCombo,
        &QComboBox::currentIndexChanged,
        this,
        &HtrRepositoryDialog::applyFilter);

    m_recognitionOnly = new QCheckBox(QStringLiteral("Recognition models only"));
    m_recognitionOnly->setChecked(true);
    m_recognitionOnly->setToolTip(QStringLiteral(
        "The repository also holds segmentation and reading-order models, which "
        "read no text and cannot be used here."));
    connect(m_recognitionOnly, &QCheckBox::toggled, this, &HtrRepositoryDialog::applyFilter);

    m_showUnusable = new QCheckBox(QStringLiteral("Show models Kraken cannot load"));
    m_showUnusable->setToolTip(QStringLiteral(
        "The repository holds recognition models for other programs too, and "
        "Kraken cannot load them. They are left out unless you ask, and shown "
        "greyed with the reason when you do."));
    connect(m_showUnusable, &QCheckBox::toggled, this, &HtrRepositoryDialog::applyFilter);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(m_filterField, 1);
    filterRow->addWidget(new QLabel(QStringLiteral("Script")));
    filterRow->addWidget(m_scriptCombo);
    filterRow->addWidget(m_recognitionOnly);
    filterRow->addWidget(m_showUnusable);

    m_count = new QLabel;

    m_table = new QTableWidget(0, ColumnCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Model"),
                                        QStringLiteral("File"),
                                        QStringLiteral("Fit"),
                                        QStringLiteral("Script"),
                                        QStringLiteral("Language"),
                                        QStringLiteral("CER %"),
                                        QStringLiteral("Size"),
                                        QStringLiteral("Published")});
    m_table->horizontalHeaderItem(FitColumn)
        ->setToolTip(QStringLiteral(
            "How narrowly this model is aimed at the chosen script.\n\n"
            "Dedicated — it reads this script and no other.\n"
            "Focused — this script and a few related ones.\n"
            "Multilingual — this script among many. Useful, and aimed at "
            "nothing in particular.\n"
            "Mentioned — only the summary or keywords say so.\n\n"
            "Models of equal fit are ordered by whether they name a matching "
            "language, then by their stated error rate, lowest first. The list "
            "opens sorted this way, so the top row is the best fit for the "
            "script chosen."));
    m_table->horizontalHeaderItem(FileColumn)
        ->setToolTip(QStringLiteral(
            "The file this model is distributed as — and, for most of them, the "
            "name it is actually known by. The Hebrew bookhand models are "
            "BiblIA, Ashkenazi, Sephardi and Italian; their summaries all begin "
            "\"Medieval Hebrew manuscripts\" and say none of that."));
    m_table->horizontalHeaderItem(AccuracyColumn)->setToolTip(QString(kCerTooltip));
    m_table->horizontalHeaderItem(SizeColumn)
        ->setToolTip(QStringLiteral(
            "How much the model itself weighs, and so how much pressing "
            "Download will fetch. The Hebrew bookhand models are 16 MB; the "
            "multilingual base models run to several hundred."));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    // Interactive throughout, with opening widths set once — see
    // sizeColumnsOnce(). Every column is the reader's to drag, and none of them
    // is dragged back afterwards.
    for (int column = 0; column < ColumnCount; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    // Below Qt's default, so a column somebody wants out of the way can be
    // pushed most of it out of the way.
    m_table->horizontalHeader()->setMinimumSectionSize(40);
    connect(
        m_table,
        &QTableWidget::itemSelectionChanged,
        this,
        &HtrRepositoryDialog::showSelection);
    connect(m_table, &QTableWidget::doubleClicked, this, &HtrRepositoryDialog::startDownload);

    m_detail = new QTextBrowser;
    m_detail->setMaximumHeight(150);
    m_detail->setOpenExternalLinks(false);

    m_progress = new HtrProgress;

    m_refreshButton = new QPushButton(QStringLiteral("Refresh"));
    m_refreshButton->setToolTip(QStringLiteral(
        "Asks the repository again. The listing is cached after the first time, "
        "which is why opening this is usually instant."));
    connect(m_refreshButton, &QPushButton::clicked, this, &HtrRepositoryDialog::startListing);

    m_downloadButton = new QPushButton(QStringLiteral("Download and use"));
    m_downloadButton->setDefault(true);
    connect(m_downloadButton, &QPushButton::clicked, this, &HtrRepositoryDialog::startDownload);

    m_closeButton = new QPushButton(QStringLiteral("Cancel"));
    connect(m_closeButton, &QPushButton::clicked, this, [this] {
        if (m_process) {
            stop();
            return;
        }
        close();
    });

    auto *buttons = new QDialogButtonBox;
    buttons->addButton(m_refreshButton, QDialogButtonBox::ResetRole);
    buttons->addButton(m_downloadButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(m_closeButton, QDialogButtonBox::RejectRole);

    auto *outer = new QVBoxLayout(this);
    outer->addLayout(filterRow);
    outer->addWidget(m_count);
    outer->addWidget(m_table, 1);
    outer->addWidget(m_detail);
    outer->addWidget(m_progress);
    outer->addWidget(buttons);

    updateButtons();
    // Asked as the dialog opens rather than behind a button nobody would think
    // to press. htrmopo caches the listing, so this is slow once and instant
    // afterwards.
    QMetaObject::invokeMethod(this, &HtrRepositoryDialog::startListing, Qt::QueuedConnection);
}

void HtrRepositoryDialog::startListing()
{
    if (m_process) {
        return;
    }
    if (m_script.isEmpty()) {
        m_script = m_environment->writeHelperScript();
    }
    if (m_script.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("Milah could not write the helper it uses to ask the "
                           "model repository."));
        return;
    }

    m_request = Request::Listing;
    m_payload.clear();
    m_error.clear();
    m_lastLine.clear();
    m_progress->begin(QStringLiteral("Asking the repository what it holds"));

    QStringList command = m_environment->listCommand(m_script);
    const QString program = command.takeFirst();

    m_process = new QProcess(this);
    // Separate, so a progress line on stderr can never land in the middle of
    // the JSON on stdout.
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_payload += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        readDiagnostics(QString::fromUtf8(m_process->readAllStandardError()));
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QByteArray payload = m_payload;
        const QString why = failureText();
        m_process->deleteLater();
        m_process = nullptr;
        m_request = Request::None;

        if (status == QProcess::CrashExit) {
            m_progress->finish(QStringLiteral("Stopped."));
            updateButtons();
            return;
        }
        if (code != 0) {
            m_progress->finish(
                why.isEmpty() ? QStringLiteral("The repository could not be reached.")
                              : why);
            updateButtons();
            return;
        }
        m_progress->finish();
        readPayload(payload);
        updateButtons();
    });

    m_process->start(program, command);
    updateButtons();
}

void HtrRepositoryDialog::startDownload()
{
    if (m_process) {
        return;
    }
    const Model *model = selectedModel();
    if (!model) {
        return;
    }

    m_request = Request::Download;
    m_payload.clear();
    m_error.clear();
    m_lastLine.clear();
    m_downloading = model->doi;
    // Kept now rather than looked up when the download finishes: applyFilter()
    // may have rebuilt the table by then, and the pointer into m_models with it.
    m_chosenLabel = model->summary.trimmed().isEmpty() ? model->doi : model->summary.trimmed();
    m_progress->begin(QStringLiteral("Downloading %1").arg(model->doi));
    m_progress->setDetail(
        QStringLiteral("Kraken will be asked to load it when it arrives."));

    QStringList command = m_environment->fetchCommand(m_script, model->doi);
    const QString program = command.takeFirst();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_payload += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        readDiagnostics(QString::fromUtf8(m_process->readAllStandardError()));
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QByteArray payload = m_payload;
        const QString why = failureText();
        m_process->deleteLater();
        m_process = nullptr;
        m_request = Request::None;
        updateButtons();

        if (status == QProcess::CrashExit) {
            m_progress->finish(QStringLiteral("Download stopped. Nothing has been "
                                              "chosen."));
            return;
        }
        if (code != 0) {
            m_progress->finish();
            QMessageBox::warning(
                this,
                QStringLiteral("Milah"),
                QStringLiteral("%1 could not be downloaded.\n\n%2")
                    .arg(m_downloading,
                         why.isEmpty() ? QStringLiteral("No reason was given.") : why));
            return;
        }

        const QJsonObject answer = QJsonDocument::fromJson(payload).object();
        const QString path = answer.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            m_progress->finish();
            QMessageBox::warning(
                this,
                QStringLiteral("Milah"),
                QStringLiteral("%1 was downloaded, but Milah could not find a "
                               "model file in what arrived.")
                    .arg(m_downloading));
            return;
        }

        // Downloaded and then tried, because nothing in the record says whether
        // Kraken can read it. The repository holds models for more than one
        // program, and the Party ones declare Kraken as their software.
        if (!answer.value(QStringLiteral("loadable")).toBool()) {
            m_progress->finish();

            // Deleted, not kept. This used to leave the files where they were,
            // on the reasoning that a later Kraken might read them — which
            // overlooked that a rejected model never joins the installed list,
            // and that Manage models… can only show what is on that list. The
            // files were therefore invisible and unreachable: several hundred
            // megabytes with no button anywhere in Milah that could touch them.
            const bool removed = m_environment->deleteModelFiles(path);

            // And remembered, so it is not offered again. Of the three things
            // that mark a model as no use here, this is the only one that is a
            // demonstration rather than a reading of the record.
            KrakenEnvironment::rememberUnusable(m_downloading);

            QMessageBox::warning(
                this,
                QStringLiteral("Milah"),
                QStringLiteral(
                    "%1 downloaded, but this Kraken cannot load it — it is a "
                    "model for a different program.<p>%2 The model you were "
                    "using is still the one that runs, and this one will not be "
                    "offered again.</p><p><small>%3</small></p>")
                    .arg(m_downloading,
                         removed ? QStringLiteral("The download has been deleted.")
                                 : QStringLiteral("The download could not be deleted; "
                                                  "Manage models… will offer to clear "
                                                  "it up."),
                         answer.value(QStringLiteral("why")).toString().toHtmlEscaped()));

            // Re-read, so the row disappears from the table it was chosen in.
            applyFilter();
            return;
        }

        m_chosen = path;
        accept();
    });

    m_process->start(program, command);
    updateButtons();
}

void HtrRepositoryDialog::stop()
{
    if (!m_process) {
        return;
    }
    m_process->kill();
    m_process->waitForFinished(3000);
}

void HtrRepositoryDialog::readDiagnostics(const QString &text)
{
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String(KrakenEnvironment::progressMarker()))) {
            const QStringList words =
                trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (words.size() >= 3) {
                // Bytes while downloading a file, records while walking the
                // listing — the same numbers read out two different ways.
                m_progress->setProgress(
                    words.at(1).toLongLong(),
                    words.at(2).toLongLong(),
                    m_request == Request::Download ? HtrProgress::Unit::Bytes
                                                   : HtrProgress::Unit::Items);
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1String(KrakenEnvironment::errorMarker()))) {
            m_error =
                trimmed.mid(int(qstrlen(KrakenEnvironment::errorMarker()))).trimmed();
            continue;
        }
        m_progress->setDetail(trimmed);
        // The last line, not the first. A Python traceback opens with
        // "Traceback (most recent call last):", which says nothing, and closes
        // with the exception, which says everything.
        m_lastLine = trimmed;
    }
}

QString HtrRepositoryDialog::failureText() const
{
    if (!m_error.isEmpty()) {
        return m_error;
    }
    return m_lastLine;
}

void HtrRepositoryDialog::readPayload(const QByteArray &json)
{
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(json, &problem);
    if (document.isNull()) {
        m_progress->finish(QStringLiteral("The repository's answer could not be "
                                          "read: %1")
                               .arg(problem.errorString()));
        return;
    }

    m_models.clear();
    for (const QJsonValue &value : document.object().value(QStringLiteral("models")).toArray()) {
        const QJsonObject object = value.toObject();
        Model model;
        model.doi = object.value(QStringLiteral("doi")).toString();
        model.summary = object.value(QStringLiteral("summary")).toString();
        model.licence = object.value(QStringLiteral("license")).toString();
        model.creators = object.value(QStringLiteral("creators")).toString();
        model.published = object.value(QStringLiteral("published")).toString();
        model.script = stringsOf(object.value(QStringLiteral("script")));
        model.language = stringsOf(object.value(QStringLiteral("language")));
        model.modelType = stringsOf(object.value(QStringLiteral("model_type")));
        model.keywords = stringsOf(object.value(QStringLiteral("keywords")));
        model.software = object.value(QStringLiteral("software")).toString();

        QList<QPair<QString, qint64>> files;
        for (const QJsonValue &value : object.value(QStringLiteral("files")).toArray()) {
            const QJsonObject file = value.toObject();
            files.append({file.value(QStringLiteral("name")).toString(),
                          qint64(file.value(QStringLiteral("size")).toDouble())});
        }
        model.file = modelFileOf(files, &model.bytes);

        // The records disagree about what the metric is called, so the first key
        // with `cer` in it is taken and the rest ignored rather than guessed at.
        const QJsonObject metrics = object.value(QStringLiteral("metrics")).toObject();
        for (auto item = metrics.constBegin(); item != metrics.constEnd(); ++item) {
            if (item.key().contains(QStringLiteral("cer"))) {
                model.cer = item.value().toDouble(-1.0);
                break;
            }
        }

        if (!model.doi.isEmpty()) {
            m_models.append(model);
        }
    }

    rebuildScriptList();
    applyFilter();
}

void HtrRepositoryDialog::sizeColumnsOnce()
{
    if (m_columnsSized || m_table->rowCount() == 0) {
        return;
    }
    m_columnsSized = true;

    QHeaderView *header = m_table->horizontalHeader();
    header->resizeSection(FileColumn, 130);
    header->resizeSection(FitColumn, 95);
    header->resizeSection(ScriptColumn, 95);
    header->resizeSection(LanguageColumn, 95);
    header->resizeSection(AccuracyColumn, 75);
    header->resizeSection(SizeColumn, 85);
    header->resizeSection(PublishedColumn, 100);

    // The name takes what is left, because it is the one column anybody reads
    // rather than glances at. Not a Stretch section: that would keep it filling
    // the window and stop it being dragged, and every column here is the
    // reader's to set.
    constexpr int others = 130 + 95 + 95 + 95 + 75 + 85 + 100;
    header->resizeSection(
        ModelColumn, std::max(240, m_table->viewport()->width() - others));
}

void HtrRepositoryDialog::rebuildScriptList()
{
    // Counted from what the listing actually holds rather than from a fixed
    // list, so the combo never offers a script with nothing behind it.
    QMap<QString, int> counts;
    for (const Model &model : m_models) {
        QStringList seen;
        for (const QString &raw : model.script) {
            const QString code = canonicalScript(raw);
            // A record naming a script twice is still one model.
            if (!code.isEmpty() && !seen.contains(code)) {
                seen.append(code);
                ++counts[code];
            }
        }
    }

    const QSignalBlocker quiet(m_scriptCombo);
    // Told apart from "the reader chose Any script", which is also an empty
    // code: a Refresh must not quietly put them back on Hebrew.
    const bool firstFill = m_scriptCombo->count() == 0;
    const QString was = targetScript();
    m_scriptCombo->clear();

    // Sorted by name so the list reads alphabetically rather than by ISO code,
    // which nobody knows the order of.
    QList<QString> codes = counts.keys();
    std::sort(codes.begin(), codes.end(), [](const QString &left, const QString &right) {
        return scriptName(left).localeAwareCompare(scriptName(right)) < 0;
    });

    for (const QString &code : codes) {
        m_scriptCombo->addItem(
            QStringLiteral("%1 (%2)").arg(scriptName(code)).arg(counts.value(code)), code);
    }
    // Last, so it is not what the eye lands on: the whole repository at once is
    // the answer to a question almost nobody is asking here.
    m_scriptCombo->addItem(QStringLiteral("Any script"), QString());

    const int wanted = firstFill ? m_scriptCombo->findData(QStringLiteral("Hebr"))
                                 : m_scriptCombo->findData(was);
    m_scriptCombo->setCurrentIndex(wanted >= 0 ? wanted : 0);
}

bool HtrRepositoryDialog::isRecognition(const Model &model)
{
    for (const QString &kind : model.modelType) {
        if (kind.compare(QLatin1String("recognition"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

HtrRepositoryDialog::Unusable HtrRepositoryDialog::unusableReason(const Model &model)
{
    // Tried, and refused. The only one of the three that is a fact rather than
    // a reading of what the record says about itself.
    if (KrakenEnvironment::unusableModels().contains(model.doi)) {
        return Unusable::Proven;
    }

    // The record naming its own program. Empty means the older schema, which
    // predates the field and is Kraken's own era, so it is not held against it.
    if (!model.software.isEmpty()
        && model.software.compare(QLatin1String("kraken"), Qt::CaseInsensitive) != 0) {
        return Unusable::Declared;
    }

    // And the prose, which is what catches the Party base model: it declares
    // `software_name: kraken`, calls itself a Party model in its own summary,
    // and stops Kraken with "PartyModel is not in model registry".
    static const QStringList others{QStringLiteral("party")};
    const QString prose =
        (QStringList{model.summary} + model.keywords).join(QLatin1Char(' '));
    for (const QString &program : others) {
        if (prose.contains(program, Qt::CaseInsensitive)) {
            return Unusable::Suspected;
        }
    }
    return Unusable::None;
}

QString HtrRepositoryDialog::unusableText(Unusable reason)
{
    switch (reason) {
    case Unusable::Proven:
        return QStringLiteral("Downloaded once and refused by Kraken — it is a model "
                              "for a different program.");
    case Unusable::Declared:
        return QStringLiteral("This record says it was made for another program, not "
                              "for Kraken.");
    case Unusable::Suspected:
        return QStringLiteral("This looks like a Party model, which Kraken cannot "
                              "load. Downloading it would prove it either way.");
    case Unusable::None:
        break;
    }
    return QString();
}

QString HtrRepositoryDialog::targetScript() const
{
    return m_scriptCombo ? m_scriptCombo->currentData().toString() : QString();
}

bool HtrRepositoryDialog::languageAgrees(const Model &model) const
{
    const QStringList expected = scriptLanguages(targetScript());
    for (const QString &language : model.language) {
        for (const QString &candidate : expected) {
            if (language.trimmed().compare(candidate, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
    }
    return false;
}

HtrRepositoryDialog::Fit HtrRepositoryDialog::fitOf(const Model &model) const
{
    const QString target = targetScript();
    if (target.isEmpty()) {
        return Fit::None;
    }

    bool declared = false;
    for (const QString &script : model.script) {
        if (canonicalScript(script).compare(target, Qt::CaseInsensitive) == 0) {
            declared = true;
            break;
        }
    }

    if (declared) {
        // How many scripts it claims altogether, which is the thing that says
        // whether it was aimed at this one. BiblIA's Hebrew hands declare
        // Hebrew and nothing else; the multilingual base models declare a
        // dozen and were trained for none of them in particular.
        const int breadth = model.script.size();
        if (breadth <= 1) {
            return Fit::Dedicated;
        }
        return breadth <= FocusedScriptLimit ? Fit::Focused : Fit::Multilingual;
    }

    // Nothing in the fields, so the prose is all there is. Worth showing, and
    // worth labelling as the guess it is: the older records name their script
    // in a summary because there was no field for it when they were made.
    const QString name = scriptName(target);
    const QString prose =
        (QStringList{model.summary} + model.keywords + model.language).join(QLatin1Char(' '));
    if (prose.contains(name, Qt::CaseInsensitive)) {
        return Fit::Mentioned;
    }
    return Fit::None;
}

QString HtrRepositoryDialog::fitLabel(Fit fit)
{
    switch (fit) {
    case Fit::Dedicated:
        return QStringLiteral("Dedicated");
    case Fit::Focused:
        return QStringLiteral("Focused");
    case Fit::Multilingual:
        return QStringLiteral("Multilingual");
    case Fit::Mentioned:
        return QStringLiteral("Mentioned");
    case Fit::None:
        break;
    }
    return QStringLiteral("—");
}

double HtrRepositoryDialog::fitKey(Fit fit, bool languageAgrees, double cer)
{
    const double tier = double(int(fit)) * 1000.0;

    // A named language is evidence, not a tier. It used to be one, and that is
    // precisely how a twelve-script generalist came to sit above four models
    // trained on nothing but medieval Hebrew — the generalist listed languages
    // and the older records had no field to list them in.
    const double named = languageAgrees ? 120.0 : 0.0;

    // Then the lower stated error rate. A record stating none is treated as
    // middling rather than as worst: silence is not a failing grade, and most
    // of the Hebrew records state nothing at all.
    const double stated = cer >= 0.0 ? std::clamp(cer, 0.0, 100.0) : 50.0;
    return tier + named + (100.0 - stated);
}

bool HtrRepositoryDialog::matches(const Model &model) const
{
    if (!targetScript().isEmpty() && fitOf(model) == Fit::None) {
        return false;
    }
    if (m_recognitionOnly->isChecked() && !isRecognition(model)) {
        return false;
    }
    // What cannot be run is not offered. Off by default and reversible, so
    // nothing is hidden that cannot be looked at deliberately.
    if (!m_showUnusable->isChecked() && unusableReason(model) != Unusable::None) {
        return false;
    }

    const QString needle = m_filterField->text().trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    const QString haystack = QStringList{model.summary,
                                         // Typing "bibl" has to find BiblIA,
                                         // which is written nowhere else.
                                         model.file,
                                         model.doi,
                                         model.creators,
                                         model.script.join(QLatin1Char(' ')),
                                         model.language.join(QLatin1Char(' ')),
                                         model.keywords.join(QLatin1Char(' '))}
                                 .join(QLatin1Char(' '));
    return haystack.contains(needle, Qt::CaseInsensitive);
}

void HtrRepositoryDialog::applyFilter()
{
    // Held across the rebuild, so ticking a box does not lose the row somebody
    // had picked.
    const Model *was = selectedModel();
    const QString wasDoi = was ? was->doi : QString();

    const bool sorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);

    int shown = 0;
    for (const Model &model : m_models) {
        if (!matches(model)) {
            continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        const Unusable unusable = unusableReason(model);

        auto *name = new QTableWidgetItem(orDash(model.summary));
        // The DOI travels with the row rather than being shown in it: it is how
        // the model is fetched and no use at all to read.
        name->setData(Qt::UserRole, model.doi);
        name->setToolTip(
            unusable == Unusable::None
                ? QStringLiteral("%1\n%2").arg(model.summary, model.doi).trimmed()
                : QStringLiteral("%1\n\n%2").arg(model.summary, unusableText(unusable)));
        m_table->setItem(row, ModelColumn, name);

        // The stem, because the extension is the same for whole runs of them and
        // says nothing about which model this is.
        auto *file = new QTableWidgetItem(
            orDash(model.file.section(QLatin1Char('.'), 0, -2)));
        file->setToolTip(model.file);
        m_table->setItem(row, FileColumn, file);

        const Fit fit = fitOf(model);
        auto *fitCell =
            new SortableItem(fitLabel(fit), fitKey(fit, languageAgrees(model), model.cer));
        if (fit == Fit::Multilingual || fit == Fit::Focused) {
            fitCell->setToolTip(
                QStringLiteral("Reads %1 scripts, of which this is one.")
                    .arg(model.script.size()));
        }
        m_table->setItem(row, FitColumn, fitCell);

        // Every cell carries its whole value as a tooltip. A script list can be
        // longer than any sensible column, and elision with nothing behind it
        // is how a table hides the thing somebody opened it to find.
        const QString scripts = model.script.join(QStringLiteral(", "));
        auto *script = new QTableWidgetItem(orDash(scripts));
        script->setToolTip(scripts);
        m_table->setItem(row, ScriptColumn, script);

        const QString languages = model.language.join(QStringLiteral(", "));
        auto *language = new QTableWidgetItem(orDash(languages));
        language->setToolTip(languages);
        m_table->setItem(row, LanguageColumn, language);

        // Sorted on the number and read as a percentage, which one plain
        // QTableWidgetItem cannot do: it compares what is displayed, so "4.2%"
        // would sort before "12.0%" and the column would look sorted while
        // being wrong.
        auto *accuracy = new SortableItem(
            model.cer >= 0.0 ? QStringLiteral("%1%").arg(model.cer, 0, 'f', 1)
                             : QStringLiteral("—"),
            model.cer >= 0.0 ? model.cer : std::numeric_limits<double>::max());
        accuracy->setToolTip(QString(kCerTooltip));
        m_table->setItem(row, AccuracyColumn, accuracy);

        // Read as "857.5 MB" and sorted on the byte count, which is the same
        // trick the error rate needs: sorted as text, 90 MB would come before
        // 9 MB.
        m_table->setItem(
            row,
            SizeColumn,
            new SortableItem(model.bytes > 0
                                 ? QLocale().formattedDataSize(
                                       model.bytes, 1, QLocale::DataSizeTraditionalFormat)
                                 : QStringLiteral("—"),
                             model.bytes > 0
                                 ? double(model.bytes)
                                 : std::numeric_limits<double>::max()));

        m_table->setItem(row, PublishedColumn, new QTableWidgetItem(orDash(model.published)));

        // Muted across the whole row, so a model that is only on screen because
        // it was asked for cannot be mistaken for one on offer.
        if (unusable != Unusable::None) {
            QColor quiet = palette().color(QPalette::Text);
            quiet.setAlphaF(0.55);
            for (int column = 0; column < ColumnCount; ++column) {
                if (QTableWidgetItem *cell = m_table->item(row, column)) {
                    cell->setForeground(quiet);
                }
            }
        }

        if (model.doi == wasDoi) {
            m_table->selectRow(row);
        }
        ++shown;
    }

    m_table->setSortingEnabled(sorting);

    // Best fit first, best score within it — but only the first time. Descending
    // because fitKey() counts upwards for better, and a table that opens on its
    // worst row is a table everybody sorts by hand before reading.
    //
    // Once only, for the same reason the column widths are set once: this runs
    // on every keystroke in the filter box, and re-sorting there threw away a
    // sort by error rate or by date the moment anything else was touched. The
    // header keeps its indicator either way, so which order this is in is
    // legible rather than merely true.
    if (!m_sorted && m_table->rowCount() > 0) {
        m_sorted = true;
        m_table->sortItems(FitColumn, Qt::DescendingOrder);
        m_table->horizontalHeader()->setSortIndicatorShown(true);
        m_table->horizontalHeader()->setSortIndicator(FitColumn, Qt::DescendingOrder);
    }

    sizeColumnsOnce();

    // Said out loud, because a filter that hides something ought to be visible
    // rather than merely effective — and because "no models" with two boxes
    // ticked means something quite different from "no models" without them.
    if (m_models.isEmpty()) {
        m_count->setText(QStringLiteral("No models yet."));
    } else if (shown == m_models.size()) {
        m_count->setText(QStringLiteral("%1 models.").arg(m_models.size()));
    } else {
        m_count->setText(
            QStringLiteral("%1 of %2 models shown.").arg(shown).arg(m_models.size()));
    }

    showSelection();
    updateButtons();
}

const HtrRepositoryDialog::Model *HtrRepositoryDialog::selectedModel() const
{
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return nullptr;
    }
    const QTableWidgetItem *name = m_table->item(selected.constFirst()->row(), ModelColumn);
    if (!name) {
        return nullptr;
    }
    const QString doi = name->data(Qt::UserRole).toString();
    for (const Model &model : m_models) {
        if (model.doi == doi) {
            return &model;
        }
    }
    return nullptr;
}

void HtrRepositoryDialog::showSelection()
{
    const Model *model = selectedModel();
    if (!model) {
        m_detail->clear();
        updateButtons();
        return;
    }

    QStringList lines;
    lines.append(QStringLiteral("<b>%1</b>").arg(model->summary.toHtmlEscaped()));
    if (!model->creators.isEmpty()) {
        lines.append(model->creators.toHtmlEscaped());
    }
    if (!model->file.isEmpty()) {
        // The name only. The size has a column of its own now, and saying it
        // twice a centimetre apart is not saying it twice as clearly.
        lines.append(QStringLiteral("File: %1").arg(model->file.toHtmlEscaped()));
    }
    lines.append(QStringLiteral("<code>%1</code>").arg(model->doi.toHtmlEscaped()));
    // Spelled out here, where there is room for the whole of it. The table's
    // cells are for scanning and can only ever show as much as they are wide.
    if (!model->script.isEmpty()) {
        lines.append(QStringLiteral("Script: %1")
                         .arg(model->script.join(QStringLiteral(", ")).toHtmlEscaped()));
    }
    if (!model->language.isEmpty()) {
        lines.append(QStringLiteral("Language: %1")
                         .arg(model->language.join(QStringLiteral(", ")).toHtmlEscaped()));
    }
    if (model->cer >= 0.0) {
        lines.append(QStringLiteral("Character error rate: %1%, on its author's own "
                                    "test material")
                         .arg(model->cer, 0, 'f', 1));
    }
    if (!model->licence.isEmpty()) {
        lines.append(QStringLiteral("Licence: %1").arg(model->licence.toHtmlEscaped()));
    }
    if (!model->modelType.isEmpty()) {
        lines.append(QStringLiteral("Type: %1")
                         .arg(model->modelType.join(QStringLiteral(", ")).toHtmlEscaped()));
    }
    if (!model->software.isEmpty()) {
        lines.append(QStringLiteral("Made for: %1").arg(model->software.toHtmlEscaped()));
    }
    if (!model->keywords.isEmpty()) {
        lines.append(model->keywords.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    m_detail->setHtml(lines.join(QStringLiteral("<br>")));
    updateButtons();
}

void HtrRepositoryDialog::updateButtons()
{
    const bool busy = m_process != nullptr;
    m_refreshButton->setEnabled(!busy);
    m_downloadButton->setEnabled(!busy && selectedModel() != nullptr);
    m_filterField->setEnabled(!busy);
    m_scriptCombo->setEnabled(!busy);
    m_recognitionOnly->setEnabled(!busy);
    m_showUnusable->setEnabled(!busy);
    m_table->setEnabled(!busy);
    m_closeButton->setText(busy ? QStringLiteral("Stop") : QStringLiteral("Cancel"));
}

void HtrRepositoryDialog::closeEvent(QCloseEvent *event)
{
    if (!m_process) {
        QDialog::closeEvent(event);
        return;
    }
    stop();
    QDialog::closeEvent(event);
}

} // namespace milah
