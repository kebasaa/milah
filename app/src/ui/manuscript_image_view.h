#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

namespace milah {

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
    void setImageData(const QByteArray &bytes, const QString &name);
    void clear();

    bool hasImage() const { return !m_image.isNull(); }

    /// Whether the magnifier follows the pointer. Off by default: a loupe under
    /// the cursor at all times would be in the way of reading the page whole.
    bool magnifierEnabled() const { return m_magnifying; }

    QSize sizeHint() const override;
    /// The image is scaled to the width it is given, so its height depends on
    /// that width — which is what a scroll area needs to be told.
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

public slots:
    void setMagnifierEnabled(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;
    /// Claims the height the folio needs at the width it has been given, so the
    /// scroll area around it knows there is more page below.
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    /// Where the folio is drawn: the whole widget width, the image's aspect
    /// ratio, and nothing above or below it.
    QRect pageRect() const;
    /// The circle the loupe occupies at `centre`, in widget coordinates.
    QRect loupeRect(const QPoint &centre) const;

    QImage m_image;
    QString m_name;
    /// Set when the bytes could not be decoded, so the widget can say which
    /// file and why rather than showing nothing.
    QString m_failure;

    bool m_magnifying = false;
    /// Where the loupe is, and whether it is on screen at all. An invalid point
    /// means the pointer has left the folio.
    QPoint m_loupeCentre;
    bool m_loupeVisible = false;
    double m_magnification = 3.0;
};

} // namespace milah
