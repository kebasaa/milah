#pragma once

#include <QIcon>
#include <QString>

class QPalette;

namespace milah {

/// Toolbar and menu artwork.
///
/// The bundled icons are single-colour SVGs, recoloured to the palette when they
/// are loaded. They are not handed to Qt as an icon theme: QSvgIconEngine draws
/// an SVG exactly as authored, so a themed set would be stuck at one colour and
/// would disappear into a dark window.

/// Renders a monochrome SVG resource at the sizes a toolbar asks for and
/// recolours each one to `color`, keeping the artwork's own alpha.
QIcon tintedIcon(const QString &resource, const QColor &color);

/// Bundled artwork by stem, for actions with no standard counterpart:
/// `appIcon("load-manuscript", palette())`.
QIcon appIcon(const QString &name, const QPalette &palette);

/// A standard action's icon: the desktop's own where there is an icon theme —
/// which is to say on Linux — and our bundled artwork everywhere else.
QIcon actionIcon(
    QIcon::ThemeIcon themeIcon,
    const QString &name,
    const QPalette &palette);

} // namespace milah
