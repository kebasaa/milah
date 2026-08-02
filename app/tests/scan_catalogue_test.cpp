#include "core/scan_catalogue.h"

#include <QJsonDocument>
#include <QtTest>

using namespace milah;

namespace {

/// Puts the addresses back into a fixture, which is written without them.
///
/// No URL is spelled out in the JSON below, and it is not a matter of taste:
/// moc strips `//` comments line by line even inside a raw string literal, so
/// the two slashes of a scheme swallow the rest of that line — the closing
/// delimiter with it — and everything after parses as something that is not
/// C++. The test class then becomes invisible to moc and the binary fails to
/// link with no vtable, reported nowhere except as "No relevant classes found".
/// A raw string that opens and closes on one line survives it; a multi-line one
/// does not. The sibling catalogue_test.cpp escapes it only by having no URLs.
///
/// So `IMG` and `VIEW` stand in for a host, and are put back here.
QByteArray addresses(QByteArray json)
{
    return json.replace("IMG", "https://img.example")
        .replace("VIEW", "https://cudl.lib.cam.ac.uk/view");
}

ScanCatalogue catalogueFrom(const QByteArray &json)
{
    return ScanCatalogue::fromJson(QJsonDocument::fromJson(addresses(json)).object());
}

/// Shaped exactly like the published manifest, cut to two folios.
QByteArray sampleManifest()
{
    return R"JSON({
     "version": 1,
     "about": "Published manuscript scans Milah can transcribe from.",
     "scans": [
      {
       "id": "MS-OO-00001-00032",
       "title": "Hebrew translation of the New Testament",
       "shelfmark": "MS Oo.1.32",
       "repository": "Cambridge University Library",
       "origin": "Kochi",
       "date": "Eighteenth century",
       "language": "Hebrew",
       "material": "Paper",
       "provenance": "Presented in 1809 by the Revd Claudius Buchanan",
       "attribution": "Provided by Cambridge University Library",
       "licence": "Images made available for download are licensed under CC BY-NC 3.0",
       "book": "",
       "folios": "",
       "source": "VIEW/MS-OO-00001-00032/1",
       "pages": [
        {"n": 1, "label": "front cover", "image": "IMG/one.jp2/full/1024,/0/default.jpg"},
        {"n": 3, "label": "1r", "image": "IMG/three.jp2/full/1024,/0/default.jpg"}
       ]
      },
      {
       "id": "MS-OO-00001-00032-22r-33v",
       "title": "Mark",
       "shelfmark": "MS Oo.1.32",
       "repository": "Cambridge University Library",
       "origin": "Kochi",
       "date": "Eighteenth century",
       "language": "Hebrew",
       "material": "Paper",
       "provenance": "Presented in 1809 by the Revd Claudius Buchanan",
       "attribution": "Provided by Cambridge University Library",
       "licence": "Images made available for download are licensed under CC BY-NC 3.0",
       "book": "MRK",
       "folios": "22r..33v",
       "source": "VIEW/MS-OO-00001-00032/1",
       "pages": [
        {"n": 45, "label": "22r", "image": "IMG/forty-five.jp2/full/1024,/0/default.jpg"}
       ]
      }
     ]
    })JSON";
}

} // namespace

/// The scan catalogue is read exactly as the manuscript catalogue is, and for
/// the same reason: parsing lives in MilahCore and takes a QJsonObject, so it
/// can be tested without a network and without a window.
class ScanCatalogueTest final : public QObject
{
    Q_OBJECT

private slots:
    void aManifestIsReadIntoEntries()
    {
        const ScanCatalogue catalogue = catalogueFrom(sampleManifest());
        // The codex whole, and one of its books: the shape the manifest has had
        // since a binding could be offered book by book.
        QCOMPARE(catalogue.entries().size(), 2);

        const ScanEntry &entry = catalogue.entries().constFirst();
        QCOMPARE(entry.id, QStringLiteral("MS-OO-00001-00032"));
        QCOMPARE(entry.title, QStringLiteral("Hebrew translation of the New Testament"));
        QCOMPARE(entry.shelfmark, QStringLiteral("MS Oo.1.32"));
        QCOMPARE(entry.repository, QStringLiteral("Cambridge University Library"));
        QCOMPARE(entry.origin, QStringLiteral("Kochi"));
        QCOMPARE(entry.date, QStringLiteral("Eighteenth century"));
        QCOMPARE(entry.language, QStringLiteral("Hebrew"));
        QCOMPARE(entry.material, QStringLiteral("Paper"));
        QVERIFY(entry.provenance.startsWith(QStringLiteral("Presented in 1809")));
    }

