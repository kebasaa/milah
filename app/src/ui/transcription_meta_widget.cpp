#include "ui/transcription_meta_widget.h"

#include "core/manuscript_catalogue.h"
#include "transcription_controller.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace milah {
namespace {

/// Room for a sentence or two about the state of the codex without the box
/// taking over the dock.
constexpr int NotesHeight = 90;

QLineEdit *field(QFormLayout *form, const QString &label, const QString &hint)
{
    auto *edit = new QLineEdit;
    edit->setPlaceholderText(hint);
    edit->setClearButtonEnabled(true);
    form->addRow(label, edit);
    return edit;
}

} // namespace

TranscriptionMetaWidget::TranscriptionMetaWidget(
    TranscriptionController *controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    auto *box = new QGroupBox(QStringLiteral("Manuscript"));
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(6);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    boxLayout->addLayout(form);

    m_manuscriptName = field(
        form, QStringLiteral("Manuscript"), QStringLiteral("Vat. ebr. 530"));
    m_transcriber =
        field(form, QStringLiteral("Transcriber"), QStringLiteral("Your name"));
    m_origin =
        field(form, QStringLiteral("Origin"), QStringLiteral("Northern Italy"));
    m_libraryMark =
        field(form, QStringLiteral("Library"), QStringLiteral("Biblioteca Vaticana"));
    m_shelfmark =
        field(form, QStringLiteral("Shelfmark"), QStringLiteral("Ebr. 530"));
    // Against the shelfmark, because that is how one is written out in full:
    // "Vat. ebr. 530, folios 1r–2v".
    m_folios = field(form, QStringLiteral("Folios"), QStringLiteral("1r–2v"));
    m_date = field(form, QStringLiteral("Date"), QStringLiteral("14th century"));
    m_material = field(form, QStringLiteral("Material"), QStringLiteral("Parchment"));
    m_provenance = field(
        form,
        QStringLiteral("Provenance"),
        QStringLiteral("How it came to be where it is kept"));
    // The two judgements, together and after the facts. Nothing fills these in
    // but the transcriber: no library catalogue records either.
    m_translatedFrom = field(
        form,
        QStringLiteral("Translated from"),
        QStringLiteral("Translated from the Greek"));
    // A choice rather than a box, because a verdict comes from a fixed set and
    // typing it invites four spellings of "uncertain" that nothing downstream
    // could tell apart. The empty first entry is the honest default: not that
    // the manuscript is original, but that nobody has said.
    m_translatedFromCertainty = new QComboBox;
    m_translatedFromCertainty->addItem(QStringLiteral("Not recorded"), QString());
    m_translatedFromCertainty->addItem(
        QStringLiteral("Certain"), QLatin1String(TranslationCertainty::Certain));
    m_translatedFromCertainty->addItem(
        QStringLiteral("Uncertain"), QLatin1String(TranslationCertainty::Uncertain));
    m_translatedFromCertainty->addItem(
        QStringLiteral("Not a translation"),
        QLatin1String(TranslationCertainty::Original));
    m_translatedFromCertainty->addItem(
        QStringLiteral("Probably not a translation"),
        QLatin1String(TranslationCertainty::OriginalUncertain));
    m_translatedFromCertainty->setToolTip(QStringLiteral(
        "Whether the line above is established. The download list shows an "
        "unsettled answer with a question mark rather than stating it flat."));
    form->addRow(QString(), m_translatedFromCertainty);
    m_exemplar = field(
        form,
        QStringLiteral("Copy of"),
        QStringLiteral("Copied from Cambridge MS Oo.1.32"));
    m_language = field(form, QStringLiteral("Language"), QStringLiteral("he"));

    m_notes = new QPlainTextEdit;
    m_notes->setMinimumHeight(NotesHeight);
    m_notes->setPlaceholderText(
        QStringLiteral("Anything about the codex worth recording — its condition, "
                       "its hand, where the transcription came from."));
    boxLayout->addWidget(m_notes);

    m_save = new QPushButton(QStringLiteral("Save details"));
    connect(m_save, &QPushButton::clicked, this, &TranscriptionMetaWidget::save);
    boxLayout->addWidget(m_save, 0, Qt::AlignRight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(box);

    // Every line edit, or Save never lights for the one left out.
    for (QLineEdit *edit : {m_manuscriptName,
                            m_transcriber,
                            m_origin,
                            m_libraryMark,
                            m_shelfmark,
                            m_folios,
                            m_date,
                            m_material,
                            m_provenance,
                            m_translatedFrom,
                            m_exemplar,
                            m_language}) {
        connect(
            edit,
            &QLineEdit::textChanged,
            this,
            &TranscriptionMetaWidget::updateSaveState);
    }
    connect(
        m_translatedFromCertainty,
        &QComboBox::currentIndexChanged,
        this,
        &TranscriptionMetaWidget::updateSaveState);
    connect(
        m_notes,
        &QPlainTextEdit::textChanged,
        this,
        &TranscriptionMetaWidget::updateSaveState);

    connect(
        m_controller,
        &TranscriptionController::documentChanged,
        this,
        &TranscriptionMetaWidget::refresh);

    refresh();
}

QString TranscriptionMetaWidget::fingerprint() const
{
    // A single string rather than a field-by-field comparison, because what is
    // being asked is only "has anything changed at all" and the fields have
    // nothing in common but that.
    return QStringList{
        m_manuscriptName->text(),
        m_transcriber->text(),
        m_origin->text(),
        m_libraryMark->text(),
        m_shelfmark->text(),
        m_folios->text(),
        m_date->text(),
        m_material->text(),
        m_provenance->text(),
        m_translatedFrom->text(),
        m_translatedFromCertainty->currentData().toString(),
        m_exemplar->text(),
        m_language->text(),
        m_notes->toPlainText(),
    }
        .join(QLatin1Char('\x1f'));
}

void TranscriptionMetaWidget::refresh()
{
    // Opening a folio, saving the file and editing these details all report the
    // same change, and only the last of them is about this panel. Refilling the
    // boxes while the transcriber is halfway through typing in one would throw
    // away what they had written, so a panel holding unsaved edits is left
    // alone until they have said what to do with them.
    if (!m_saved.isEmpty() && fingerprint() != m_saved) {
        return;
    }

    const TranscriptionMetadata &metadata = m_controller->metadata();

    // Every programmatic fill is blocked, so that loading a transcription does
    // not read as an edit and leave Save looking ready to write over nothing.
    {
        const QSignalBlocker blockName(m_manuscriptName);
        const QSignalBlocker blockTranscriber(m_transcriber);
        const QSignalBlocker blockOrigin(m_origin);
        const QSignalBlocker blockLibrary(m_libraryMark);
        const QSignalBlocker blockShelf(m_shelfmark);
        const QSignalBlocker blockFolios(m_folios);
        const QSignalBlocker blockDate(m_date);
        const QSignalBlocker blockMaterial(m_material);
        const QSignalBlocker blockProvenance(m_provenance);
        const QSignalBlocker blockTranslatedFrom(m_translatedFrom);
        const QSignalBlocker blockCertainty(m_translatedFromCertainty);
        const QSignalBlocker blockExemplar(m_exemplar);
        const QSignalBlocker blockLanguage(m_language);
        const QSignalBlocker blockNotes(m_notes);

        m_manuscriptName->setText(metadata.manuscriptName);
        m_transcriber->setText(metadata.transcriber);
        m_origin->setText(metadata.origin);
        m_libraryMark->setText(metadata.libraryMark);
        m_shelfmark->setText(metadata.shelfmark);
        m_folios->setText(metadata.folios);
        m_date->setText(metadata.date);
        m_material->setText(metadata.material);
        m_provenance->setText(metadata.provenance);
        m_translatedFrom->setText(metadata.translatedFrom);
        // findData answers -1 for a verdict this build has never heard of,
        // which lands on "Not recorded" — the same thing it means to a reader.
        m_translatedFromCertainty->setCurrentIndex(
            std::max(0, m_translatedFromCertainty->findData(metadata.translatedFromCertainty)));
        m_exemplar->setText(metadata.exemplar);
        m_language->setText(metadata.language);
        m_notes->setPlainText(metadata.notes);
    }

    m_saved = fingerprint();
    updateSaveState();
}

void TranscriptionMetaWidget::updateSaveState()
{
    m_save->setEnabled(fingerprint() != m_saved);
}

void TranscriptionMetaWidget::save()
{
    // Read out of the controller rather than default-constructed, so whatever
    // the extra fields hold survives a panel that has no box for them.
    TranscriptionMetadata metadata = m_controller->metadata();
    metadata.manuscriptName = m_manuscriptName->text().trimmed();
    metadata.transcriber = m_transcriber->text().trimmed();
    metadata.origin = m_origin->text().trimmed();
    metadata.libraryMark = m_libraryMark->text().trimmed();
    metadata.shelfmark = m_shelfmark->text().trimmed();
    metadata.folios = m_folios->text().trimmed();
    metadata.date = m_date->text().trimmed();
    metadata.material = m_material->text().trimmed();
    metadata.provenance = m_provenance->text().trimmed();
    metadata.translatedFrom = m_translatedFrom->text().trimmed();
    metadata.translatedFromCertainty =
        m_translatedFromCertainty->currentData().toString();
    metadata.exemplar = m_exemplar->text().trimmed();
    metadata.language = m_language->text().trimmed();
    metadata.notes = m_notes->toPlainText();

    // Settled before the controller is told, because telling it reports a
    // document change straight back here, and the guard in refresh() has to see
    // a panel with nothing outstanding or it would decline to reread itself.
    m_saved = fingerprint();
    m_controller->setMetadata(metadata);
}

} // namespace milah
