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
       "shelfmark":"British Library, Sloane MS 237",
       "folios":"1r-4v",
       "translatedFrom":"Translated from the Greek",
       "exemplar":"Copied from Cambridge MS Oo.1.32",
       "rights":"© 2017 by Nehemia Gordon",
       "license":"CC BY-NC-SA 4.0",
       "bytes":17203},
      {"file":"REV_Sloane237_translation.osis",
       "title":"English Translation of Revelation (British Library, Sloane MS 237)",
       "book":"REV","role":"translation","language":"en",
       "shelfmark":"British Library, Sloane MS 237",
       "date":"2017","bytes":12812}
    ]})JSON";
}

/// A one-entry manifest whose rights statement is exactly `statement`.
///
/// At namespace scope like every other raw string here, and not inside a test:
/// moc parses this class, and a raw string in a member body is enough to lose
/// it the vtable it needs.
QByteArray manifestWithRights(const QByteArray &statement)
{
    return QByteArray(R"JSON({"manuscripts":[{"file":"a.osis","title":"A",)JSON"
                      R"JSON("role":"manuscript","rights":")JSON")
        + statement + R"JSON("}]})JSON";
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

    void theCataloguingFieldsAreRead()
    {
        // The four columns the download window shows beside a title. Without
        // them the window is a list of names, which is what it was.
        const CatalogueEntry &witness = catalogueFrom(sampleManifest()).entries().first();
        QCOMPARE(witness.shelfmark, QStringLiteral("British Library, Sloane MS 237"));
        QCOMPARE(witness.folios, QStringLiteral("1r-4v"));
        QCOMPARE(witness.translatedFrom, QStringLiteral("Translated from the Greek"));
        QCOMPARE(witness.exemplar, QStringLiteral("Copied from Cambridge MS Oo.1.32"));
    }

    void theTranslationColumnSaysWhetherItIsSettled()
    {
        // The whole feature. A column that printed "Greek" flat would state as
        // a fact what, for several of these manuscripts, is the argument.
        CatalogueEntry entry;
        entry.translatedFrom = QStringLiteral("Translated from the Greek");

        entry.translationCertainty = QLatin1String(TranslationCertainty::Certain);
        QCOMPARE(translationColumn(entry), QStringLiteral("Greek"));

        entry.translationCertainty = QLatin1String(TranslationCertainty::Uncertain);
        QCOMPARE(translationColumn(entry), QStringLiteral("Greek?"));

        entry.translationCertainty = QLatin1String(TranslationCertainty::Original);
        QCOMPARE(translationColumn(entry), QStringLiteral("Original"));

        entry.translationCertainty =
            QLatin1String(TranslationCertainty::OriginalUncertain);
        QCOMPARE(translationColumn(entry), QStringLiteral("Original?"));
    }

    void anUnrecordedAnswerClaimsNothing()
    {
        // Not "original", and not "Greek". Nobody has said, and a blank cell
        // used to mean that and "an original Hebrew text" at the same time.
        CatalogueEntry entry;
        QCOMPARE(translationColumn(entry), QStringLiteral("—"));

        // Even where the prose is there: without a verdict it is not an answer
        // the catalogue is prepared to stand behind.
        entry.translatedFrom = QStringLiteral("Translated from the Greek");
        QCOMPARE(translationColumn(entry), QStringLiteral("—"));
    }

    void theColumnKeepsTheAnswerRatherThanTheQuestion()
    {
        // "Translated from the" is the same on every one of them; the language
        // is what a reader is looking down the column for.
        CatalogueEntry entry;
        entry.translationCertainty = QLatin1String(TranslationCertainty::Certain);

        entry.translatedFrom = QStringLiteral("Translated from the Latin Vulgate.");
        QCOMPARE(translationColumn(entry), QStringLiteral("Latin Vulgate"));

        entry.translatedFrom = QStringLiteral("A translation of the Peshitta");
        QCOMPARE(translationColumn(entry), QStringLiteral("Peshitta"));

        // A verdict with nothing named still says that it is one.
        entry.translatedFrom.clear();
        QCOMPARE(translationColumn(entry), QStringLiteral("Yes"));
    }

    void aSubTypeCrossesToACertaintyAndBack()
    {
        QCOMPARE(
            translationSubType(QLatin1String(TranslationCertainty::Certain)),
            QStringLiteral("x-certain"));
        QCOMPARE(
            translationCertaintyOf(QStringLiteral("x-original-uncertain")),
            QLatin1String(TranslationCertainty::OriginalUncertain));
        // Nothing recorded stays nothing, in both directions.
        QVERIFY(translationSubType(QString()).isEmpty());
        QVERIFY(translationCertaintyOf(QString()).isEmpty());
        // The x- prefix is the schema's rule for a value it does not define, so
        // an attribute without it is not this vocabulary.
        QVERIFY(translationCertaintyOf(QStringLiteral("certain")).isEmpty());
        // And a verdict from a later version reads as one nobody recorded,
        // which is what it means to this one.
        QVERIFY(translationCertaintyOf(QStringLiteral("x-disputed")).isEmpty());
    }

    void theCertaintyIsReadFromTheManifest()
    {
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[
              {"file":"a.osis","title":"A","role":"manuscript",
               "translatedFrom":"Translated from the Greek",
               "translationCertainty":"Uncertain"}]})JSON");
        // Lowercased on the way in, so a manifest written either way compares
        // equal to the vocabulary.
        QCOMPARE(
            catalogue.entries().first().translationCertainty,
            QLatin1String(TranslationCertainty::Uncertain));
        QCOMPARE(
            translationColumn(catalogue.entries().first()), QStringLiteral("Greek?"));
    }

    void theCataloguingFieldsAreAllOptional()
    {
        // Most manuscripts answer none of these, and a manifest written before
        // they existed answers none of them at all. Absent has to be ordinary.
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[
              {"file":"a.osis","title":"A","role":"manuscript"}]})JSON");
        const CatalogueEntry &entry = catalogue.entries().first();
        QVERIFY(entry.shelfmark.isEmpty());
        QVERIFY(entry.folios.isEmpty());
        QVERIFY(entry.translatedFrom.isEmpty());
        QVERIFY(entry.exemplar.isEmpty());
    }

    void theCataloguingFieldsAreTrimmed()
    {
        // They come out of prose in an OSIS header, and a stray newline in a
        // table column is a row that no longer lines up with its neighbours.
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[
              {"file":"a.osis","title":"A","role":"manuscript",
               "shelfmark":"\n  Sloane MS 237  \n","folios":"  1r-4v "}]})JSON");
        QCOMPARE(catalogue.entries().first().shelfmark, QStringLiteral("Sloane MS 237"));
        QCOMPARE(catalogue.entries().first().folios, QStringLiteral("1r-4v"));
    }

    void aTranslationIsAsOldAsTheManuscriptItRenders()
    {
        // The one that decides whether the Age column is worth having. A
        // translation's own date is the year somebody translated it — 2017 —
        // and answering that under a column headed Age for a manuscript
        // written between 1500 and 1699 would be worse than answering nothing.
        const ManuscriptCatalogue catalogue = catalogueFrom(sampleManifest());
        const CatalogueEntry &witness = catalogue.entries().at(0);
        const CatalogueEntry &translation = catalogue.entries().at(1);

        QCOMPARE(translation.date, QStringLiteral("2017"));
        QCOMPARE(
            catalogue.manuscriptAge(translation),
            QStringLiteral("between 1500 and 1699"));
        // And a witness is simply its own age.
        QCOMPARE(catalogue.manuscriptAge(witness), witness.date);
    }

    void aTranslationWithNoWitnessKeepsItsOwnDate()
    {
        // A date is better than a blank, and nothing else in the window claims
        // it is the manuscript's.
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[
              {"file":"a_translation.osis","title":"A","role":"translation",
               "shelfmark":"Nowhere in particular","date":"2024"}]})JSON");
        QCOMPARE(
            catalogue.manuscriptAge(catalogue.entries().first()),
            QStringLiteral("2024"));
    }

    void aTranslationIsNotAgedByAnUnrelatedManuscript()
    {
        // Paired on the shelfmark, which names the physical object. Pairing on
        // the book alone would give a Revelation translation the age of
        // whichever Revelation manuscript happened to be listed first.
        const ManuscriptCatalogue catalogue = catalogueFrom(
            R"JSON({"manuscripts":[
              {"file":"REV_other.osis","title":"Other","book":"REV",
               "role":"manuscript","shelfmark":"MS.Oo.1.16.2","date":"ca. 1730"},
              {"file":"REV_x_translation.osis","title":"X","book":"REV",
               "role":"translation","shelfmark":"Sloane MS 237","date":"2017"}]})JSON");
        QCOMPARE(
            catalogue.manuscriptAge(catalogue.entries().at(1)),
            QStringLiteral("2017"));
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

    void theCopyrightAndTheLicenceAreReadApart()
    {
        // Two questions with different answers, and the download window shows
        // them under different labels. Every published text names a holder,
        // while the terms run from "All rights reserved" to CC BY-NC-SA — so a
        // reader told only one of the two has been told the less useful one.
        const ManuscriptCatalogue catalogue = ManuscriptCatalogue::fromJson(
            QJsonDocument::fromJson(sampleManifest()).object());
        QCOMPARE(catalogue.entries().size(), 2);
        QCOMPARE(
            catalogue.entries().at(0).rights, QStringLiteral("© 2017 by Nehemia Gordon"));
        QCOMPARE(catalogue.entries().at(0).license, QStringLiteral("CC BY-NC-SA 4.0"));
    }

    void anEntryWithoutRightsSaysNothingRatherThanGuessing()
    {
        // A manifest written before the fields existed must not have terms
        // invented for it — "no statement" and "no restrictions" are very
        // different claims to put in front of a reader, and the second is the
        // dangerous one.
        const ManuscriptCatalogue catalogue = ManuscriptCatalogue::fromJson(
            QJsonDocument::fromJson(sampleManifest()).object());
        QVERIFY(catalogue.entries().at(1).rights.isEmpty());
        QVERIFY(catalogue.entries().at(1).license.isEmpty());
    }

    void aLicenceWithNoNamedHolderIsStillRead()
    {
        // The two are independent: a bare transcription may credit nobody in
        // particular and still be published under stated terms.
        const ManuscriptCatalogue catalogue = ManuscriptCatalogue::fromJson(
            QJsonDocument::fromJson(
                QByteArray(R"JSON({"manuscripts":[{"file":"a.osis","title":"A",)JSON"
                           R"JSON("role":"manuscript","license":"  CC BY-NC-SA 4.0  "}]})JSON"))
                .object());
        QCOMPARE(catalogue.entries().size(), 1);
        QVERIFY(catalogue.entries().first().rights.isEmpty());
        // Trimmed like the copyright: these come out of prose in an OSIS header.
        QCOMPARE(catalogue.entries().first().license, QStringLiteral("CC BY-NC-SA 4.0"));
    }

    void theRightsAreTrimmed()
    {
        // The OSIS headers wrap this across lines, and a tooltip that opens on
        // a newline reads as broken.
        const ManuscriptCatalogue catalogue = ManuscriptCatalogue::fromJson(
            QJsonDocument::fromJson(
                manifestWithRights("\\n   © 2018 Nehemia Gordon. "
                                   "All rights reserved.\\n  "))
                .object());
        QCOMPARE(catalogue.entries().size(), 1);
        QCOMPARE(catalogue.entries().at(0).rights,
                 QStringLiteral("© 2018 Nehemia Gordon. All rights reserved."));
    }

    void theRightsAreKeptWordForWord()
    {
        // Not summarised, not shortened to a licence code. "All rights
        // reserved" and "CC BY-NC-SA 4.0 (this repository's default; no licence
        // was stated for the transcription itself)" are both statements a
        // reader is entitled to read as written.
        const QByteArray statement =
            "CC BY-NC-SA 4.0 (this repository's default; no licence was stated "
            "for the transcription itself).";
        const ManuscriptCatalogue catalogue = ManuscriptCatalogue::fromJson(
            QJsonDocument::fromJson(manifestWithRights(statement)).object());
        QCOMPARE(catalogue.entries().size(), 1);
        QCOMPARE(catalogue.entries().at(0).rights, QString::fromUtf8(statement));
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
