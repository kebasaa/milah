#include "ui/manuscript_image_view.h"

#include "core/line_fill.h"

#include <QBuffer>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QImageReader>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace milah {
namespace {

/// The loupe's diameter on screen. Big enough to hold a word of pointed Hebrew
/// at three times its drawn size, small enough to leave the page around it
/// readable — the point of a loupe is to look at one thing without losing
/// where it sits.
constexpr int LoupeDiameter = 220;
constexpr double MinMagnification = 1.5;
constexpr double MaxMagnification = 12.0;
/// The widget is useless below this, and a scroll area will happily squeeze it
/// there. Kept modest all the same: a stacked layout takes its minimum from
/// every page it holds, so anything demanded here would pin the whole window
/// wider — including for the verse comparison, which needs the width it can get.
constexpr int MinimumPageWidth = 240;

/// Below this the label would be an ellipsis and nothing else, and a folio
/// carries hundreds of them.
constexpr double MinimumLabelWidth = 14.0;
/// How opaque the strip behind a label is. Enough to read black text on, little
/// enough to see that there is ink underneath — which is the whole point of
/// having put it there.
constexpr int LabelBackingAlpha = 205;
/// The wash over a box held out of the work. Faint: it has to say "not the text"
/// without hiding the very ink the transcriber is reading to decide what the
/// note says.
constexpr int MarginalWashAlpha = 46;

/// The chip the line number sits in, at the leading edge of a line's label.
/// Wide enough for two digits, which is every line of every folio Milah has
/// seen; a third widens the chip rather than being cut off.
/// The smallest the line view will write a reading at. Below this it is a row
/// of grey marks rather than words, and the arrows reach whatever the line was
/// too crowded to fit.
constexpr double MinimumReadingPoint = 5.5;
/// How far from the line its reading sits. Close enough to belong to it, clear
/// enough not to sit on the ascenders. Over the line, except at the top of the
/// folio where there is nothing over it -- see readingLayout().
constexpr double ReadingGap = 3.0;
/// How solid the strip behind a line's reading is, of 255.
///
/// It lies in the gap beside the line, and on a hand whose lines interleave it
/// lands across a neighbour's descenders as well — so it has to lighten what is
/// beneath it rather than blot it out. Enough to read black letters on, little
/// enough to see the ink through: the same bargain the word overlay's
/// LabelBackingAlpha makes, struck further towards the folio because this one
/// is over writing rather than beside it.
constexpr int ReadingBackingAlpha = 150;

/// The smallest the word editor is allowed to be. A recogniser's box round a
/// two-letter word is a dozen pixels across, which is a box you cannot type in.
constexpr int MinimumEditorWidth = 120;
constexpr int MinimumEditorHeight = 26;

} // namespace

ManuscriptImageView::ManuscriptImageView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("manuscriptImage"));
    setMinimumWidth(MinimumPageWidth);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    // Nothing is typed here, and the transcriber is typing constantly: taking
    // the focus on a click would cost them their place in the text.
    setFocusPolicy(Qt::NoFocus);
}

void ManuscriptImageView::setImageData(
    const QByteArray &bytes,
    const QString &name,
    const QString &unavailable)
{
    m_name = name;
    m_failure.clear();
    m_loupeVisible = false;
    // A different folio: the box the editor is on is somewhere else entirely
    // now, so what was half-typed cannot be committed to it.
    closeEditor(false);
    // A different folio, so the boxes read off the last one are not merely
    // stale, they are somewhere else entirely. Whoever shows a folio says what
    // is on it, in that order.
    m_words.clear();

    if (bytes.isEmpty()) {
        // A folio that was meant to be here and is not says so. Empty is
        // otherwise indistinguishable from no document, and paintEvent's
        // no-document text — "File ▸ Open Image starts a transcription" — is
        // exactly the wrong thing to read while sitting on folio 12r of an open
        // manuscript.
        m_failure = unavailable;
        m_image = QImage();
        setMinimumHeight(0);
        updateGeometry();
        update();
        return;
    }

    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    m_image = reader.read();

    if (m_image.isNull()) {
        // Which file and why, because the commonest cause is a format this
        // build has no plugin for — TIFF, most often — and "nothing appeared"
        // gives the transcriber nothing to act on.
        m_failure = QStringLiteral("%1 could not be read.\n%2")
                        .arg(name.isEmpty() ? QStringLiteral("The image") : name)
                        .arg(reader.errorString());
    }

    setMinimumHeight(m_image.isNull() ? 0 : heightForWidth(width()));
    updateGeometry();
    update();
}

void ManuscriptImageView::clear()
{
    setImageData(QByteArray(), QString());
}

void ManuscriptImageView::editWordAt(const QRect &box, const QString &hebrew)
{
    const QRect page = pageRect();
    if (m_image.isNull() || box.isNull() || page.isEmpty()) {
        return;
    }
    closeEditor(false);

    m_editing = box;
    m_editor = new QLineEdit(hebrew, this);
    m_editor->setGeometry(editorGeometry(box));
    m_editor->installEventFilter(this);
    // The hand is Hebrew and so is the correction.
    m_editor->setLayoutDirection(Qt::RightToLeft);
    m_editor->setAlignment(Qt::AlignRight);
    m_editor->selectAll();
    m_editor->show();
    m_editor->setFocus(Qt::OtherFocusReason);

    connect(m_editor, &QLineEdit::returnPressed, this, [this] { closeEditor(true); });
    // Clicking away is a commit rather than a discard: somebody who typed a word
    // and then reached for the next one meant to keep it.
    connect(m_editor, &QLineEdit::editingFinished, this, [this] { closeEditor(true); });
}

