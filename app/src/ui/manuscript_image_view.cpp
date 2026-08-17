#include "ui/manuscript_image_view.h"

#include "core/line_fill.h"

#include <QBuffer>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QImageReader>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
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
    update();
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
        drawWordOverlay(painter, page, event->rect());
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
