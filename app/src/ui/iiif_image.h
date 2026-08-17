#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>

class QByteArray;

namespace milah {

/// Fetching a folio at the size the library actually holds, in pieces.
///
/// For training, and for nothing else. A recogniser reading a folio does just
/// as well off the 1024 px picture Milah shows — that was measured, and the
/// master made it no better. But **training** cuts every line out of the picture
/// and shows it to the model over and over, and what it is shown is what it
/// learns. A line stretched from 56 px to the 120 the model wants is a line of
/// invented detail; the same line at the master's resolution is 194 px and gets
/// scaled down, which invents nothing.
///
/// Libraries cap a single request well below their masters — Cambridge holds
/// 3948 × 5295 and answers at most 2000 × 2000, Manchester 5091 × 6500 the same
/// — so the master comes in a grid of region requests and is put back together.
namespace IiifImage {

/// What `info.json` says a service holds.
struct Service
{
    /// The master's size.
    QSize full;
    /// The largest single answer it will give. Equal to `full` when the profile
    /// does not cap it.
    int maxWidth = 0;
    int maxHeight = 0;

    bool isValid() const { return full.isValid() && maxWidth > 0 && maxHeight > 0; }
};

/// Whether this address is an IIIF Image API request Milah can take apart —
/// `…/{identifier}/{region}/{size}/{rotation}/{quality}.{format}`.
///
/// Fails closed. An address that cannot be read apart with confidence is left
/// exactly as it is: OPenn and Alvin serve fixed sizes, a local file is not a
/// service at all, and guessing at any of them produces a request that fetches
/// nothing.
bool looksLikeImageApi(const QUrl &url);

/// The service description for an image request. Empty for anything that is not
/// one.
QUrl infoUrl(const QUrl &url);

/// Reads `info.json`. An unreadable or incomplete answer gives an invalid
/// Service, which every caller treats as "fetch it the way we always did".
Service serviceFromInfo(const QByteArray &json);

/// The grid of pixel regions that covers the master, each small enough to be
/// answered whole.
///
/// One tile where the service will simply give the master, which is the common
/// case for a small image and means no caller needs to special-case it.
QList<QRect> tiles(const Service &service);

/// The address of one region at its own resolution — nothing is scaled, so the
/// pieces fit together exactly.
QUrl tileUrl(const QUrl &url, const QRect &region);

} // namespace IiifImage

} // namespace milah
