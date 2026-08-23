#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace milah {

class TranscriptionController;

/// How far this folio and this hand are from being worth training on.
///
/// **The path existed and was not visible.** Read a folio, pour a transcription
/// onto it, check every word of a line, save the folio, do that until a set
/// holds fifty lines, then train. Every one of those steps had a home — a
/// right-click on the picture, a key on the line, an entry three levels into
/// File — and nothing anywhere said what the sequence was or how far along it
/// you were. The counts existed too: trainableLineCount() has always known how
/// many of this folio's lines are finished, and it was spent on greying out a
/// menu item nobody had opened.
///
/// So: two figures and a button. How many of this folio's lines are finished,
/// how many the hand's set holds against the fifty that open training, and the
/// one thing worth pressing next.
///
/// It also says when a folio has been corrected since it was saved. Saving
/// replaces a folio's lines rather than adding beside them, so putting that
/// right costs one press — the only thing ever missing was being told.
class TranscriptionTrainingWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TranscriptionTrainingWidget(
        TranscriptionController *controller,
        QWidget *parent = nullptr);

private:
    /// Where this folio stands on the path, which is what the button is.
    ///
    /// A plain enum rather than a QAction per state: only one of them is ever
    /// offered, and the thing that changes is which. `Nothing` is the resting
    /// state — the button still says Save, greyed unless there is something to
    /// save, because that is what it will be next whatever happens.
    enum class Step {
        Nothing,
        Read,
        Fill,
        Continue,
        Save,
        Train,
    };

    /// Does whatever the folio's step is, by calling what the toolbar and the
    /// right-click call.
    void act();
    /// Reads the folio and the set and redraws. Never called straight from a
    /// signal — see m_soon.
    void refresh();

    TranscriptionController *m_controller = nullptr;
    QLabel *m_folioSaid = nullptr;
    QProgressBar *m_folioBar = nullptr;
    QLabel *m_setSaid = nullptr;
    QProgressBar *m_setBar = nullptr;
    QLabel *m_state = nullptr;
    QPushButton *m_do = nullptr;
    Step m_step = Step::Nothing;
    /// Coalesces the refreshes. Every keystroke in the grid reports the verses
    /// changed, and answering each one means building this folio's whole
    /// training layout to count its finished lines — a third of a megabyte of
    /// XML per letter typed. A quarter of a second's wait costs nothing anybody
    /// can see and turns that into one.
    QTimer *m_soon = nullptr;
};

} // namespace milah
