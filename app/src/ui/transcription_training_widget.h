#pragma once

#include <QWidget>

class QFrame;
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
/// right costs one press — the only thing ever missing was being told. And it
/// says so again on the way out of such a folio, which is the moment the
/// correction is most likely to be forgotten.
///
/// **Which lines, and why.** A count of finished lines does not say what to do
/// about the rest, and the reasons a line is not taught are not all "somebody
/// has to check it": a word typed into it, a note in its middle, a sliver too
/// short to cut. Under the count, each kind is named and links to the next line
/// of that kind.
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
        /// Nothing on the folio is finished any more, and the set still holds
        /// lines of it from before. Saving takes them out.
        Withdraw,
        Train,
    };

    /// Does whatever the folio's step is, by calling what the toolbar and the
    /// right-click call.
    void act();
    /// Reads the folio and the set and redraws. Never called straight from a
    /// signal — see m_soon.
    void refresh();
    /// A link in the breakdown: shows the next line refused for that reason,
    /// counting on from the word the caret is in.
    void showNextRefused(int reason);
    /// The notice for a folio just left with corrections the set lacks.
    void offerUpdate(int page, const QString &entry, const QString &label);
    void hideNotice();

    TranscriptionController *m_controller = nullptr;
    QLabel *m_folioSaid = nullptr;
    QProgressBar *m_folioBar = nullptr;
    /// "31 ready · 3 unchecked · 1 typed word", each refusal a link.
    QLabel *m_breakdown = nullptr;
    QLabel *m_setSaid = nullptr;
    QProgressBar *m_setBar = nullptr;
    QLabel *m_state = nullptr;
    QPushButton *m_do = nullptr;
    /// Training, offered on its own whenever the hand has enough for it.
    ///
    /// The main button is what comes next *for this folio*, which meant training
    /// was reachable from it only on a folio that happened to be saved and
    /// unedited since — so a set of two hundred lines could sit there with the
    /// button saying "Read this folio". Hidden while the main button is already
    /// Train, so the two never say the same thing twice.
    QPushButton *m_train = nullptr;
    Step m_step = Step::Nothing;
    /// Coalesces the refreshes. Every keystroke in the grid reports the verses
    /// changed, and answering each one means building this folio's whole
    /// training layout to count its finished lines — a third of a megabyte of
    /// XML per letter typed. A quarter of a second's wait costs nothing anybody
    /// can see and turns that into one.
    QTimer *m_soon = nullptr;

    /// The folio the panel last described, and whether the set was behind it
    /// then — which is what is known about a folio at the moment it is left.
    int m_lastPage = -1;
    QString m_lastEntry;
    QString m_lastLabel;
    bool m_lastBehind = false;
    /// The notice offering to bring a folio just left into the set, and which
    /// folio it is about. Checked against the entry before acting, because the
    /// document can change under a notice nobody has answered.
    QFrame *m_notice = nullptr;
    QLabel *m_noticeSaid = nullptr;
    int m_noticePage = -1;
    QString m_noticeEntry;
};

} // namespace milah