QRect ManuscriptImageView::editorGeometry(const QRect &box) const
{
    const QRect page = pageRect();
    if (m_image.isNull() || page.isEmpty()) {
        return QRect();
    }
    const double scaleX = double(page.width()) / m_image.width();
    const double scaleY = double(page.height()) / m_image.height();
    QRect where(
        page.x() + qRound(box.x() * scaleX),
        page.y() + qRound(box.y() * scaleY),
        qMax(MinimumEditorWidth, qRound(box.width() * scaleX)),
        qMax(MinimumEditorHeight, qRound(box.height() * scaleY)));
    // Kept on the pane. A word at the very edge of the leaf would otherwise put
    // half its editor outside the window.
    if (where.right() > width() - 2) {
        where.moveRight(width() - 2);
    }
    where.moveLeft(qMax(2, where.left()));
    return where;
}

bool ManuscriptImageView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            closeEditor(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ManuscriptImageView::closeEditor(bool keep)
{
    if (!m_editor) {
        return;
    }
    // Taken out of the way first, so neither the commit below nor anything it
    // sets off can find a half-dismantled editor.
    QLineEdit *editor = m_editor;
    const QRect box = m_editing;
    m_editor = nullptr;
    m_editing = QRect();
    const QString typed = editor->text().trimmed();
    editor->deleteLater();

    if (keep && !box.isNull()) {
        emit wordEdited(box, typed);
    }
}

void ManuscriptImageView::setContinuation(const QString &where)
{
    // Nothing is drawn from it, so no repaint. It is read when a menu opens,
    // which is rare, and kept up to date because the folio can change under it.
    m_continuation = where;
}

void ManuscriptImageView::setWords(const QList<TranscribedWord> &words)
{
    QList<TranscribedWord> boxed;
    boxed.reserve(words.size());
    for (const TranscribedWord &word : words) {
        // A word somebody typed has no box, and most words are typed. Dropping
        // them here is what lets the drawing loop and hasWordBoxes() both be
        // about nothing but geometry.
        if (!word.box.isNull() && !word.hebrew.isEmpty()) {
            boxed.append(word);
        }
    }
    if (boxed == m_words) {
        // Every keystroke reports the verses changed, and a repaint of a folio
        // that has no boxes on it is a repaint of a scan for nothing.
        return;
    }
    m_words = boxed;
    // A selection is a box, and the folio may no longer have that box on it —
    // a re-flow moves every word below the break onto a different one.
    if (!m_selected.isNull()) {
        bool still = false;
        for (const TranscribedWord &word : m_words) {
            if (word.box == m_selected) {
                still = true;
                break;
            }
        }
        if (!still) {
            m_selected = QRect();
        }
    }
    update();
}

void ManuscriptImageView::setLines(const QList<TranscribedLine> &lines)
{
    if (lines == m_lines) {
        return;
    }
    m_lines = lines;
    if (m_lineBoxes) {
        update();
    }
}

bool ManuscriptImageView::hasLines() const
{
    if (m_image.isNull()) {
        return false;
    }
    // Of the words and not of m_lines, because a line box can be drawn round a
    // line's words with no geometry at all — which is every folio read before
    // Milah started keeping what the segmenter drew.
    for (const TranscribedWord &word : m_words) {
        if (word.line >= 0) {
            return true;
        }
    }
    return false;
}

void ManuscriptImageView::setLineBoxesVisible(bool visible)
{
    if (m_lineBoxes == visible) {
        return;
    }
    m_lineBoxes = visible;
    // The one state this widget takes the focus in. It is Qt::NoFocus otherwise
    // because the transcriber is typing constantly and a click on the folio
    // would cost them their place in the text — which holds for the word view,
    // where nothing here is typed, and not for this one, where the folio is what
    // the keys are about.
    setFocusPolicy(visible ? Qt::ClickFocus : Qt::NoFocus);
    if (!visible) {
        m_selected = QRect();
    }
    if (m_overlayVisible) {
        update();
    }
}

void ManuscriptImageView::setOverlayVisible(bool visible)
{
    if (m_overlayVisible == visible) {
        return;
    }
    m_overlayVisible = visible;
    update();
}

void ManuscriptImageView::setMagnifierEnabled(bool enabled)
{
    if (m_magnifying == enabled) {
        return;
    }
    m_magnifying = enabled;
    setMouseTracking(enabled);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    if (!enabled && m_loupeVisible) {
        const QRect stale = loupeRect(m_loupeCentre);
        m_loupeVisible = false;
        update(stale.adjusted(-2, -2, 2, 2));
    }
}

QSize ManuscriptImageView::sizeHint() const
{
    if (m_image.isNull()) {
        return QSize(MinimumPageWidth, 200);
    }
    return m_image.size();
}

int ManuscriptImageView::heightForWidth(int width) const
{
    if (m_image.isNull() || m_image.width() <= 0) {
        return 200;
    }
    return int(std::llround(double(width) * m_image.height() / m_image.width()));
}

void ManuscriptImageView::contextMenuEvent(QContextMenuEvent *event)
{
    const QRect page = pageRect();
    if (m_image.isNull() || !page.contains(event->pos())) {
        QWidget::contextMenuEvent(event);
        return;
    }

    // Back into the folio's own pixels, which is the space every box is in.
    const QPoint folioPixel(
        qRound((event->pos().x() - page.x()) * double(m_image.width()) / page.width()),
        qRound((event->pos().y() - page.y()) * double(m_image.height()) / page.height()));

    QMenu menu(this);

    // The box under the cursor first, because that is what was aimed at. A
    // transcriber who can see what a word says wants to type it there and then,
    // not go looking for it again in the text below.
    // recogniser segments every mark with ink in it, and a published
    // transcription holds the work and not the notes beside it — so saying which
    // boxes are not the work has to happen before a fill, not after it.
    const QRect box = wordAtPoint(m_words, folioPixel);
    if (!box.isNull()) {
        bool marginal = false;
        QString hebrew;
        for (const TranscribedWord &word : m_words) {
            if (word.box == box) {
                marginal = word.marginal;
                hebrew = word.hebrew;
                break;
            }
        }

        QAction *edit = menu.addAction(QStringLiteral("Edit this word…"));
        edit->setToolTip(QStringLiteral(
            "Types over the reading on the picture itself, where the ink you are "
            "reading it against is directly underneath."));
        connect(edit, &QAction::triggered, this, [this, box, hebrew] {
            editWordAt(box, hebrew);
        });

        QAction *hold = menu.addAction(QStringLiteral("Not part of the transcribed text"));
        hold->setCheckable(true);
        hold->setChecked(marginal);
        hold->setToolTip(QStringLiteral(
            "A marginal note, a catchword, a running header — on the leaf, but "
            "not in the work. A fill steps over it instead of pouring a word of "
            "the text onto it, and the exports carry it as a note on its line."));
        connect(hold, &QAction::triggered, this, [this, box](bool marked) {
            emit marginalToggled(box, marked);
        });
        menu.addSeparator();
    }

    // The line the click fell on, and the two ways the segmenter can have got it
    // wrong. Offered in both views on purpose: a fault is usually noticed in the
    // word boxes, where the poured text stops matching the ink, and corrected
    // while looking at the line boxes, where you can see why.
    const int line = lineAtPoint(m_words, folioPixel);
    if (line >= 0) {
        // The word a break would go after. The click is usually inside a box,
        // but a line box is mostly the gaps between its words, and a menu whose
        // first entry is missing because you were four pixels wide of a letter
        // is a menu that has to be aimed at rather than used.
        const TranscribedWord *at = nullptr;
        int nearest = 0;
        bool allMarginal = true;
        bool below = false;
        for (const TranscribedWord &word : m_words) {
            if (word.line > line) {
                below = true;
            }
            if (word.line != line || word.box.isNull()) {
                continue;
            }
            allMarginal = allMarginal && word.marginal;
            if (word.box.contains(folioPixel)) {
                at = &word;
                nearest = -1;
                continue;
            }
            if (nearest < 0) {
                continue;
            }
            const int away = std::min(
                qAbs(word.box.left() - folioPixel.x()),
                qAbs(word.box.right() - folioPixel.x()));
            if (!at || away < nearest) {
                at = &word;
                nearest = away;
            }
        }

        if (at) {
            const QRect wordBox = at->box;
            QAction *ends = menu.addAction(
                QStringLiteral("The line ends after “%1”").arg(at->hebrew));
            ends->setCheckable(true);
            ends->setChecked(at->endsLine);
            ends->setToolTip(QStringLiteral(
                "The segmenter ran two lines of the manuscript together, and the "
                "first of them stops here. Training cuts one strip per line, so a "
                "line that is really two teaches the model a strip with two lines "
                "of ink squashed onto one baseline."));
            connect(ends, &QAction::triggered, this, [this, wordBox](bool broken) {
                emit lineBreakToggled(wordBox, broken);
            });
        }

        if (below) {
            QAction *join = menu.addAction(QStringLiteral("Join with the line below"));
            join->setToolTip(QStringLiteral(
                "The other fault: one line of the manuscript cut into two. "
                "Joining them lays their words out together, which is what puts "
                "two side-by-side pieces back into one right-to-left run."));
            connect(join, &QAction::triggered, this, [this, line] {
                emit lineJoinRequested(line);
            });
        }

        QAction *note =
            menu.addAction(QStringLiteral("This whole line is not part of the transcribed text"));
        note->setCheckable(true);
        note->setChecked(allMarginal);
        note->setToolTip(QStringLiteral(
            "Every word of it at once. A margin note or a running header usually "
            "has a line to itself, and marking it a box at a time is the "
            "fiddliest thing in the program."));
        connect(note, &QAction::triggered, this, [this, line](bool marked) {
            emit lineMarginalToggled(line, marked);
        });
        menu.addSeparator();
    }

    // Which transcription, before where in it. The first folio of a book has
    // nothing to carry on from and gets only the second entry; every folio after
    // it is either a continuation or a fresh start, and which of the two is
    // always asked rather than guessed — from the document they look identical,
    // and guessing wrong lays down the wrong text.
    if (!m_continuation.isEmpty()) {
        QAction *carry = menu.addAction(
            QStringLiteral("Continue from the previous transcription (%1)…")
                .arg(m_continuation));
        carry->setToolTip(QStringLiteral(
            "Carries on the transcription the last filled folio used, from the "
            "verse and word it stopped on, laid in from here down."));
        connect(carry, &QAction::triggered, this, [this, folioPixel] {
            emit fillRequested(folioPixel, true);
        });
    }

    QAction *fill = menu.addAction(
        QStringLiteral("Fill from a new transcription file starting here…"));
    fill->setToolTip(QStringLiteral(
        "Choose a published transcription; its beginning goes on the line you "
        "clicked, and whatever is above is left as it is. A folio nothing has "
        "read yet is read first."));
    // The place, not the line it is currently on: a folio with no boxes yet has
    // no line to name, and that is the folio this is most often used on.
    connect(fill, &QAction::triggered, this, [this, folioPixel] {
        emit fillRequested(folioPixel, false);
    });
    menu.exec(event->globalPos());
    event->accept();
}

QRect ManuscriptImageView::pageRect() const
{
    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0) {
        return QRect();
    }
    // Fitted to the width and never cropped: the folio is scrolled through
    // vertically, so its full width being on screen is what matters and its
    // height is whatever the ratio makes it.
    const QSize scaled = m_image.size().scaled(width(), height(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - scaled.width()) / 2, 0), scaled);
}

