#pragma once

#include "core/transcription.h"

#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

class QLineEdit;
class QPainter;

namespace milah {

/// One line of the folio as the line view draws it: which words are on it, and
/// where each one's reading is written under the ink.
///
/// Worked out in one place and used by two, because the second is a click. Text
/// laid out by QPainter's own bidi engine cannot be measured back into words —
/// a Hebrew line drawn with drawText() puts word three somewhere the caller has
/// no way to ask about — so the line view places every word itself, in the
/// direction its boxes say the line runs, and keeps the rectangles. Drawing and
/// hit-testing then read the same answer, and a click cannot land on a word that
/// is not the one under the pointer.
struct DrawnLine
{
    /// The recogniser's index for the line, which is what a word carries.
    int line = 0;
    /// Indices into the view's own word list, in reading order, and where each
    /// one's reading is written in widget coordinates. Parallel.
    QList<int> words;
    QList<QRectF> at;
    /// The rectangle round the line's word boxes, in widget coordinates.
    QRectF box;
    /// What the reading had to shrink to in order to fit under the line, in
    /// points. A folio line can carry twenty-six words.
    qreal pointSize = 0.0;
    bool allMarginal = false;
    bool anyUnchecked = false;
};

/// The folio being transcribed, with a magnifier the reader moves over it.
///
/// The first widget in Milah to paint itself. It has to be: a loupe is a second
/// view of the same pixels at a different scale in the same place, which no
/// arrangement of child widgets gives you. Everything else here follows from
/// that — the image is kept as a QImage rather than a QPixmap because the loupe
/// samples the source, and only the part of the widget the loupe has moved
/// across is repainted, because a full repaint of a forty-megapixel scan on
/// every mouse move would crawl.
class ManuscriptImageView final : public QWidget
{
    Q_OBJECT

public:
    explicit ManuscriptImageView(QWidget *parent = nullptr);

    /// Shows the decoded bytes of a folio. Empty bytes clear the view; bytes
    /// nothing installed can decode leave a notice rather than a blank page,
    /// because a scan that silently fails to appear reads as a broken program.
    ///
    /// `unavailable` says why there are no bytes, for a folio that was meant to
    /// arrive and did not. Without it this view cannot tell a scan whose folio
    /// failed to fetch from no document at all — they are both an empty
    /// QByteArray — and it used to answer a library's 404 by inviting the
    /// transcriber to open the file they already had open.
    ///
    /// Drops any word overlay: the boxes belonged to the folio being replaced.
    /// Call setWords() after this, never before.
    void setImageData(
        const QByteArray &bytes,
        const QString &name,
        const QString &unavailable = QString());
    void clear();

    bool hasImage() const { return !m_image.isNull(); }

    /// Whether the magnifier follows the pointer. Off by default: a loupe under
    /// the cursor at all times would be in the way of reading the page whole.
    bool magnifierEnabled() const { return m_magnifying; }

    /// The words to draw over the ink they were read from.
    ///
    /// Their boxes are in the folio image's own pixels, which is the invariant
    /// TranscribedWord::box states and TranscriptionController::applyRecognition
    /// establishes — a layout file measured against a differently sized copy is
    /// converted there, once, rather than being carried around in two spaces at
    /// the risk of a box scaled by the wrong factor landing somewhere plausible
    /// and wrong.
    ///
    /// Words without a box are dropped here rather than at every place that
    /// draws one, so hasWordBoxes() is the whole of the question the eye button
    /// has to ask.
    void setWords(const QList<TranscribedWord> &words);

    /// The lines the segmenter drew, for the line view to take its shapes from.
    ///
    /// Separate from setWords() because they arrive together but mean different
    /// things: a word has a box because something read it, while a line has a
    /// boundary only if the folio was recognised by a version of Milah that kept
    /// one. A folio with words and no lines still gets line boxes — drawn round
    /// its words instead — which is why this may be empty and the view carries on.
    void setLines(const QList<TranscribedLine> &lines);

    /// Where the folio before this one stopped filling — "Jas 1:25" — or empty
    /// where none did.
    ///
    /// Held here only so the right-click can name the place it would carry on
    /// from. A menu entry reading "continue" without saying from where asks the
    /// transcriber to trust it blindly, and the whole point of this round is
    /// that continuing is chosen rather than assumed.
    void setContinuation(const QString &where);