    void theTermsAreCarried()
    {
        // The images stay on the library's server under the library's licence,
        // which is not Milah's to grant — so losing either of these loses the
        // right to use the scan at all.
        const ScanEntry &entry = catalogueFrom(sampleManifest()).entries().constFirst();
        QCOMPARE(entry.attribution, QStringLiteral("Provided by Cambridge University Library"));
        QVERIFY(entry.licence.contains(QStringLiteral("CC BY-NC 3.0")));
    }

    void theFoliosKeepTheirPlaceAndTheirName()
    {
        const ScanEntry &entry = catalogueFrom(sampleManifest()).entries().constFirst();
        QCOMPARE(entry.pages.size(), 2);
        QCOMPARE(entry.pages.at(0).number, 1);
        QCOMPARE(entry.pages.at(0).label, QStringLiteral("front cover"));
        // Not 2: the manifest counts the folios of the codex, gaps and all, and
        // the second entry is the third leaf.
        QCOMPARE(entry.pages.at(1).number, 3);
        QCOMPARE(entry.pages.at(1).label, QStringLiteral("1r"));
        QVERIFY(entry.pages.at(1).imageUrl.startsWith(QStringLiteral("https://")));
    }

    void theShelfmarkTellsTwoManuscriptsApart()
    {
        const ScanEntry &entry = catalogueFrom(sampleManifest()).entries().constFirst();
        QCOMPARE(
            entry.displayTitle(),
            QStringLiteral("Hebrew translation of the New Testament (MS Oo.1.32)"));
    }

    void aBookOfACodexCarriesTheFoliosItCovers()
    {
        // What tells one entry of a codex from another: they share a shelfmark,
        // a repository and a date, and differ only in which leaves they are.
        const ScanEntry &book = catalogueFrom(sampleManifest()).entries().at(1);
        QCOMPARE(book.title, QStringLiteral("Mark"));
        QCOMPARE(book.book, QStringLiteral("MRK"));
        QCOMPARE(book.folios, QStringLiteral("22r..33v"));
        QCOMPARE(book.shelfmark, QStringLiteral("MS Oo.1.32"));
    }

    void theBooksOfOneCodexAreReadAsOneManuscript()
    {
        // The address is what the picker gathers a manuscript's books by, so two
        // entries of one binding have to agree on it.
        const ScanCatalogue catalogue = catalogueFrom(sampleManifest());
        QCOMPARE(catalogue.entries().at(0).source, catalogue.entries().at(1).source);
        QVERIFY(!catalogue.entries().at(0).source.isEmpty());
    }

    void aWholeCodexNamesNoFolios()
    {
        // Empty is the whole of it, which is what every entry was before a
        // binding could be offered book by book.
        QVERIFY(catalogueFrom(sampleManifest()).entries().constFirst().folios.isEmpty());
    }

