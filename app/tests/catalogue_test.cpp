#include "core/manuscript_catalogue.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace milah;

namespace {

ManuscriptCatalogue catalogueFrom(const QByteArray &json)
{
    return ManuscriptCatalogue::fromJson(QJsonDocument::fromJson(json).object());
}

/// A manifest shaped exactly like the published one.
///
/// The JSON is delimited R"JSON( … )JSON" rather than R"( … )": these titles end
/// in "Sloane MS 237)", and the bracket followed by the quote would close a
/// default-delimited raw string in the middle of the manuscript's name.
QByteArray sampleManifest()
{
    return R"JSON({"version":1,"manuscripts":[
      {"file":"REV_Sloane237_hebrew_commented.osis",
       "title":"Revelation (British Library, Sloane MS 237)",
       "book":"REV","role":"manuscript","language":"he",
       "date":"between 1500 and 1699",
       "covers":"This edition covers Revelation 1:1-2:13.",
       "bytes":17203},
      {"file":"REV_Sloane237_translation.osis",
       "title":"English Translation of Revelation (British Library, Sloane MS 237)",
       "book":"REV","role":"translation","language":"en",
       "date":"2017","bytes":12812}
    ]})JSON";
}

} // namespace

class CatalogueTest final : public QObject
{
    Q_OBJECT

private slots:
    void aManifestIsReadIntoEntries()
    {
        const ManuscriptCatalogue catalogue = catalogueFrom(sampleManifest());
        QCOMPARE(catalogue.entries().size(), 2);

        const CatalogueEntry &first = catalogue.entries().first();
        QCOMPARE(first.file, QStringLiteral("REV_Sloane237_hebrew_commented.osis"));
        QCOMPARE(
            first.title, QStringLiteral("Revelation (British Library, Sloane MS 237)"));
        QCOMPARE(first.book, QStringLiteral("REV"));
        QCOMPARE(first.language, QStringLiteral("he"));
        QCOMPARE(first.date, QStringLiteral("between 1500 and 1699"));
        QVERIFY(first.covers.contains(QStringLiteral("1:1-2:13")));
        QCOMPARE(first.bytes, 17203);
    }

    void aTranslationIsToldFromAManuscript()
    {
        // The role is what decides which way Milah loads the file, so it is
        // the one field that cannot be wrong.
        const ManuscriptCatalogue catalogue = catalogueFrom(sampleManifest());
        QVERIFY(!catalogue.entries().at(0).isTranslation());
        QCOMPARE(catalogue.entries().at(0).role, SourceRole::Manuscript);
        QVERIFY(catalogue.entries().at(1).isTranslation());
        QCOMPARE(catalogue.entries().at(1).role, SourceRole::Translation);
    }

    void coverageIsOptional()
    {
        // A third of the published texts carry no x-contents description, so
        // its absence must be ordinary rather than exceptional.
        const ManuscriptCatalogue catalogue = catalogueFrom(sampleManifest());
        QVERIFY(catalogue.entries().at(1).covers.isEmpty());
        QVERIFY(!catalogue.entries().at(1).title.isEmpty());
    }

    void aMalformedEntryIsSkippedNotFatal()
    {
        // One bad row must not cost the editor the whole catalogue, the way a
        // bad phrase rule does not cost them the rest of the table.
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[
              {"title":"No file at all","role":"manuscript"},
              {"file":"strange.osis","role":"incunabulum"},
              {"file":"good.osis","title":"Kept","role":"manuscript"}
            ]})JSON");

        QCOMPARE(catalogue.entries().size(), 1);
        QCOMPARE(catalogue.entries().first().file, QStringLiteral("good.osis"));
    }

    void anOddSizeCostsTheSizeNotTheManuscript()
    {
        // The byte count only ever says how large a download will be. Losing a
        // manuscript over it would be a poor trade.
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[
              {"file":"a.osis","title":"A","role":"manuscript","bytes":"lots"},
              {"file":"b.osis","title":"B","role":"manuscript","bytes":-5}
            ]})JSON");

        QCOMPARE(catalogue.entries().size(), 2);
        QCOMPARE(catalogue.entries().at(0).bytes, 0);
        QCOMPARE(catalogue.entries().at(1).bytes, 0);
    }

    void anEntryWithoutATitleFallsBackToItsFileName()
    {
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[{"file":"MAT_x.osis","role":"manuscript"}]})JSON");
        QCOMPARE(catalogue.entries().first().title, QStringLiteral("MAT_x.osis"));
    }

    void anEmptyOrUnreadableManifestIsSilent()
    {
        QVERIFY(catalogueFrom(QByteArray("{}")).isEmpty());
        QVERIFY(catalogueFrom(QByteArray("not json at all")).isEmpty());
    }

    // --- what is already held ----------------------------------------------

    void installedFilesFindsWhatIsOnDisk()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile held(directory.filePath(QStringLiteral("REV_Sloane237_translation.osis")));
        QVERIFY(held.open(QIODevice::WriteOnly));
        held.close();

        const ManuscriptCatalogue catalogue = catalogueFrom(sampleManifest());
        QCOMPARE(
            catalogue.installedFiles(directory.path()),
            QStringList{QStringLiteral("REV_Sloane237_translation.osis")});
    }

    void installedFilesIgnoresUnrelatedFiles()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile stray(directory.filePath(QStringLiteral("something-else.osis")));
        QVERIFY(stray.open(QIODevice::WriteOnly));
        stray.close();

        QVERIFY(catalogueFrom(sampleManifest()).installedFiles(directory.path()).isEmpty());
        // And a folder that is not there at all is simply empty.
        QVERIFY(catalogueFrom(sampleManifest())
                    .installedFiles(directory.filePath(QStringLiteral("nowhere")))
                    .isEmpty());
    }

    // --- where the library lives -------------------------------------------

    void theWriteDirectoryIsWritable()
    {
        // Downloads must not go beside the executable: an installed Milah sits
        // in a folder its user cannot write to.
        const QString target = manuscriptWriteDirectory();
        QVERIFY(!target.isEmpty());
        QVERIFY(!target.startsWith(QCoreApplication::applicationDirPath()));
        QVERIFY(manuscriptSearchPaths().contains(target));
    }

    void theEnvironmentOverridesTheSearchPath()
    {
        // The whole feature is verified against a local copy through this, so
        // it has to come first and it has to work.
        const QByteArray previous = qgetenv("MILAH_MANUSCRIPT_DIR");
        qputenv("MILAH_MANUSCRIPT_DIR", QByteArray("C:/somewhere/of/my/own"));
        const QStringList paths = manuscriptSearchPaths();
        if (previous.isEmpty()) {
            qunsetenv("MILAH_MANUSCRIPT_DIR");
        } else {
            qputenv("MILAH_MANUSCRIPT_DIR", previous);
        }

        QVERIFY(!paths.isEmpty());
        QCOMPARE(paths.first(), QStringLiteral("C:/somewhere/of/my/own"));
    }
};

QTEST_MAIN(CatalogueTest)
#include "catalogue_test.moc"
