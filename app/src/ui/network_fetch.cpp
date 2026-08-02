#include "ui/network_fetch.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace milah {
namespace {

QString hostOf(const QUrl &url)
{
    return url.host();
}

QNetworkRequest::RedirectPolicy policyFor(Redirects redirects)
{
    return redirects == Redirects::SameHost
        ? QNetworkRequest::SameOriginRedirectPolicy
        : QNetworkRequest::NoLessSafeRedirectPolicy;
}

void applyPolicy(QNetworkRequest &request, Redirects redirects)
{
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, policyFor(redirects));
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(TransferTimeoutMs);
}

} // namespace

QNetworkReply *fetchOverIPv4(
    QNetworkAccessManager *network,
    const QUrl &url,
    Redirects redirects)
{
    const QHostInfo resolved = QHostInfo::fromName(hostOf(url));
    QHostAddress chosen;
    for (const QHostAddress &address : resolved.addresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            chosen = address;
            break;
        }
    }
    if (chosen.isNull()) {
        return nullptr;
    }

    QUrl direct = url;
    direct.setHost(chosen.toString());

    QNetworkRequest request(direct);
    // The certificate names the host, not the number. Verifying against the
    // name keeps the connection as safe as the one it replaces; without this
    // the fallback would be an open door.
    request.setPeerVerifyName(hostOf(url));
    // And the CDN routes on the Host header, which would otherwise say the IP.
    request.setRawHeader("Host", hostOf(url).toUtf8());
    // Which only works over HTTP/1.1. HTTP/2 has no Host header: it carries the
    // site in the :authority pseudo-header, built from the URL, and a raw Host
    // is ignored outright. The CDN then sees a bare address, cannot tell which
    // of the sites it fronts is wanted, and answers 404 — measured, against the
    // published host, before this line existed. Costs nothing worth counting:
    // only the degraded path comes through here.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    applyPolicy(request, redirects);

    return network->get(request);
}

QNetworkReply *fetch(
    QNetworkAccessManager *network,
    const QUrl &url,
    Redirects redirects,
    bool preferIPv4)
{
    if (preferIPv4) {
        // Already learned once this session that the ordinary route does not
        // work here; no reason to spend three seconds learning it again.
        if (QNetworkReply *direct = fetchOverIPv4(network, url, redirects)) {
            return direct;
        }
    }

    QNetworkRequest request(url);
    applyPolicy(request, redirects);
    return network->get(request);
}

} // namespace milah
