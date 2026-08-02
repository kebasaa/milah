#include "core/manuscript_catalogue.h"

#include <QCryptographicHash>
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

/// Writes `text` verbatim and returns its path.
QString writeFile(const QTemporaryDir &directory, const QString &name, const QByteArray &text)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text);
    }
    return path;
}

/// The manifest's own recipe: sha256 over the LF form.
QByteArray checksumOf(const QByteArray &text)
{
    QByteArray normalised = text;
    normalised.replace("\r\n", "\n");
    return QCryptographicHash::hash(normalised, QCryptographicHash::Sha256).toHex();
}

QByteArray manifestWithHash(const QByteArray &file, const QByteArray &hash, qint64 bytes)
{
    return QByteArray(R"JSON({"manuscripts":[{"file":")JSON") + file
        + R"JSON(","title":"A text","role":"manuscript","bytes":)JSON"
        + QByteArray::number(bytes) + R"JSON(,"sha256":")JSON" + hash + R"JSON("}]})JSON";
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

    // --- telling a stale copy from a current one ---------------------------

    void aHashIsReadFromTheManifest()
    {
        const ManuscriptCatalogue catalogue =
            catalogueFrom(manifestWithHash("a.osis", "ABCDEF0123", 10));
        // Lowercased on the way in, so a manifest written with upper-case hex
        // still compares equal to what QCryptographicHash produces.
        QCOMPARE(catalogue.entries().first().sha256, QStringLiteral("abcdef0123"));
    }

    void anEntryWithoutAHashIsStillOffered()
    {
        // A manifest written before checksums existed must not lose its
        // manuscripts; they simply cannot report an update.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory, QStringLiteral("a.osis"), "anything");

        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[{"file":"a.osis","title":"A","role":"manuscript"}]})JSON");
        QCOMPARE(catalogue.entries().size(), 1);
        QVERIFY(catalogue.entries().first().sha256.isEmpty());
        QCOMPARE(
            catalogue.installedFiles(directory.path()),
            QStringList{QStringLiteral("a.osis")});
        QVERIFY(catalogue.updatableFiles(directory.path()).isEmpty());
    }

    void aFileMatchingItsHashIsNotUpdatable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray text = "the published text\n";
        writeFile(directory, QStringLiteral("a.osis"), text);

        const ManuscriptCatalogue catalogue =
            catalogueFrom(manifestWithHash("a.osis", checksumOf(text), text.size()));
        QVERIFY(catalogue.updatableFiles(directory.path()).isEmpty());
    }

    void aFileDifferingFromItsHashIs()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory, QStringLiteral("a.osis"), "the copy I downloaded\n");

        const ManuscriptCatalogue catalogue = catalogueFrom(
            manifestWithHash("a.osis", checksumOf("the corrected text\n"), 19));
        QCOMPARE(
            catalogue.updatableFiles(directory.path()),
            QStringList{QStringLiteral("a.osis")});
    }

    void aFileNotHeldIsNotUpdatable()
    {
        // "You do not have this" and "yours is out of date" are different
        // answers, and the download window shows them differently.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const ManuscriptCatalogue catalogue =
            catalogueFrom(manifestWithHash("a.osis", checksumOf("x"), 1));
        QVERIFY(catalogue.installedFiles(directory.path()).isEmpty());
        QVERIFY(catalogue.updatableFiles(directory.path()).isEmpty());
    }

    void aSameSizeChangeIsStillDetected()
    {
        // The reason this uses a checksum rather than the byte count. Fixing
        // "Sloane MS 237" to "MS Sloane 273" moves not one byte of length, and
        // that correction is outstanding in the published texts. Size-based
        // detection passes every other test here and fails this one.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray held = "British Library, Sloane MS 237\n";
        const QByteArray published = "British Library, Sloane MS 273\n";
        QCOMPARE(held.size(), published.size());

        writeFile(directory, QStringLiteral("a.osis"), held);
        const ManuscriptCatalogue catalogue = catalogueFrom(
            manifestWithHash("a.osis", checksumOf(published), published.size()));

        QCOMPARE(
            catalogue.updatableFiles(directory.path()),
            QStringList{QStringLiteral("a.osis")});
    }

    void hashingIsBlindToLineEndings()
    {
        // A clone made with core.autocrlf=true holds CRLF where GitHub serves
        // LF. Without normalising, every manuscript would read "update
        // available" for ever and taking the update would not settle it.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray lf = "one\ntwo\nthree\n";
        const QByteArray crlf = "one\r\ntwo\r\nthree\r\n";

        const QString withCrlf = writeFile(directory, QStringLiteral("crlf.osis"), crlf);
        const QString withLf = writeFile(directory, QStringLiteral("lf.osis"), lf);
        QCOMPARE(manuscriptChecksum(withCrlf), manuscriptChecksum(withLf));
        QCOMPARE(manuscriptChecksum(withLf), QString::fromLatin1(checksumOf(lf)));
    }

    void anUnreadableFileIsNotCalledStale()
    {
        // A missing file has no checksum, and re-downloading is not an answer
        // to a question that was never asked.
        QVERIFY(manuscriptChecksum(QStringLiteral("nowhere/at/all.osis")).isEmpty());
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

    void aWitnessKeepsItsTitleExactly()
    {
        CatalogueEntry entry;
        entry.role = SourceRole::Manuscript;
        entry.title = QStringLiteral("Revelation (British Library, Sloane MS 237)");
        QCOMPARE(entry.displayTitle(), entry.title);
    }

    void aTranslationIsNamedForTheWitnessItRenders()
    {
        // The point of the whole thing: the pair reads as a pair, sorted
        // together, differing only in the mark at the end.
        CatalogueEntry witness;
        witness.role = SourceRole::Manuscript;
        witness.title = QStringLiteral("Luke (Vatican, Vat. ebr. 530)");

        CatalogueEntry rendered;
        rendered.role = SourceRole::Translation;
        rendered.title =
            QStringLiteral("English Translation of Luke (Vatican, Vat. ebr. 530)");

        QVERIFY(rendered.displayTitle().startsWith(witness.displayTitle()));
        QVERIFY(rendered.displayTitle().endsWith(QStringLiteral("(English translation)")));
    }

    void bothWordingsOfTheLeadAreTaken()
    {
        // The transcribers write it two ways across the published texts, and a
        // fix that only knew one would leave half the rows saying it twice.
        CatalogueEntry shorter;
        shorter.role = SourceRole::Translation;
        shorter.title = QStringLiteral("Translation of James (Cochin MS Oo.1.32)");
        QCOMPARE(shorter.displayTitle(),
                 QStringLiteral("James (Cochin MS Oo.1.32)  (English translation)"));

        CatalogueEntry longer;
        longer.role = SourceRole::Translation;
        longer.title = QStringLiteral("English Translation of James (Cochin MS Oo.1.32)");
        QCOMPARE(longer.displayTitle(), shorter.displayTitle());
    }

    void theLongerLeadIsNotLeftAsAStrayEnglish()
    {
        // Matching "Translation of " first would take it out of the middle and
        // leave "English " behind. Order of the table is load-bearing.
        CatalogueEntry entry;
        entry.role = SourceRole::Translation;
        entry.title = QStringLiteral("English Translation of Matthew");
        QVERIFY(!entry.displayTitle().startsWith(QStringLiteral("English ")));
    }

    void aTitleWithNoLeadIsStillMarkedButNotMangled()
    {
        // A third wording is only a matter of time, and half a title is worse
        // than a title that merely repeats itself.
        CatalogueEntry entry;
        entry.role = SourceRole::Translation;
        entry.title = QStringLiteral("Revelation rendered into English");
        QCOMPARE(entry.displayTitle(),
                 QStringLiteral("Revelation rendered into English  (English translation)"));
    }

    void aTitleThatIsNothingButTheLeadIsLeftAlone()
    {
        CatalogueEntry entry;
        entry.role = SourceRole::Translation;
        entry.title = QStringLiteral("Translation of ");
        // Stripping would leave an empty row, which says less than the doubled
        // wording this replaces.
        QVERIFY(entry.displayTitle().contains(QStringLiteral("Translation of")));
    }

    void everyPublishedTranslationLosesItsDoubling()
    {
        // Against the wording actually published rather than invented examples:
        // if a transcriber writes a third form, this is what notices.
        const QStringList published = {
            QStringLiteral("Translation of James (Cochin MS Oo.1.32)"),
            QStringLiteral("English Translation of John (Vatican, Vat. ebr. 530)"),
            QStringLiteral("English Translation of Luke (Vatican, Vat. ebr. 530)"),
            QStringLiteral("Translation of Matthew (Cochin MS Oo.1.32)"),
            QStringLiteral("Translation of Revelation (Cochin MS Oo.1.16.2)"),
            QStringLiteral(
                "English Translation of Revelation (British Library, Sloane MS 237)"),
        };
        for (const QString &title : published) {
            CatalogueEntry entry;
            entry.role = SourceRole::Translation;
            entry.title = title;
            const QString shown = entry.displayTitle();
            QVERIFY2(!shown.contains(QStringLiteral("Translation of"), Qt::CaseInsensitive),
                     qPrintable(shown));
            QVERIFY2(shown.endsWith(QStringLiteral("  (English translation)")),
                     qPrintable(shown));
        }
    }
};

QTEST_MAIN(CatalogueTest)
#include "catalogue_test.moc"
