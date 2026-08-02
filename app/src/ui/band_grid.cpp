#include "ui/band_grid.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace milah {

QList<Band> packBands(const QList<int> &widths, int readingRoom)
{
    QList<Band> bands;
    int start = 0;
    int used = 0;
    for (int index = 0; index < widths.size(); ++index) {
        const int required = widths.at(index) + (index > start ? ColumnSpacing : 0);
        // `index > start` is what guarantees a band is never empty: the first
        // column of a band is taken whatever it costs, and only the ones after
        // it can be pushed to the next band.
        if (index > start && used + required > readingRoom) {
            bands.append(Band{start, index});
            start = index;
            used = widths.at(index);
        } else {
            used += required;
        }
    }
    bands.append(Band{start, int(widths.size())});
    return bands;
}

QFont scaledFont(const QWidget *widget, double factor)
{
    QFont font = widget->font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * factor);
    } else {
        font.setPixelSize(int(std::lround(font.pixelSize() * factor)));
    }
    return font;
}

int htmlWidth(const QString &html, const QFont &font)
{
    if (html.isEmpty()) {
        return 0;
    }
    QTextDocument document;
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    document.setHtml(html);
    return int(std::ceil(document.idealWidth())) + MeasurementSlack;
}

QString unsettledColor(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128
        ? QStringLiteral("#8fa3bf")
        : QStringLiteral("#5a6c8c");
}

QColor noteMarkerColor(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128
        ? QColor(QStringLiteral("#ff6b6b"))
        : QColor(QStringLiteral("#c02626"));
}

QString acronymColor(const QPalette &palette)
{
    const QColor text = palette.color(QPalette::Text);
    return QStringLiteral("rgba(%1, %2, %3, 0.72)")
        .arg(text.red())
        .arg(text.green())
        .arg(text.blue());
}

BandedGridWidget::BandedGridWidget(QWidget *parent)
    : QWidget(parent)
{
    m_readingFont = scaledFont(this, 1.25);
    m_acronymFont = scaledFont(this, 0.85);

    m_bandHost = new QWidget;
    // The bands are packed to fit the viewport, so they must never be the
    // thing that decides how wide the viewport is: an ignored width keeps the
    // readings from pushing the card — and with it the scroll area, and with
    // it the next packing pass — steadily wider.
    m_bandHost->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_bandLayout = new QVBoxLayout(m_bandHost);
    m_bandLayout->setContentsMargins(0, 0, 0, 0);
    m_bandLayout->setSpacing(6);
    m_bandTarget = m_bandLayout;
}

void BandedGridWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (m_rebuildQueued || std::abs(availableWidth() - m_builtForAvailable) <= ReflowThreshold) {
        return;
    }
    // Collapse a run of resize events into one reflow, and never reflow from
    // inside the resize itself.
    m_rebuildQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildQueued = false;
        if (std::abs(availableWidth() - m_builtForAvailable) > ReflowThreshold) {
            build();
        }
    });
}

QVBoxLayout *BandedGridWidget::addBandGroup()
{
    auto *card = new QWidget;
    // The same card the comparison draws round a verse, and the same rule in
    // the window's stylesheet draws it — a folio's verses and a comparison's
    // ought to look like the same kind of thing, because they are.
    card->setObjectName(QStringLiteral("verseCard"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(CardPadding, 10, CardPadding, CardPadding);
    layout->setSpacing(8);

    m_bandLayout->addWidget(card);
    m_bandTarget = layout;
    return layout;
}

void BandedGridWidget::clearBands()
{
    // Bands go back into the stack itself until another card is opened.
    m_bandTarget = m_bandLayout;

    while (QLayoutItem *item = m_bandLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            // A control inside a band can trigger the rebuild that deletes it,
            // so the widget is only hidden here and freed once the stack
            // unwinds.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

int BandedGridWidget::availableWidth() const
{
    // Not the widget's own width: it is inside a resizable scroll area and
    // grows to whatever its content demands, so measuring itself would always
    // report enough room and nothing would ever wrap. The viewport is the real
    // limit; this sits inset from it by the enclosing list's margins.
    int limit = width();
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *area = qobject_cast<QScrollArea *>(ancestor)) {
            const int inset = mapTo(area->viewport(), QPoint(0, 0)).x();
            limit = area->viewport()->width() - 2 * std::max(0, inset);
            break;
        }
    }

    const QMargins margins = layout() ? layout()->contentsMargins() : QMargins();
    const int available = limit - margins.left() - margins.right();
    return available > 2 * ColumnSpacing ? available : UnlaidOutWidth;
}

QGridLayout *BandedGridWidget::addBand(bool separator)
{
    if (separator) {
        auto *rule = new QFrame;
        rule->setObjectName(QStringLiteral("bandRule"));
        rule->setFrameShape(QFrame::HLine);
        rule->setFrameShadow(QFrame::Plain);
        m_bandTarget->addWidget(rule);
    }

    auto *host = new QWidget;
    host->setLayoutDirection(m_bandDirection);

    auto *grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(ColumnSpacing);
    grid->setVerticalSpacing(2);
    m_bandTarget->addWidget(host);
    return grid;
}

void BandedGridWidget::addRowAcronym(
    QGridLayout *grid,
    int row,
    const QString &text,
    const QString &tooltip,
    const std::function<void(const QPoint &)> &menu)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("rowAcronym"));

    if (menu) {
        label->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            label,
            &QLabel::customContextMenuRequested,
            this,
            [label, menu](const QPoint &position) { menu(label->mapToGlobal(position)); });
    }
    // Latin in a right-to-left band: the label reads left to right within its
    // own cell, which sits at the right-hand edge of the row.
    label->setLayoutDirection(Qt::LeftToRight);
    label->setFont(m_acronymFont);
    label->setStyleSheet(QStringLiteral("color: %1; padding-left: %2px;")
                             .arg(acronymColor(palette()))
                             .arg(AcronymPadding));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (!tooltip.isEmpty()) {
        label->setToolTip(tooltip);
    }
    grid->addWidget(label, row, AcronymColumn);
}

} // namespace milah
