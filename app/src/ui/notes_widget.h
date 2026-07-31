#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace milah {

class AppController;

/// The notes on the word being worked on: what the manuscripts say about it,
/// and what the editor has to say themselves.
///
/// Witness rows show a note as an underline and a tooltip, which is enough to
/// say a note exists but a poor place to read a paragraph of commentary. This
/// follows the Combined word that has the focus and gives the note room to be
/// read, grouped by the witness it came from — a column may be annotated by
/// more than one, and they need not agree. Beneath them the editor writes their
/// own, which belongs to the edition rather than to any manuscript.
class NotesWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit NotesWidget(AppController *controller, QWidget *parent = nullptr);

    /// Redraws from the controller's current selection.
    void refresh();
    /// Puts the caret in the editor, for the "Add/edit note" menu entry.
    void beginEditing();

private:
    void save();
    /// Enables Save only where there is a word to attach a note to and the
    /// text has actually changed, so the button never looks live over nothing.
    void updateSaveState();

    AppController *m_controller = nullptr;
    QLabel *m_heading = nullptr;
    QTextBrowser *m_body = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_save = nullptr;
    /// What the selected word's note said when it was last loaded or saved.
    QString m_saved;
};

} // namespace milah
