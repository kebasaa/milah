#include "ui/manuscript_image_view.h"

#include <QBuffer>
#include <QImageReader>
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

void ManuscriptImageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The folio is drawn at the full width it is given, so how tall it is
    // depends on how wide that is. Claiming it as a minimum is what puts a
    // scrollbar beside a page taller than the pane. This settles rather than
    // looping: the claim changes the height, and the height is not what the
    // claim is computed from.
    setMinimumHeight(m_image.isNull() ? 0 : heightForWidth(width()));
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