    /// True when there is anything to show. An eye that toggles nothing is
    /// worse than a grey one, so the toolbar asks this before offering it.
    bool hasWordBoxes() const { return !m_words.isEmpty() && !m_image.isNull(); }
    bool overlayVisible() const { return m_overlayVisible; }
    /// True when anything on the folio carries a line number, which is what the
    /// line view needs and the toolbar asks before offering it. A folio can have
    /// boxes and no lines — an imported layout that numbered nothing — and there
    /// the switch stays grey rather than turning the overlay off.
    bool hasLines() const;
    bool lineBoxesVisible() const { return m_lineBoxes; }

    QSize sizeHint() const override;
    /// The image is scaled to the width it is given, so its height depends on
    /// that width — which is what a scroll area needs to be told.
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

public slots:
    void setMagnifierEnabled(bool enabled);
    void setOverlayVisible(bool visible);
    /// Draws the folio's lines instead of its words. The words keep their jobs —
    /// correcting one reading, holding one box out of the work — and the line
    /// view answers the other question: whether the segmenter's idea of a line
    /// is the manuscript's. On a hand it was not trained for it often is not,
    /// and nothing in the word boxes says so, because each box looks right on
    /// its own and only the grouping is wrong.
    void setLineBoxesVisible(bool visible);

signals:
    /// The transcriber has pointed at where a published transcription should
    /// start being laid down, in the folio image's own pixels.
    ///
    /// **A place and not a line number.** On a folio nothing has read there are
    /// no boxes to name a line with, and that is exactly when the fill is most
    /// wanted — it runs the recognition itself. So the point is what travels,
    /// and it is turned into a line once there are lines, by lineAtPoint() in
    /// core/line_fill.h.
    ///
    /// `carryOn` says which of the two entries was taken: continuing the
    /// transcription the last folio was filled from, or starting a new one.
    ///
    /// About where the *pour* begins and not about the folio: a leaf commonly
    /// opens with the end of the book before it, and that text stays.
    void fillRequested(QPoint folioPixel, bool carryOn);

    /// The transcriber has said this box is on the leaf but outside the work —
    /// a marginal note, a catchword, a running header — or has taken that back.
    ///
    /// The box names the word, because a click on the picture is what this has
    /// and a position in somebody else's flattened list would mean nothing here.
    void marginalToggled(QRect box, bool marginal);

    /// A word corrected on the picture itself. The box names it, as above.
    void wordEdited(QRect box, QString hebrew);

    /// The transcriber has said the manuscript's line ends after this word,
    /// or has taken that back — the segmenter ran two lines together and this is
    /// where the first of them stops.
    ///
    /// Straight onto TranscribedWord::endsLine, which the training export has
    /// honoured all along; until now it could only be reached from the text
    /// below, where there is nothing to judge it against.
    void lineBreakToggled(QRect box, bool endsLine);

    /// The transcriber has said this line and the next are one line of the
    /// manuscript — the segmenter cut one line into two pieces.
    ///
    /// The line number and not a box, because what is being joined is not a word
    /// and there may be no box at all where the click landed.
    void lineJoinRequested(int line);

    /// The whole line is on the leaf but outside the work: a margin note, a
    /// running header, a column of catchwords. Every word of it at once, because
    /// marking a note of nine words one box at a time is the fiddliest thing in
    /// the program.
    void lineMarginalToggled(int line, bool marginal);

    /// The transcriber has said the poured text's line ends **before** this word
    /// — the segmenter drew more boxes on the line than the manuscript has words
    /// there, so everything from here down belongs on the next line.
    ///
    /// **Not lineBreakToggled().** That says where a line of the manuscript ends
    /// inside one thing the segmenter drew, and deliberately moves no text: the
    /// words are already on the right boxes. This says the boxes hold fewer
    /// words than were poured onto them, and the passage below has to be laid
    /// again. One is about the picture, the other about the text.
    void lineBrokenBefore(QRect box);