    void aManifestWrittenBeforeFoliosExistedStillReads()
    {
        // The field was added after the first manifests were published, and an
        // older one must not lose its scans over a key it never carried.
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"old","title":"A codex","pages":[{"n":1,"image":"IMG/a"}]}]})JSON");
        QCOMPARE(catalogue.entries().size(), 1);
        QVERIFY(catalogue.entries().constFirst().folios.isEmpty());
    }

    void aTitleWithNoShelfmarkStandsAlone()
    {
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"x","title":"A codex","pages":[{"n":1,"image":"IMG/a"}]}]})JSON");
        QCOMPARE(catalogue.entries().constFirst().displayTitle(), QStringLiteral("A codex"));
    }

    void aMalformedEntryIsSkippedNotFatal()
    {
        // One good scan among four, the way a manifest goes wrong in practice:
        // an entry loses its identifier, another its folios, a third is not an
        // object at all. The transcriber must still be offered the one that is
        // fine.
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"title":"No identifier","pages":[{"n":1,"image":"IMG/a"}]},
         {"id":"empty","title":"No folios","pages":[]},
         "not an object",
         {"id":"good","title":"Readable","pages":[{"n":1,"image":"IMG/b"}]}
        ]})JSON");

        QCOMPARE(catalogue.entries().size(), 1);
        QCOMPARE(catalogue.entries().constFirst().id, QStringLiteral("good"));
    }

    void aFolioWithNoImageIsLeftOut()
    {
        // Offering it would put a blank page on screen with nothing to say why.
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"x","title":"A codex","pages":[
          {"n":1,"label":"front cover","image":"IMG/a"},
          {"n":2,"label":"unreleased"},
          {"n":3,"label":"1r","image":"IMG/c"}
         ]}]})JSON");

        const ScanEntry &entry = catalogue.entries().constFirst();
        QCOMPARE(entry.pages.size(), 2);
        QCOMPARE(entry.pages.at(1).label, QStringLiteral("1r"));
    }

    void aScanWhoseFoliosAllFailIsNotOffered()
    {
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"x","title":"A codex","pages":[{"n":1,"label":"unreleased"}]}]})JSON");
        QVERIFY(catalogue.isEmpty());
    }

    void anUntitledScanIsStillNamed()
    {
        // A scan that cannot be named cannot be chosen, so it falls back to
        // what it is kept under, and failing that to its identifier.
        const ScanCatalogue byShelfmark = catalogueFrom(R"JSON({"scans":[
         {"id":"x","shelfmark":"MS Oo.1.32","pages":[{"n":1,"image":"IMG/a"}]}]})JSON");
        QCOMPARE(byShelfmark.entries().constFirst().title, QStringLiteral("MS Oo.1.32"));

        const ScanCatalogue byId = catalogueFrom(R"JSON({"scans":[
         {"id":"MS-OO-1","pages":[{"n":1,"image":"IMG/a"}]}]})JSON");
        QCOMPARE(byId.entries().constFirst().title, QStringLiteral("MS-OO-1"));
    }

    void anAbsentFolioNumberFallsBackToItsPlace()
    {
        // A run of zeroes would put every folio in the same place and leave the
        // arrows walking on the spot.
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"x","title":"A codex","pages":[
          {"image":"IMG/a"},
          {"image":"IMG/b"}
         ]}]})JSON");

        const ScanEntry &entry = catalogue.entries().constFirst();
        QCOMPARE(entry.pages.at(0).number, 1);
        QCOMPARE(entry.pages.at(1).number, 2);
    }

    void anEmptyOrUnreadableManifestIsSilent()
    {
        QVERIFY(catalogueFrom("{}").isEmpty());
        QVERIFY(catalogueFrom("not json at all").isEmpty());
        QVERIFY(catalogueFrom(R"JSON({"scans":[]})JSON").isEmpty());
    }

    void aManuscriptMarkedUnavailableIsKeptWithNoPages()
    {
        // Cambridge MS Oo.1.16 has no viewer to copy a link from — the
        // manifest carries it anyway, with pages: [] and a reason.
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"unavailable-ms-oo-1-16","title":"MS Oo.1.16",
          "shelfmark":"MS Oo.1.16","unavailable":"Not on Cambridge Digital Library.",
          "pages":[]}]})JSON");

        QCOMPARE(catalogue.entries().size(), 1);
        const ScanEntry &entry = catalogue.entries().constFirst();
        QVERIFY(entry.pages.isEmpty());
        QCOMPARE(entry.unavailable, QStringLiteral("Not on Cambridge Digital Library."));
    }

    void aScanWithNoPagesAndNoReasonIsStillDroppedAsMalformed()
    {
        // The guard aMalformedEntryIsSkippedNotFatal() already covers is not
        // to be loosened: an empty "pages" with nothing marking it deliberate
        // is still the sign of a resolver that found nothing, not a
        // manuscript recorded as unavailable on purpose.
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"x","title":"A codex","pages":[]}]})JSON");
        QVERIFY(catalogue.isEmpty());
    }

    void anUnavailableEntryWithNoUnavailableFieldWrittenStillDefaultsEmpty()
    {
        // A manifest written before this field existed must not crash reading
        // it, the same discipline "folios" was held to.
        const ScanCatalogue catalogue = catalogueFrom(R"JSON({"scans":[
         {"id":"x","title":"A codex","pages":[{"n":1,"image":"IMG/a"}]}]})JSON");
        QVERIFY(catalogue.entries().constFirst().unavailable.isEmpty());
    }
};

QTEST_MAIN(ScanCatalogueTest)
#include "scan_catalogue_test.moc"
