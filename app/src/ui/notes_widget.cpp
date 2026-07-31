#include "ui/notes_widget.h"

#include "app_controller.h"

#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Enough to read a remark or two without the panel taking over the dock; more
/// than that scrolls.
constexpr int BodyHeight = 130;
/// Room to write a couple of sentences without the box dominating the dock.
constexpr int EditorHeight = 80;

QString escape(const QString &text)
{
    return text.toHtmlEscaped();
}

} // namespace

NotesWidget::NotesWidget(AppController *controller, QWidget *parent)
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

    m_body = new QTextBrowser;
    m_body->setOpenExternalLinks(false);
    m_body->setOpenLinks(false);
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->setMinimumHeight(BodyHeight);
    boxLayout->addWidget(m_body);

    // The editor's own remark sits below what the manuscripts say, because it
    // is a reply to them rather than one of them.
    auto *mine = new QLabel(QStringLiteral("<b>My note</b>"));
    mine->setTextFormat(Qt::RichText);
    boxLayout->addWidget(mine);

    m_editor = new QPlainTextEdit;
    m_editor->setMinimumHeight(EditorHeight);
    m_editor->setPlaceholderText(
        QStringLiteral("Your own remark on this word."));
    boxLayout->addWidget(m_editor);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &NotesWidget::updateSaveState);

    m_save = new QPushButton(QStringLiteral("Save note"));
    connect(m_save, &QPushButton::clicked, this, &NotesWidget::save);
    boxLayout->addWidget(m_save, 0, Qt::AlignRight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(box);

    connect(
        m_controller,
        &AppController::selectionChanged,
        this,
        &NotesWidget::refresh);
    // The words themselves can change under a standing selection — a reading
    // chosen, a word retyped — and the heading names the word.
    connect(m_controller, &AppController::verseChanged, this, &NotesWidget::refresh);
    connect(m_controller, &AppController::sourcesChanged, this, &NotesWidget::refresh);
    connect(
        m_controller,
        &AppController::noteEditingRequested,
        this,
        &NotesWidget::beginEditing);

    refresh();
}

void NotesWidget::beginEditing()
{
    if (!m_editor->isEnabled()) {
        return;
    }
    m_editor->setFocus(Qt::OtherFocusReason);
    m_editor->moveCursor(QTextCursor::End);
}

void NotesWidget::save()
{
    const WordSelection selected = m_controller->selection();
    if (!selected.isValid()) {
        return;
    }
    m_controller->setCombinedNote(
        selected.verseId, selected.columnIndex, m_editor->toPlainText());
}

void NotesWidget::updateSaveState()
{
    const bool editable = m_editor->isEnabled();
    m_save->setEnabled(editable && m_editor->toPlainText().trimmed() != m_saved);
}

void NotesWidget::refresh()
{
    const WordSelection selected = m_controller->selection();
    const QList<ColumnNote> notes = m_controller->selectedNotes();

    if (!selected.isValid()) {
        m_heading->setText(QStringLiteral(
            "<i>Select a word in the Combined row to see any notes on it.</i>"));
        m_body->clear();
        m_saved.clear();
        // Blocked so that emptying the box does not read as an edit and leave
        // Save looking ready to write over nothing.
        const QSignalBlocker blocker(m_editor);
        m_editor->clear();
        m_editor->setEnabled(false);
        updateSaveState();
        return;
    }

    const QString word = m_controller->selectedWord();
    m_heading->setText(word.isEmpty()
        ? QStringLiteral("<b>This place in the verse</b>")
        : QStringLiteral("<b>%1</b>").arg(escape(word)));

    if (notes.isEmpty()) {
        // Not a fault, and worth saying plainly: most words carry no note.
        m_body->setHtml(QStringLiteral(
            "<p style='color:gray'>No manuscript notes on this word.</p>"));
    } else {
        const QHash<QString, QString> acronyms = m_controller->acronyms();

        QString html;
        QString lastSource;
        for (const ColumnNote &entry : notes) {
            if (entry.sourceId != lastSource) {
                html += QStringLiteral("<p style='margin:8px 0 2px 0'><b>%1</b></p>")
                            .arg(escape(acronyms.value(entry.sourceId, entry.sourceId)));
                lastSource = entry.sourceId;
            }
            const QString number = entry.note.number.isEmpty()
                ? QString()
                : QStringLiteral("<b>%1.</b> ").arg(escape(entry.note.number));
            html += QStringLiteral("<p style='margin:0 0 4px 0'>%1%2</p>")
                        .arg(number, escape(entry.note.text));
        }
        m_body->setHtml(html);
    }

    m_saved = m_controller->combinedNote(selected.verseId, selected.columnIndex);
    m_editor->setEnabled(true);
    if (m_editor->toPlainText() != m_saved) {
        const QSignalBlocker blocker(m_editor);
        m_editor->setPlainText(m_saved);
    }
    updateSaveState();
}

} // namespace milah
