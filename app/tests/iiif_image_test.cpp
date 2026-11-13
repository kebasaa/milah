#include "ui/iiif_image.h"

#include <QtTest>

using namespace milah;

namespace {

/// The two libraries Milah actually fetches from, as their own info.json
/// describes them. Both hold masters far larger than they will answer in one
/// request, which is the whole reason tiling exists.
///
/// The compliance URIs are written without their `//`, and that is not tidiness:
/// moc treats `//` inside a multi-line raw string as a comment and swallows the
/// rest of the file, so a realistic `http://iiif.io/…` here leaves it unable to
/// find the test class at all. The parser never reads them.
constexpr char kLevel1[] = R"("http:@@iiif.io/api/image/2/level1.json")";

QByteArray withUris(QByteArray json)
{
    return json.replace("LEVEL1", kLevel1).replace("@@", "//");
}

QByteArray cambridge()
{
    return withUris(QByteArray(R"({"width":"3948","height":"5295",
      "sizes":[{"width":987,"height":1323}],
      "profile":[LEVEL1,
                 {"formats":["jpg"],"maxWidth":2000,"maxHeight":2000}]})"));
}

QByteArray manchester()
{
    return withUris(QByteArray(R"({"width":5091,"height":6500,
      "profile":[LEVEL1, {"maxWidth":2000,"maxHeight":2000}]})"));
}

} // namespace

class IiifImageTest final : public QObject
{
    Q_OBJECT

private slots:
    /// An address Milah cannot take apart with confidence is left alone.
    ///
    /// Fails closed on purpose. OPenn and Alvin serve fixed sizes and a local
    /// file is not a service at all; rewriting any of them produces a request
    /// that fetches nothing, which looks exactly like a slow network.
    void onlyAnImageRequestIsRecognised()
    {
        QVERIFY(IiifImage::looksLikeImageApi(QUrl(
            "https://images.lib.cam.ac.uk/iiif/MS-OO-1.jp2/full/1024,/0/default.jpg")));
        // A rotation of 90, mirrored, and a different quality: still one.
        QVERIFY(IiifImage::looksLikeImageApi(
            QUrl("https://x/iiif/id/0,0,10,10/full/!90/gray.png")));

        // Not one: no format, no rotation, too few segments, a local file.
        QVERIFY(!IiifImage::looksLikeImageApi(QUrl("https://x/iiif/id/full/max/0/default")));
        QVERIFY(!IiifImage::looksLikeImageApi(QUrl("https://x/iiif/id/full/max/x/d.jpg")));
        QVERIFY(!IiifImage::looksLikeImageApi(QUrl("https://openn.library/0001/data.jpg")));
        QVERIFY(!IiifImage::looksLikeImageApi(QUrl("file:///C:/scans/folio.jpg")));
        QVERIFY(!IiifImage::looksLikeImageApi(QUrl()));
    }

    void theServiceIsAskedBesideTheImage()
    {
        QCOMPARE(
            IiifImage::infoUrl(QUrl(
                "https://images.lib.cam.ac.uk/iiif/MS-OO-1.jp2/full/1024,/0/default.jpg")),
            QUrl("https://images.lib.cam.ac.uk/iiif/MS-OO-1.jp2/info.json"));
        // And nothing is asked of an address that is not a service.
        QVERIFY(IiifImage::infoUrl(QUrl("https://openn.library/0001/data.jpg")).isEmpty());
    }

    /// Version 2 hides the caps in an object inside the profile array, after the
    /// compliance URI. Missing them means every tile comes back short and the
    /// stitched folio has holes in it.
    void theCapsAreReadOutOfTheProfile()
    {
        const IiifImage::Service service = IiifImage::serviceFromInfo(cambridge());
        QVERIFY(service.isValid());
        QCOMPARE(service.full, QSize(3948, 5295));
        QCOMPARE(service.maxWidth, 2000);
        QCOMPARE(service.maxHeight, 2000);
    }

    /// A service that states no cap will hand over the whole master, so one
    /// tile is the answer and no caller has to special-case it.
    void anUncappedServiceIsOneTile()
    {
        const IiifImage::Service service = IiifImage::serviceFromInfo(
            QByteArray(R"({"width":800,"height":600,"profile":"level2"})"));
        QVERIFY(service.isValid());
        const QList<QRect> grid = IiifImage::tiles(service);
        QCOMPARE(grid.size(), 1);
        QCOMPARE(grid.first(), QRect(0, 0, 800, 600));
    }

    /// Nothing readable means nothing is attempted — the caller falls back to
    /// the single request Milah has always made.
    void anUnreadableServiceIsRefused()
    {
        QVERIFY(!IiifImage::serviceFromInfo(QByteArray("not json")).isValid());
        QVERIFY(!IiifImage::serviceFromInfo(QByteArray(R"({"height":10})")).isValid());
        QVERIFY(IiifImage::tiles(IiifImage::Service()).isEmpty());
    }

    /// The grid covers the master exactly, with no tile over the cap and none
    /// of them a sliver.
    void theGridCoversTheMaster()
    {
        for (const QByteArray &json : {cambridge(), manchester()}) {
            const IiifImage::Service service = IiifImage::serviceFromInfo(json);
            const QList<QRect> grid = IiifImage::tiles(service);
            QVERIFY(!grid.isEmpty());

            QRegion covered;
            for (const QRect &tile : grid) {
                QVERIFY2(
                    tile.width() <= service.maxWidth && tile.height() <= service.maxHeight,
                    "a tile is larger than the service will answer");
                // A sliver is a request some services refuse outright, so the
                // master is divided evenly rather than into full tiles and a
                // remainder.
                QVERIFY2(tile.width() > 1 && tile.height() > 1, "a tile is a sliver");
                covered += tile;
            }
            QCOMPARE(covered.boundingRect(), QRect(QPoint(0, 0), service.full));
            QCOMPARE(
                covered,
                QRegion(QRect(QPoint(0, 0), service.full)));
        }

        // Cambridge comes to 2 across and 3 down, Manchester to 3 and 4.
        QCOMPARE(IiifImage::tiles(IiifImage::serviceFromInfo(cambridge())).size(), 6);
        QCOMPARE(IiifImage::tiles(IiifImage::serviceFromInfo(manchester())).size(), 12);
    }

    /// A tile is asked for at its own resolution, so the pieces fit together
    /// without anything being scaled.
    void aTileIsAskedForWhole()
    {
        QCOMPARE(
            IiifImage::tileUrl(
                QUrl("https://images.lib.cam.ac.uk/iiif/MS-OO-1.jp2/full/1024,/0/default.jpg"),
                QRect(1974, 1765, 1974, 1765)),
            QUrl("https://images.lib.cam.ac.uk/iiif/MS-OO-1.jp2/"
                 "1974,1765,1974,1765/full/0/default.jpg"));
    }
};

QTEST_MAIN(IiifImageTest)
#include "iiif_image_test.moc"
