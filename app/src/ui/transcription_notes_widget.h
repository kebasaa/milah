#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace milah {

class TranscriptionController;

/// The transcriber's own remark on a word.
///
/// The comparison's Notes panel shows two things — what the manuscripts say
/// about a word, and what the editor says back. Here there is only the second:
/// the manuscript is the folio on screen, and it has no notes of its own to
/// read. So this is that panel with its upper half absent.
class TranscriptionNotesWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TranscriptionNotesWidget(
        TranscriptionController *controller,
        QWidget *parent = nullptr);

public slots:
    /// Takes the caret, for "Add/edit note" in the word's context menu.
    void beginEditing();

private:
    void refresh();
    void save();
    /// Save lights only when the box differs from what was last read or
    /// written, so a panel merely looked at does not offer to write over itself.
    void updateSaveState();

    TranscriptionController *m_controller = nullptr;

    QLabel *m_heading = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_save = nullptr;

    /// What the note said when it was last loaded or saved.
    QString m_saved;
};

} // namespace milah
