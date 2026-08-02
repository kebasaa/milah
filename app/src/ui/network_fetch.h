#pragma once

#include <QNetworkRequest>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace milah {

/// How long to let a transfer run before giving up. Generous on purpose:
/// cutting it short turns a slow success into a failure, and a folio off a
/// library's image server on a poor connection is genuinely slow.
inline constexpr int TransferTimeoutMs = 120 * 1000;

/// How long to wait before suspecting the route rather than the server. Not a
/// guess: a healthy connection to the published host answers in 0.12–0.75 s.
inline constexpr int RouteProbeMs = 3 * 1000;

/// Whether a request may follow a redirect off the host it was aimed at.
enum class Redirects {
    /// For an address whose host is the whole point — the published manifests,
    /// which live on GitHub and have no business sending anyone elsewhere.
    SameHost,
    /// For a third-party image server. A library's folio addresses belong to
    /// its own image host and are redirected freely; refusing to follow would
    /// simply not fetch the picture.
    AnywhereNoLessSafe,
};

/// Issues a GET with the policy every request in Milah shares.
///
/// One place, because a second caller was the moment to stop copying it: the
/// IPv4 fallback below is measured behaviour, not a precaution, and two
/// divergent copies of it would be worse than none.
QNetworkReply *fetch(
    QNetworkAccessManager *network,
    const QUrl &url,
    Redirects redirects,
    bool preferIPv4 = false);

/// The same request aimed at an IPv4 address, resolved here.
///
/// Some networks advertise an IPv6 route that silently drops packets, which
/// costs about twenty seconds per address before anything gives up. Resolving
/// the host here rather than leaving it to the stack is the point: the stack is
/// what is choosing badly. The lookup itself is fast even when the route is not
/// — measured at 7 ms while connections were taking a minute.
///
/// Returns nullptr on an IPv6-only network, where there is nothing to fall back
/// to and pretending otherwise would replace a slow answer with no answer.
QNetworkReply *fetchOverIPv4(
    QNetworkAccessManager *network,
    const QUrl &url,
    Redirects redirects);

} // namespace milah