    /// The other direction: this word belongs on the line above, because the
    /// segmenter drew fewer boxes on that line than the manuscript has words on
    /// it. One word per press, which is how a person corrects a count they are
    /// reading off a picture.
    void wordPulledUp(QRect box);

protected:
    void paintEvent(QPaintEvent *event) override;
    /// Claims the height the folio needs at the width it has been given, so the
    /// scroll area around it knows there is more page below.
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    /// Picking a word out of the reading the line view writes, so that a key can
    /// then say something about it.
    void mousePressEvent(QMouseEvent *event) override;
    /// Enter, Backspace, the arrows and Escape, over the word that is selected.
    /// Reached only in the line view, which is the only state this widget takes
    /// the focus in — see setLineBoxesVisible().
    void keyPressEvent(QKeyEvent *event) override;
    /// The one menu the folio itself carries: where the transcription starts.
    void contextMenuEvent(QContextMenuEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    /// Escape, for the word editor. A QLineEdit says nothing about it on its
    /// own, and a correction you have thought better of has to be abandonable.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Where the folio is drawn: the whole widget width, the image's aspect
    /// ratio, and nothing above or below it.
    QRect pageRect() const;
    /// The circle the loupe occupies at `centre`, in widget coordinates.
    QRect loupeRect(const QPoint &centre) const;
    /// Opens the little editor over `box`, in the folio's own pixels.
    ///
    /// Over the box rather than in a dialog because the ink being read is
    /// directly underneath it, and a window in the middle of the screen puts the
    /// evidence behind the question.
    void editWordAt(const QRect &box, const QString &hebrew);
    /// Takes the editor down, keeping what was typed or throwing it away.
    void closeEditor(bool keep);
    /// Where the editor sits for the box it is on, in widget coordinates. Wide
    /// enough to type in whatever the box's own size, and kept on the pane.
    QRect editorGeometry(const QRect &box) const;
    /// Draws the recognised words over `page`. `damage` is the region being
    /// repainted: the loupe asks for a small one on every mouse move, and a
    /// folio can carry several hundred words, none of which is worth measuring
    /// to draw somewhere that is not being painted.
    void drawWordOverlay(QPainter &painter, const QRect &page, const QRect &damage) const;
    /// Draws one box per line instead of one per word: the segmenter's own
    /// boundary where the folio has it, the rectangle round the line's words
    /// where it does not, and the line's whole reading above it.
    void drawLineOverlay(QPainter &painter, const QRect &page, const QRect &damage) const;
    /// Every line the line view writes, with each word's reading placed. See
    /// DrawnLine for why the placement is done here rather than left to
    /// QPainter. `page` is where the folio is drawn, in widget coordinates.
    QList<DrawnLine> readingLayout(const QRect &page) const;
    /// Moves the selection `by` words along the folio in reading order, and
    /// scrolls nothing — the folio is one pane and the selection is on it.
    /// A word that has been elided off the end of its line is still reachable
    /// this way, which is what makes eliding acceptable at all.
    void selectBy(int by);

    QImage m_image;
    QString m_name;
    /// Set when the bytes could not be decoded, so the widget can say which
    /// file and why rather than showing nothing.
    QString m_failure;

    /// Only words that have a box, so drawing never has to ask.
    QList<TranscribedWord> m_words;
    /// The segmenter's line shapes, keyed nowhere — searched by index, of which
    /// a folio has a few dozen. Empty for a folio recognised before Milah kept
    /// them, and for any imported layout without shapes.
    QList<TranscribedLine> m_lines;
    /// Where an earlier folio's fill stopped, for the menu to name. Empty when
    /// there is nothing to carry on from.
    QString m_continuation;
    /// The editor, alive only while a word is being corrected, and the box it
    /// belongs to — which is what names the word when it commits.
    QLineEdit *m_editor = nullptr;
    QRect m_editing;
    bool m_overlayVisible = false;
    /// Lines instead of words. Off by default: the word boxes are what a
    /// transcriber checking a fill reads, and the line view is the second look.
    bool m_lineBoxes = false;
    /// The word a key would act on, as its box in the folio's own pixels, or
    /// null where nothing is selected. A box names a word everywhere else in
    /// this widget and does here too, so the controller resolves it by the same
    /// route as a marginal mark or a correction.
    QRect m_selected;

    bool m_magnifying = false;
    /// Where the loupe is, and whether it is on screen at all. An invalid point
    /// means the pointer has left the folio.
    QPoint m_loupeCentre;
    bool m_loupeVisible = false;
    double m_magnification = 3.0;
};

} // namespace milah