QRect ManuscriptImageView::loupeRect(const QPoint &centre) const
{
    return QRect(
        centre.x() - LoupeDiameter / 2,
        centre.y() - LoupeDiameter / 2,
        LoupeDiameter,
        LoupeDiameter);
}

void ManuscriptImageView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_image.isNull()) {
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawText(
            rect().adjusted(24, 24, -24, -24),
            Qt::AlignCenter | Qt::TextWordWrap,
            m_failure.isEmpty()
                ? QStringLiteral("No folio open.\nFile ▸ Open Image starts a transcription.")
                : m_failure);
        return;
    }

    const QRect page = pageRect();
    painter.drawImage(page, m_image);

    // Over the folio and under the loupe, so that magnifying a word the machine
    // read shows the ink rather than the reading of it.
    if (m_overlayVisible && hasWordBoxes()) {
        if (m_lineBoxes && hasLines()) {
            drawLineOverlay(painter, page, event->rect());
        } else {
            drawWordOverlay(painter, page, event->rect());
        }
    }

    if (!m_magnifying || !m_loupeVisible) {
        return;
    }

    const QRect loupe = loupeRect(m_loupeCentre);
    if (!loupe.intersects(event->rect())) {
        return;
    }

    // How much of the source one loupe-width covers: the folio is already drawn
    // at page.width() / image.width(), and the loupe shows it magnification
    // times larger again.
    const double drawnScale = double(page.width()) / m_image.width();
    const double sourceSpan = LoupeDiameter / (drawnScale * m_magnification);

    const QPointF onImage(
        (m_loupeCentre.x() - page.x()) / drawnScale,
        (m_loupeCentre.y() - page.y()) / drawnScale);
    const QRectF source(
        onImage.x() - sourceSpan / 2.0,
        onImage.y() - sourceSpan / 2.0,
        sourceSpan,
        sourceSpan);

    painter.save();
    QPainterPath circle;
    circle.addEllipse(loupe);
    painter.setClipPath(circle);
    // The paper behind it, so a loupe hanging over the edge of the folio shows
    // an edge rather than whatever was on screen before.
    painter.fillRect(loupe, palette().color(QPalette::Base));
    painter.drawImage(QRectF(loupe), m_image, source);
    painter.restore();

    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(loupe);
}

