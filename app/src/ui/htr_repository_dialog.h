#pragma once

#include "ui/kraken_environment.h"

#include <QDialog>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace milah {

class HtrProgress;

/// Choosing a recognition model out of Kraken's repository.
///
/// Replaces what used to happen: `kraken list` printed a table drawn for a
/// terminal into a read-only log, several hundred models long, every script the
/// repository holds, and nothing in it that could be clicked — so the only way
/// through was to read a DOI off it and type that back into a field.
///
/// The repository is asked through htrmopo instead, which hands over the records
/// themselves and counts its progress while it fetches them. See
/// KrakenEnvironment::listCommand.
class HtrRepositoryDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit HtrRepositoryDialog(KrakenEnvironment *environment, QWidget *parent = nullptr);

    /// The model file that was downloaded, as the environment sees it. Empty
    /// unless the dialog was accepted.
    QString chosenModel() const { return m_chosen; }
    /// What to call it in a menu — the repository's summary for it.
    QString chosenLabel() const { return m_chosenLabel; }

protected:
    /// Refuses to close while the repository is being talked to, so a half-read
    /// listing or a half-downloaded model is stopped deliberately.
    void closeEvent(QCloseEvent *event) override;

private:
    /// One model, as much of it as the table and the detail pane need.
    struct Model
    {
        QString doi;
        QString summary;
        QString licence;
        QString creators;
        QString published;
        /// What the record says it was made for. Shown and never filtered on:
        /// the Party base model claims Kraken and Kraken cannot load it.
        QString software;
        QStringList script;
        QStringList language;
        QStringList modelType;
        QStringList keywords;
        /// Character error rate where the record states one, negative where it
        /// does not — which is most of the older ones.
        double cer = -1.0;

        /// The model file the record ships: `BiblIA_01.mlmodel`, `tiny.-
        /// safetensors`. The name everybody actually uses — nobody calls BiblIA
        /// "Medieval Hebrew manuscripts version 1.0" — and, for the three Party
        /// records that share one summary between them, the only thing that
        /// tells them apart.
        QString file;
        /// What downloading it will cost, in bytes. Zero when unstated.
        qint64 bytes = 0;
    };

    /// How narrowly a model is aimed at the script being looked for.
    ///
    /// Specialisation rather than completeness of the record, and that is the
    /// correction: ranking by "declares the script *and* a matching language"
    /// put a twelve-script generalist above four models trained on nothing but
    /// medieval Hebrew, because the generalist listed a language and they did
    /// not. A model that reads one script was made for it; a model that reads
    /// fifteen was made for none of them in particular.
    ///
    /// Still a statement about the record rather than a quality score. How many
    /// scripts a model claims is a fact it states about itself; how well it
    /// reads any of them is not something this dialog can know.
    enum class Fit
    {
        None = 0,
        /// Only the prose says so — a summary or a keyword. A guess, labelled.
        Mentioned = 1,
        /// Declares this script among many. Useful, and aimed at nothing.
        Multilingual = 2,
        /// Declares this script and a few others — a related family, usually.
        Focused = 3,
        /// Declares this script and no other. What BiblIA's Hebrew hands are.
        Dedicated = 4,
    };

    /// Above this many declared scripts, a model is a generalist. Four because
    /// a model covering a script and its near neighbours is still aimed
    /// somewhere, and a dozen is not.
    static constexpr int FocusedScriptLimit = 4;

    /// Which of the two things the helper is being asked for, so one set of
    /// process plumbing serves both.
    enum class Request
    {
        None,
        Listing,
        Download,
    };

    void startListing();
    void startDownload();
    void stop();

    void readPayload(const QByteArray &json);
    /// Pulls `MILAH-PROGRESS` and `MILAH-ERROR` out of stderr and shows the
    /// rest as the detail line.
    void readDiagnostics(const QString &text);
    /// Why the helper failed: what it said through the marker, and failing that
    /// the last thing it said at all.
    QString failureText() const;

    /// Rebuilds the table from m_models and whatever the filters say, and
    /// states how many of how many are being shown — a filter that hides
    /// something ought to be visible rather than merely effective.
    void applyFilter();
    bool matches(const Model &model) const;
    void showSelection();
    void updateButtons();

    /// Fills the Script combo from the scripts the listing actually contains,
    /// each with its count, and opens it on Hebrew where there is any.
    /// Gives the columns their opening widths, once and never again.
    ///
    /// Narrow on purpose — a script list is worth a glance, not a third of the
    /// window, and the whole of it is a hover away in the tooltip. The "once"
    /// is the important half: sizing them on every rebuild is what made them
    /// feel unresizable, since typing in the filter threw away whatever width
    /// had just been dragged to.
    void sizeColumnsOnce();
    void rebuildScriptList();
    /// The script code the combo is on, empty for "Any script".
    QString targetScript() const;
    /// Whether this record reads text at all. Segmentation and reading-order
    /// models fail in kraken's `-m`, so offering one is offering a mistake.
    static bool isRecognition(const Model &model);

    /// Why a model is no use to this Kraken, or None when it is.
    ///
    /// Three signals of quite different strength, and the interface says which
    /// fired rather than flattening them into one verdict — a suspicion and a
    /// demonstration are not the same claim.
    enum class Unusable
    {
        None = 0,
        /// It was downloaded and Kraken refused to load it. A fact.
        Proven,
        /// The record names a program that is not Kraken.
        Declared,
        /// The prose names another recogniser. Catches the Party base model,
        /// which declares Kraken and is not one.
        Suspected,
    };
    static Unusable unusableReason(const Model &model);
    static QString unusableText(Unusable reason);
    Fit fitOf(const Model &model) const;
    static QString fitLabel(Fit fit);
    /// Whether the record names a language that goes with `target`, for the
    /// scripts where one language dominates. A tie-break rather than a tier —
    /// making it a tier is what put the generalists on top.
    bool languageAgrees(const Model &model) const;
    /// What the Fit column sorts on: the tier, then whether the language
    /// agrees, then the lower stated error rate. Higher is better, and the
    /// table opens sorted downwards, so the most specialised model with the
    /// best score is the top row.
    static double fitKey(Fit fit, bool languageAgrees, double cer);

    /// The model the selection names, or nullptr.
    const Model *selectedModel() const;

    KrakenEnvironment *m_environment = nullptr;
    /// The helper, as the environment sees it. Written once when the dialog
    /// opens.
    QString m_script;
    QString m_chosen;
    QString m_chosenLabel;

    QList<Model> m_models;
    /// Whether the columns have had their opening widths. After that they are
    /// the reader's.
    bool m_columnsSized = false;
    /// Whether the opening sort has been applied. Same rule, same reason: a
    /// sort the reader chose must survive the next keystroke in the filter box.
    bool m_sorted = false;

    QLineEdit *m_filterField = nullptr;
    QComboBox *m_scriptCombo = nullptr;
    QCheckBox *m_recognitionOnly = nullptr;
    /// Off by default: what cannot be run should not be offered. On, they
    /// appear muted with the reason in a tooltip — because hiding something
    /// with no way to see it is its own kind of wrong.
    QCheckBox *m_showUnusable = nullptr;
    QLabel *m_count = nullptr;
    QTableWidget *m_table = nullptr;
    QTextBrowser *m_detail = nullptr;
    HtrProgress *m_progress = nullptr;

    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    QProcess *m_process = nullptr;
    Request m_request = Request::None;
    QByteArray m_payload;
    /// What the helper said through MILAH-ERROR, and separately the last thing
    /// it said at all — see failureText().
    QString m_error;
    QString m_lastLine;
    /// The DOI being downloaded, for the message when it fails.
    QString m_downloading;
};

} // namespace milah
