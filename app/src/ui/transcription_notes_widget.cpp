#include "ui/transcription_notes_widget.h"

#include "transcription_controller.h"

#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Room to write a couple of sentences about a word without the box taking
/// over the dock.
constexpr int EditorHeight = 90;

} // namespace

TranscriptionNotesWidget::TranscriptionNotesWidget(
    TranscriptionController *controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    auto *box = new QGroupBox(QStringLiteral("Notes"));
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(6);

    m_heading = new QLabel;
    m_heading->setWordWrap(true);
    m_heading->setTextFormat(Qt::RichText);
    boxLayout->addWidget(m_heading);

    m_editor = new QPlainTextEdit;
    m_editor->setMinimumHeight(EditorHeight);
    m_editor->setPlaceholderText(
        QStringLiteral("Your own remark on this word — a doubtful letter, a "
                       "correction, anything the text alone cannot carry."));
    boxLayout->addWidget(m_editor);
    connect(
        m_editor,
        &QPlainTextEdit::textChanged,
        this,
        &TranscriptionNotesWidget::updateSaveState);

    m_save = new QPushButton(QStringLiteral("Save note"));
    connect(m_save, &QPushButton::clicked, this, &TranscriptionNotesWidget::save);
    boxLayout->addWidget(m_save, 0, Qt::AlignRight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(box);

    connect(
        m_controller,
        &TranscriptionController::selectionChanged,
        this,
        &TranscriptionNotesWidget::refresh);
    // The word itself can change under a standing selection — retyped, divided,
    // joined — and the heading names the word.
    connect(
        m_controller,
        &TranscriptionController::versesChanged,
        this,
        &TranscriptionNotesWidget::refresh);
    connect(
        m_controller,
        &TranscriptionController::pageChanged,
        this,
        &TranscriptionNotesWidget::refresh);
    connect(
        m_controller,
        &TranscriptionController::noteEditingRequested,
        this,
        &TranscriptionNotesWidget::beginEditing);

    refresh();
}

void TranscriptionNotesWidget::beginEditing()
{
    if (!m_editor->isEnabled()) {
        return;
    }
    m_editor->setFocus(Qt::OtherFocusReason);
    m_editor->moveCursor(QTextCursor::End);
}

void TranscriptionNotesWidget::save()
{
    if (m_controller->selectedVerse() < 0) {
        return;
    }
    m_controller->setNote(
        m_controller->selectedVerse(),
        m_controller->selectedColumn(),
        m_editor->toPlainText());
}

void TranscriptionNotesWidget::updateSaveState()
{
    m_save->setEnabled(
        m_editor->isEnabled() && m_editor->toPlainText().trimmed() != m_saved);
}

void TranscriptionNotesWidget::refresh()
{
    const QString word = m_controller->selectedWord();
    const bool addressable = m_controller->selectedVerse() >= 0;

    if (!addressable) {
        m_heading->setText(QStringLiteral(
            "<i>Right-click a word to write a remark on it.</i>"));
        // Blocked, so emptying the box does not read as an edit and leave Save
        // looking ready to write over nothing.
        const QSignalBlocker blocker(m_editor);
        m_editor->clear();
        m_editor->setEnabled(false);
        m_saved.clear();
        updateSaveState();
        return;
    }

    // A word still being typed has no text yet, and is still a word to remark
    // on — "the letter here is doubtful" is a note about an empty cell.
    m_heading->setText(word.isEmpty()
        ? QStringLiteral("<i>This word</i>")
        : QStringLiteral("<b>%1</b>").arg(word.toHtmlEscaped()));

    m_saved = m_controller->selectedNote();
    m_editor->setEnabled(true);
    if (m_editor->toPlainText() != m_saved) {
        const QSignalBlocker blocker(m_editor);
        m_editor->setPlainText(m_saved);
    }
    updateSaveState();
}

} // namespace milah