void ManuscriptImageView::drawWordOverlay(
    QPainter &painter,
    const QRect &page,
    const QRect &damage) const
{
    // Two factors rather than the one the folio is drawn with. They are equal
    // whenever the recogniser saw the image Milah holds, which is every folio
    // Transcribe produces — but an ALTO imported from elsewhere may have been
    // made from a copy resized to a slightly different shape, and there the
    // right answer is to stretch the boxes with it rather than to slide them
    // all a little further down the page.
    const double scaleX = double(page.width()) / m_image.width();
    const double scaleY = double(page.height()) / m_image.height();

    QFont labelFont = painter.font();
    labelFont.setPointSizeF(std::max(7.0, labelFont.pointSizeF() * 0.85));
    painter.setFont(labelFont);
    const QFontMetricsF metrics(labelFont);
    const double lineHeight = metrics.height();

    // The same colour the grid's secondary rows use — see acronymColor() in
    // band_grid, which spells it as a stylesheet string because that is what a
    // label wants. Built here rather than parsed from there: `rgba(…, 0.72)` is
    // CSS, and QColor does not read it.
    QColor outline = palette().color(QPalette::Text);
    outline.setAlphaF(0.72);
    QColor backing = palette().color(QPalette::Base);
    backing.setAlpha(LabelBackingAlpha);
    const QColor ink = palette().color(QPalette::Text);

    // Held out of the work: a real grey rather than the text colour, so it reads
    // as struck out at a glance. Dotted alone was too close to the dashed
    // outline every unchecked word already carries — and telling those two apart
    // is the whole reason for marking a box in the first place.
    const QColor dimmed = palette().color(QPalette::Mid);
    QColor wash = dimmed;
    wash.setAlpha(MarginalWashAlpha);

    painter.setBrush(Qt::NoBrush);

    for (const TranscribedWord &word : m_words) {
        const QRectF box(
            page.x() + word.box.x() * scaleX,
            page.y() + word.box.y() * scaleY,
            word.box.width() * scaleX,
            word.box.height() * scaleY);
        if (box.width() < 1.0 || box.height() < 1.0) {
            continue;
        }

        // The label sits above the box, so the region a word can touch is
        // taller than the word.
        const QRect touched =
            box.toAlignedRect().adjusted(-1, -int(lineHeight) - 3, 1, int(lineHeight) + 3);
        if (!touched.intersects(damage)) {
            continue;
        }

        // Dashed until somebody has looked at it. The count in the status line
        // says how many are left; this says which, and where on the folio they
        // are, which is what decides where to read next.
        //
        // Dotted for a box held out of the work, which a fill steps over — the
        // transcriber has to be able to see that before pressing anything, and
        // it must not be mistakable for merely unchecked.
        QPen pen(outline, 1.0);
        if (word.marginal) {
            pen.setColor(dimmed);
            pen.setStyle(Qt::DotLine);
            painter.fillRect(box, wash);
        } else {
            pen.setStyle(word.unchecked ? Qt::DashLine : Qt::SolidLine);
        }
        painter.setPen(pen);
        painter.drawRect(box);

        if (box.width() < MinimumLabelWidth) {
            continue;
        }

        QRectF label(box.x(), box.y() - lineHeight - 2.0, box.width(), lineHeight);
        // A word on the first line of the folio has nothing above it, so its
        // reading goes underneath instead of off the top of the pane.
        if (label.top() < page.top()) {
            label.moveTop(box.bottom() + 2.0);
        }

        painter.fillRect(label, backing);
        // Dimmed rather than hidden. The note's reading is worth being able to
        // read — it is what the transcriber types in to make it training data —
        // and hiding it would make a held-out box look like one the recogniser
        // found nothing in.
        painter.setPen(word.marginal ? dimmed : ink);
        painter.drawText(
            label,
            Qt::AlignCenter,
            metrics.elidedText(word.hebrew, Qt::ElideRight, label.width()));
    }
}

