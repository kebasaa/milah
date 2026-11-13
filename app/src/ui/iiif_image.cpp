#include "ui/iiif_image.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace milah {
namespace IiifImage {
namespace {

/// The four segments every image request ends with: region, size, rotation, and
/// quality.format. Everything before them is the identifier.
constexpr int TrailingSegments = 4;

QStringList segmentsOf(const QUrl &url)
{
    return url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

/// A rotation is a number, optionally mirrored with a leading `!`. Checking it
/// is what tells an image request from an ordinary path that happens to have
/// four segments left in it.
bool isRotation(const QString &text)
{
    QStringView value(text);
    if (value.startsWith(QLatin1Char('!'))) {
        value = value.mid(1);
    }
    bool ok = false;
    (void)value.toDouble(&ok);
    return ok;
}

/// A number that may have been written as a string.
///
/// Not defensiveness: Cambridge answers `"width":"3948"` and Manchester
/// `"width":5091`, and a reader that took only one of those would work against
/// one of the two libraries Milah actually fetches from and fail silently
/// against the other.
int intFrom(const QJsonValue &value)
{
    if (value.isDouble()) {
        return value.toInt(0);
    }
    bool ok = false;
    const int number = value.toString().toInt(&ok);
    return ok ? number : 0;
}

int positiveOr(const QJsonValue &value, int fallback)
{
    const int number = intFrom(value);
    return number > 0 ? number : fallback;
}

} // namespace

bool looksLikeImageApi(const QUrl &url)
{
    if (!url.isValid() || url.scheme().startsWith(QLatin1String("file"))) {
        return false;
    }
    const QStringList segments = segmentsOf(url);
    // The identifier needs at least one segment of its own, so four trailing
    // ones is not enough on its own.
    if (segments.size() <= TrailingSegments) {
        return false;
    }
    const QString quality = segments.last();
    if (!quality.contains(QLatin1Char('.'))) {
        return false;
    }
    return isRotation(segments.at(segments.size() - 2));
}

QUrl infoUrl(const QUrl &url)
{
    if (!looksLikeImageApi(url)) {
        return QUrl();
    }
    QStringList segments = segmentsOf(url);
    segments.remove(segments.size() - TrailingSegments, TrailingSegments);
    segments.append(QStringLiteral("info.json"));

    QUrl info = url;
    info.setPath(QLatin1Char('/') + segments.join(QLatin1Char('/')));
    info.setQuery(QString());
    info.setFragment(QString());
    return info;
}

Service serviceFromInfo(const QByteArray &json)
{
    const QJsonObject document = QJsonDocument::fromJson(json).object();
    const int width = intFrom(document.value(QStringLiteral("width")));
    const int height = intFrom(document.value(QStringLiteral("height")));
    if (width <= 0 || height <= 0) {
        return Service();
    }

    Service service;
    service.full = QSize(width, height);
    // Uncapped until the profile says otherwise, which is what a service that
    // will hand over the whole master means.
    service.maxWidth = width;
    service.maxHeight = height;

    // Version 2 puts the caps in one of the objects of the profile array, after
    // the compliance URI; version 3 puts them at the top. Both are read, because
    // both are in the wild and a missed cap means every tile comes back short.
    const auto readCaps = [&service](const QJsonObject &from) {
        service.maxWidth = positiveOr(from.value(QStringLiteral("maxWidth")), service.maxWidth);
        service.maxHeight =
            positiveOr(from.value(QStringLiteral("maxHeight")), service.maxHeight);
        // maxArea would bound a tile too, but no service Milah reads states it
        // without also stating the two above, so it is not guessed at here.
    };
    readCaps(document);
    const QJsonValue profile = document.value(QStringLiteral("profile"));
    if (profile.isArray()) {
        for (const QJsonValue &part : profile.toArray()) {
            if (part.isObject()) {
                readCaps(part.toObject());
            }
        }
    } else if (profile.isObject()) {
        readCaps(profile.toObject());
    }

    service.maxWidth = qMin(service.maxWidth, width);
    service.maxHeight = qMin(service.maxHeight, height);
    return service;
}

QList<QRect> tiles(const Service &service)
{
    QList<QRect> grid;
    if (!service.isValid()) {
        return grid;
    }

    const int width = service.full.width();
    const int height = service.full.height();
    const int across = (width + service.maxWidth - 1) / service.maxWidth;
    const int down = (height + service.maxHeight - 1) / service.maxHeight;

    // Divided evenly rather than into full tiles and a remainder, so the last
    // column is not a sliver — a one-pixel-wide request is a request some
    // services refuse.
    const int step = (width + across - 1) / across;
    const int drop = (height + down - 1) / down;

    grid.reserve(across * down);
    for (int row = 0; row < down; ++row) {
        for (int column = 0; column < across; ++column) {
            const int x = column * step;
            const int y = row * drop;
            grid.append(QRect(x, y, qMin(step, width - x), qMin(drop, height - y)));
        }
    }
    return grid;
}

QUrl tileUrl(const QUrl &url, const QRect &region)
{
    if (!looksLikeImageApi(url)) {
        return QUrl();
    }
    QStringList segments = segmentsOf(url);
    const int first = segments.size() - TrailingSegments;
    segments[first] = QStringLiteral("%1,%2,%3,%4")
                          .arg(region.x())
                          .arg(region.y())
                          .arg(region.width())
                          .arg(region.height());
    // `full` rather than a width: the region comes back at its own resolution,
    // so the pieces fit together without anything being scaled anywhere.
    segments[first + 1] = QStringLiteral("full");

    QUrl tile = url;
    tile.setPath(QLatin1Char('/') + segments.join(QLatin1Char('/')));
    return tile;
}

} // namespace IiifImage
} // namespace milah
