#include "ui/icons.h"

#include <QPainter>
#include <QPalette>
#include <QPixmap>

namespace milah {
namespace {

/// The sizes a toolbar, a menu or a dock header is likely to ask for. The
/// largest also serves a 24 px button on a 2x display, so hi-dpi stays crisp
/// without a second set of assets.
constexpr int IconSizes[] = {16, 20, 24, 32, 48};

QString iconResource(const QString &name)
{
    return QStringLiteral(":/img/icons/%1.svg").arg(name);
}

} // namespace

QIcon tintedIcon(const QString &resource, const QColor &color)
{
    const QIcon source(resource);
    if (source.isNull()) {
        return QIcon();
    }

    QIcon result;
    for (const int size : IconSizes) {
        QPixmap pixmap = source.pixmap(QSize(size, size));
        if (pixmap.isNull()) {
            continue;
        }

        // SourceIn keeps the artwork's alpha and replaces its colour, which is
        // why the SVGs are drawn as solid black on nothing.
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();

        result.addPixmap(pixmap);
    }

    // Qt derives the disabled and active variants from these itself.
    return result;
}

QIcon appIcon(const QString &name, const QPalette &palette)
{
    return tintedIcon(iconResource(name), palette.color(QPalette::ButtonText));
}

QIcon actionIcon(
    QIcon::ThemeIcon themeIcon,
    const QString &name,
    const QPalette &palette)
{
    return QIcon::fromTheme(themeIcon, appIcon(name, palette));
}

} // namespace milah