QList<DrawnLine> ManuscriptImageView::readingLayout(const QRect &page) const
{
    QList<DrawnLine> laid;
    if (m_image.isNull() || page.width() <= 0) {
        return laid;
    }

    const double scaleX = double(page.width()) / m_image.width();
    const double scaleY = double(page.height()) / m_image.height();

    // In the order they were read, which for the words of one line is the order
    // they are to be spoken — the fill lays them down that way and the verses
    // keep them that way.
    QMap<int, QList<int>> byLine;
    for (int index = 0; index < m_words.size(); ++index) {
        if (m_words.at(index).line >= 0) {
            byLine[m_words.at(index).line].append(index);
        }
    }

    QFont base = font();
    const qreal wanted = std::max(7.0, base.pointSizeF() * 0.85);

    for (auto entry = byLine.constBegin(); entry != byLine.constEnd(); ++entry) {
        DrawnLine line;
        line.line = entry.key();
        line.words = entry.value();
        line.allMarginal = true;

        QRect bounds;
        QList<QRect> boxes;
        // The lowest of the line's upper edges, which is where its body starts.
        //
        // Not bounds.top(), which is the *highest* point anything on the line
        // reaches: one tall lamed, or one box the segmenter drew generously,
        // and the reading floats that far above the line it belongs to and into
        // the one before it. On 158r that is 18 pixels on line 21 and 16 on line
        // 22, against letters some 30 to 40 tall.
        int lowestTop = 0;
        for (const int index : line.words) {
            const TranscribedWord &word = m_words.at(index);
            bounds = bounds.isNull() ? word.box : bounds.united(word.box);
            lowestTop = std::max(lowestTop, word.box.top());
            if (word.unchecked) {
                line.anyUnchecked = true;
                ++line.unchecked;
            }
            line.allMarginal = line.allMarginal && word.marginal;
            boxes.append(word.box);
        }
        if (bounds.isNull()) {
            continue;
        }
        line.box = QRectF(
            page.x() + bounds.x() * scaleX,
            page.y() + bounds.y() * scaleY,
            bounds.width() * scaleX,
            bounds.height() * scaleY);

        // Shrunk to fit the width of the line it belongs to, down to a floor. A folio
        // line here carries twenty-six words, and a reading wider than the line
        // it is written under stops being a reading of that line.
        base.setPointSizeF(wanted);
        QFontMetricsF metrics(base);
        const double space = metrics.horizontalAdvance(QLatin1Char(' '));
        double total = space * std::max(0, int(line.words.size()) - 1);
        for (const int index : line.words) {
            total += metrics.horizontalAdvance(m_words.at(index).hebrew);
        }
        if (total > line.box.width() && total > 0.0) {
            base.setPointSizeF(std::max(
                MinimumReadingPoint, wanted * line.box.width() / total));
            metrics = QFontMetricsF(base);
        }
        line.pointSize = base.pointSizeF();

        // Placed word by word, in the direction the boxes say the line runs —
        // right to left for Hebrew, left to right for a Latin note beside it,
        // and neither hard-coded when the geometry already says.
        const bool rightToLeft =
            LineFill::directionOf(boxes) == LineFill::Direction::RightToLeft;
        const double height = metrics.height();
        // Over the line rather than under it. Either way the band lands in the
        // gap between two lines of writing; over means it sits under the line
        // *above*, whose descenders are shorter than the next line's ascenders,
        // and it puts the reading and the ink it reads in the order a person
        // scans them.
        //
        // Measured from the line's body — the lowest of its upper edges — and
        // not from bounds.top(); see lowestTop above for why.
        //
        // Except at the top of the folio, where there is no gap to sit in and
        // the band would go off the pane — the first line's reading goes
        // underneath instead, which is what the word overlay has always done
        // with a word on the first line.
        const double body = page.y() + lowestTop * scaleY;
        const double above = body - ReadingGap - height;
        const double top = above < page.top() ? line.box.bottom() + ReadingGap : above;
        double at = rightToLeft ? line.box.right() : line.box.left();
        const double gap = metrics.horizontalAdvance(QLatin1Char(' '));
        for (const int index : line.words) {
            const double width = metrics.horizontalAdvance(m_words.at(index).hebrew);
            const QRectF where(rightToLeft ? at - width : at, top, width, height);
            line.at.append(where);
            at += rightToLeft ? -(width + gap) : width + gap;
        }
        laid.append(line);
    }
    return laid;
}

void ManuscriptImageView::drawLineOverlay(
    QPainter &painter,
    const QRect &page,
    const QRect &damage) const
{
    // The same two factors drawWordOverlay uses, and for the same reason: an
    // imported layout measured against a differently shaped copy has to stretch
    // with the picture rather than slide down it.
    const double scaleX = double(page.width()) / m_image.width();
    const double scaleY = double(page.height()) / m_image.height();
    const auto onScreen = [&](const QPoint &point) {
        return QPointF(page.x() + point.x() * scaleX, page.y() + point.y() * scaleY);
    };

    QColor outline = palette().color(QPalette::Text);
    outline.setAlphaF(0.72);
    const QColor dimmed = palette().color(QPalette::Mid);
    QColor wash = dimmed;
    wash.setAlpha(MarginalWashAlpha);
    // The baseline kraken dewarps along, faint under the words it belongs to.
    // Worth having on the picture: a boundary that looks right round a baseline
    // that has wandered into the line below still teaches the model nonsense,
    // and the boundary alone would not say so.
    QColor spine = palette().color(QPalette::Highlight);
    spine.setAlphaF(0.55);
    // **The two colours in this widget that do not come from the palette**, and
    // the exception is the point. Everything else here is drawn against the
    // window and takes the window's colours. The reading is drawn against the
    // *folio*, which is a pale parchment scan whatever the window is set to — so
    // the palette is the wrong authority, and following it is what produced
    // near-white text in one theme and, once that was fixed with Highlight,
    // saturated blue letters at seven points over brown ink in both.
    //
    // Near-black on a pale wash, fixed. The wash is translucent because it lies
    // in the gap under the line and, on a hand whose lines interleave, across
    // the next line's ascenders: it has to lighten what is beneath it rather
    // than blot it out. ReadingBackingAlpha is the dial.
    const QColor said(0x1a, 0x18, 0x14);
    QColor strip(0xf4, 0xf1, 0xe8);
    strip.setAlpha(ReadingBackingAlpha);
    // Held out of the work: still legible on the same strip, because the note's
    // reading is what gets typed over to make it training data — but plainly
    // not the text.
    const QColor asideSaid(0x6a, 0x64, 0x5c);

    QMap<int, const TranscribedLine *> drawn;
    for (const TranscribedLine &line : m_lines) {
        drawn.insert(line.index, &line);
    }

    painter.setBrush(Qt::NoBrush);

    for (const DrawnLine &line : readingLayout(page)) {
        if (line.box.width() < 1.0 || line.box.height() < 1.0) {
            continue;
        }
        // The reading sits outside the line's own box, so the region a line can
        // touch is the union of the two rather than the box alone.
        QRectF touched = line.box;
        for (const QRectF &at : line.at) {
            touched = touched.united(at);
        }
        if (!touched.toAlignedRect().adjusted(-2, -2, 2, 2).intersects(damage)) {
            continue;
        }

        // The segmenter's own outline where the folio has one — this is the
        // shape the training strip is masked to, so drawing anything else here
        // would be drawing a picture of something that is not being exported.
        // The rectangle round the words is the fallback, and it is also exactly
        // what the export falls back to, so the two never disagree.
        const TranscribedLine *shape = drawn.value(line.line, nullptr);
        QPolygonF boundary;
        if (shape && shape->boundary.size() >= 3) {
            boundary.reserve(shape->boundary.size());
            for (const QPoint &point : shape->boundary) {
                boundary << onScreen(point);
            }
        }

        QPen pen(outline, 1.4);
        if (line.allMarginal) {
            pen.setColor(dimmed);
            pen.setStyle(Qt::DotLine);
            if (boundary.isEmpty()) {
                painter.fillRect(line.box, wash);
            } else {
                painter.setPen(Qt::NoPen);
                painter.setBrush(wash);
                painter.drawPolygon(boundary);
                painter.setBrush(Qt::NoBrush);
            }
        } else {
            pen.setStyle(line.anyUnchecked ? Qt::DashLine : Qt::SolidLine);
        }
        painter.setPen(pen);
        if (boundary.isEmpty()) {
            painter.drawRect(line.box);
        } else {
            painter.drawPolygon(boundary);
        }

        if (shape && shape->baseline.size() >= 2) {
            QPolygonF spineLine;
            spineLine.reserve(shape->baseline.size());
            for (const QPoint &point : shape->baseline) {
                spineLine << onScreen(point);
            }
            painter.setPen(QPen(spine, 1.0));
            painter.drawPolyline(spineLine);
        }

        QFont reading = font();
        reading.setPointSizeF(line.pointSize);
        const QFontMetricsF metrics(reading);

        // The number at the leading edge of the line, beside the reading rather
        // than inside it: a digit dropped into a right-to-left string moves as
        // the bidi algorithm sees fit, and a line labelled 21 that draws its 21
        // in the middle of the Hebrew is worse than no label.
        //
        // The number, and after it what the line still stands away from being
        // worth anything — "21·3" is line twenty-one with three words nobody has
        // looked at. A line that is one word short of counting looks exactly
        // like one that is twenty short otherwise, and the difference is the
        // whole of what decides where to read next.
        const QString number =
            line.unchecked > 0
                ? QStringLiteral("%1·%2").arg(line.line + 1).arg(line.unchecked)
                : QString::number(line.line + 1);
        const double chip = metrics.horizontalAdvance(number) + 4.0;
        // Level with the reading, taken from it rather than worked out again:
        // the reading goes over the line except at the top of the folio, and a
        // number that recomputed that rule could disagree with it.
        const QRectF numberAt(
            line.box.left() - chip - 2.0,
            line.at.isEmpty() ? line.box.bottom() + ReadingGap : line.at.first().top(),
            chip,
            metrics.height());

        // One strip for the whole reading rather than one behind each word.
        // The words are placed with gaps between them, so a backing apiece would
        // come out as a row of chips with the folio showing between; their union
        // reads as a band, which is what a line of text looks like. The number
        // is inside it, so the two are one object rather than two that happen to
        // be adjacent.
        QRectF band = numberAt;
        for (const QRectF &at : line.at) {
            band = band.united(at);
        }
        painter.fillRect(band.adjusted(-2.0, -1.0, 2.0, 1.0), strip);

        painter.setFont(reading);
        painter.setPen(asideSaid);

        painter.drawText(numberAt, Qt::AlignCenter, number);
        for (int at = 0; at < line.words.size(); ++at) {
            const QRectF where = line.at.at(at);
            if (!where.intersects(QRectF(page))) {
                // Off the end of a line too crowded to write out even at the
                // smallest size it is allowed. Reachable with the arrows, which
                // is what makes that acceptable.
                continue;
            }
            const TranscribedWord &word = m_words.at(line.words.at(at));
            const bool chosen = !m_selected.isNull() && word.box == m_selected;

            reading.setBold(chosen);
            reading.setUnderline(chosen);
            painter.setFont(reading);
            // Dimmed rather than hidden for a line held out of the work: the
            // note's reading is what the transcriber types over to make it
            // training data.
            painter.setPen(line.allMarginal ? asideSaid : said);
            painter.drawText(where, Qt::AlignCenter, word.hebrew);
            reading.setBold(false);
            reading.setUnderline(false);

            if (!chosen) {
                continue;
            }
            // Where a break would go, drawn as a caret at the word's leading
            // edge — Enter cuts the line *before* this word, and a highlight
            // round the word alone would not say on which side.
            const bool rightToLeft = line.at.size() < 2 || line.at.at(0).x() > line.at.last().x();
            const double edge = rightToLeft ? where.right() : where.left();
            painter.setPen(QPen(said, 2.0));
            painter.drawLine(
                QPointF(edge, where.top()), QPointF(edge, where.bottom()));
            // And the box it belongs to, on the ink, so the two can be read
            // against each other without hunting.
            painter.setPen(QPen(said, 1.6));
            painter.drawRect(QRectF(
                page.x() + word.box.x() * scaleX,
                page.y() + word.box.y() * scaleY,
                word.box.width() * scaleX,
                word.box.height() * scaleY));
        }
    }
}

void ManuscriptImageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The folio is drawn at the full width it is given, so how tall it is
    // depends on how wide that is. Claiming it as a minimum is what puts a
    // scrollbar beside a page taller than the pane. This settles rather than
    // looping: the claim changes the height, and the height is not what the
    // claim is computed from.
    setMinimumHeight(m_image.isNull() ? 0 : heightForWidth(width()));
    // The folio is drawn to the new width, so the box under the editor has
    // moved. Followed rather than dismissed: losing a half-typed correction to
    // a window resize would be its own small betrayal.
    if (m_editor && !m_editing.isNull()) {
        m_editor->setGeometry(editorGeometry(m_editing));
    }
}

void ManuscriptImageView::mousePressEvent(QMouseEvent *event)
{
    const QRect page = pageRect();
    if (event->button() != Qt::LeftButton || !m_lineBoxes || !m_overlayVisible
        || m_image.isNull() || !hasWordBoxes()) {
        QWidget::mousePressEvent(event);
        return;
    }

    // The written reading first, because that is what the transcriber is looking
    // at — the whole point of writing it under the line is that the words can be
    // compared with the ink, and the one you have just compared is the one you
    // want to say something about. The box on the picture answers too, for a
    // word whose reading was elided off the end of a crowded line.
    QRect found;
    for (const DrawnLine &line : readingLayout(page)) {
        for (int at = 0; at < line.words.size(); ++at) {
            if (line.at.at(at).contains(event->position())) {
                found = m_words.at(line.words.at(at)).box;
                break;
            }
        }
        if (!found.isNull()) {
            break;
        }
    }
    if (found.isNull() && page.contains(event->pos())) {
        const QPoint folioPixel(
            qRound((event->pos().x() - page.x()) * double(m_image.width()) / page.width()),
            qRound((event->pos().y() - page.y()) * double(m_image.height()) / page.height()));
        found = wordAtPoint(m_words, folioPixel);
    }
    if (found.isNull()) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_selected = found;
    // Only now, and only here: the widget is Qt::NoFocus in the word view for a
    // reason that still holds there — see setLineBoxesVisible().
    setFocus(Qt::MouseFocusReason);
    update();
}

void ManuscriptImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_selected.isNull()) {
        QWidget::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit lineBrokenBefore(m_selected);
        event->accept();
        return;
    case Qt::Key_Backspace:
        emit wordPulledUp(m_selected);
        event->accept();
        return;
    case Qt::Key_Space: {
        // Read the line, accept it, move to the next that still needs reading.
        // The whole loop in one key, because that is the shape of the work: look
        // at the line, decide, go on. The line is taken from the selection
        // before the signal, since laying nothing again cannot move it but a
        // future change might.
        const int line = lineOfSelection();
        if (line < 0) {
            break;
        }
        emit lineChecked(line);
        // After the signal, and safe to be: the connection is direct, so by the
        // time this returns the document has changed and the words have come
        // back through setWords().
        selectNextUnread(line);
        event->accept();
        return;
    }
    case Qt::Key_Down:
        selectLineBy(1);
        event->accept();
        return;
    case Qt::Key_Up:
        selectLineBy(-1);
        event->accept();
        return;
    case Qt::Key_Right:
        // Forward and back along the reading, not left and right across the
        // screen. A Hebrew line runs the other way and a Latin note beside it
        // does not, so an arrow that meant "leftwards" would mean two different
        // things on one folio.
        selectBy(1);
        event->accept();
        return;
    case Qt::Key_Left:
        selectBy(-1);
        event->accept();
        return;
    case Qt::Key_Escape:
        m_selected = QRect();
        update();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}


int ManuscriptImageView::lineOfSelection() const
{
    if (m_selected.isNull()) {
        return -1;
    }
    for (const TranscribedWord &word : m_words) {
        if (word.box == m_selected) {
            return word.line;
        }
    }
    return -1;
}

void ManuscriptImageView::selectNextUnread(int after)
{
    for (const DrawnLine &line : readingLayout(pageRect())) {
        if (line.line <= after || line.unchecked == 0 || line.words.isEmpty()) {
            continue;
        }
        m_selected = m_words.at(line.words.first()).box;
        update();
        return;
    }
    // Nothing below it left to read. The selection stays where it is; the
    // controller's message is what says so.
}

void ManuscriptImageView::selectLineBy(int by)
{
    const QList<DrawnLine> laid = readingLayout(pageRect());
    const int here = lineOfSelection();
    int at = -1;
    for (int index = 0; index < laid.size(); ++index) {
        if (laid.at(index).line == here) {
            at = index;
            break;
        }
    }
    if (at < 0) {
        return;
    }
    // Along the folio's own lines rather than by number: a join leaves the
    // numbering with a gap in it, and the line below is whichever one is next.
    const int to = at + by;
    if (to < 0 || to >= laid.size() || laid.at(to).words.isEmpty()) {
        return;
    }
    m_selected = m_words.at(laid.at(to).words.first()).box;
    update();
}

void ManuscriptImageView::selectBy(int by)
{
    // Over the whole folio rather than one line, so the end of a line runs on to
    // the start of the next — which is where a break usually needs looking at.
    QList<QRect> order;
    for (const DrawnLine &line : readingLayout(pageRect())) {
        for (const int index : line.words) {
            order.append(m_words.at(index).box);
        }
    }
    const int at = order.indexOf(m_selected);
    if (at < 0) {
        return;
    }
    const int to = at + by;
    if (to < 0 || to >= order.size()) {
        return;
    }
    m_selected = order.at(to);
    update();
}

void ManuscriptImageView::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);
    if (!m_magnifying || m_image.isNull()) {
        return;
    }

    const QPoint position = event->position().toPoint();
    const bool onPage = pageRect().contains(position);

    // Only what the loupe has moved off and onto is repainted. A full repaint
    // per mouse move would rescale the whole folio each time, which on a real
    // scan is far too slow to follow the pointer.
    QRect damage;
    if (m_loupeVisible) {
        damage = loupeRect(m_loupeCentre);
    }
    if (onPage) {
        damage = damage.isNull() ? loupeRect(position) : damage.united(loupeRect(position));
    }

    m_loupeCentre = position;
    m_loupeVisible = onPage;
    if (!damage.isNull()) {
        update(damage.adjusted(-2, -2, 2, 2));
    }
}

void ManuscriptImageView::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_loupeVisible) {
        const QRect stale = loupeRect(m_loupeCentre);
        m_loupeVisible = false;
        update(stale.adjusted(-2, -2, 2, 2));
    }
}

void ManuscriptImageView::wheelEvent(QWheelEvent *event)
{
    // The wheel belongs to the scroll area unless the loupe is out, in which
    // case it is the obvious way to ask for more of it.
    if (!m_magnifying || !m_loupeVisible) {
        QWidget::wheelEvent(event);
        return;
    }

    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    m_magnification = std::clamp(
        m_magnification * std::pow(1.2, steps), MinMagnification, MaxMagnification);
    update(loupeRect(m_loupeCentre).adjusted(-2, -2, 2, 2));
    event->accept();
}

} // namespace milah
