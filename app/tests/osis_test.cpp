#include "core/osis.h"
#include "core/serialize.h"
#include "core/tokenize.h"
#include "test_data.h"

#include <QtTest>

using namespace milah;

namespace {

ParseOptions options(const QString &id, const QString &name)
{
    ParseOptions parseOptions;
    parseOptions.id = id;
    parseOptions.name = name;
    parseOptions.role = SourceRole::Manuscript;
    return parseOptions;
}

/// A header shaped like the published ones: the text's own `<work>` first, then
/// the versification `<work>` that every one of these files carries after it.
QString osisWithRights(const QString &rights)
{
    return QStringLiteral(
               "<?xml version='1.0' encoding='UTF-8'?>"
               "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
               "<osisText osisIDWork='W' xml:lang='he'><header>"
               "<work osisWork='W'><title>A witness</title>%1<scope>REV</scope>"
               "</work>"
               "<work osisWork='bible'><title>Referenced versification</title>"
               "<refSystem>StandardV11N</refSystem></work>"
               "</header>"
               "<div type='book' osisID='Rev'>"
               "<verse osisID='Rev.1.1'>שלום</verse></div>"
               "</osisText></osis>")
        .arg(rights);
}

} // namespace

class OsisTest final : public QObject
{
    Q_OBJECT

private slots:
    void readsHebrewAndAnchorsAnInlineNote()
    {
        const SourceDocument document = parseOsis(
            QString::fromUtf8(milah_test::kSampleOsis),
            options(QStringLiteral("witness-a"), QStringLiteral("test.osis")));

        const SourceVerse *verse = document.verse(QStringLiteral("Matt.1.1"));
        QVERIFY(verse != nullptr);
        QCOMPARE(verse->text, QString::fromUtf8("ספר הולדת"));
        QVERIFY(!verse->tokens.isEmpty());
        QVERIFY(!verse->tokens.at(0).notes.isEmpty());
        QCOMPARE(verse->tokens.at(0).notes.at(0).text, QStringLiteral("A comment"));
    }

    void theCopyrightAndTheLicenceAreReadApart()
    {
        // Two questions with different answers. Every published text names a
        // holder, while the terms run from "All rights reserved" to
        // CC BY-NC-SA — one field could only ever have carried one of them.
        const SourceDocument document = parseOsis(
            osisWithRights(
                QStringLiteral("<rights type=\"x-copyright\">© 2018 Nehemia Gordon</rights>"
                               "<rights type=\"x-license\">All rights reserved.</rights>")),
            options(QStringLiteral("w"), QStringLiteral("t.osis")));

        QCOMPARE(document.metadata.rights, QStringLiteral("© 2018 Nehemia Gordon"));
        QCOMPARE(document.metadata.license, QStringLiteral("All rights reserved."));
    }

    void anUntypedRightsElementIsReadAsTheCopyright()
    {
        // Every file carried a bare <rights> before the split. Such a header
        // must degrade to answering one question rather than neither — and it
        // must not be mistaken for a licence, which is the more dangerous of
        // the two ways to be wrong.
        const SourceDocument document = parseOsis(
            osisWithRights(QStringLiteral("<rights>© 2017 by Nehemia Gordon</rights>")),
            options(QStringLiteral("w"), QStringLiteral("t.osis")));

        QCOMPARE(document.metadata.rights, QStringLiteral("© 2017 by Nehemia Gordon"));
        QVERIFY(document.metadata.license.isEmpty());
    }

    void aFileStatingNoLicenceClaimsNone()
    {
        // Silence is not a licence. A text that names a holder and no terms
        // must leave the terms empty rather than acquire permissive ones.
        const SourceDocument document = parseOsis(
            osisWithRights(
                QStringLiteral("<rights type=\"x-copyright\">© 2024 Janice F. Baca</rights>")),
            options(QStringLiteral("w"), QStringLiteral("t.osis")));

        QCOMPARE(document.metadata.rights, QStringLiteral("© 2024 Janice F. Baca"));
        QVERIFY(document.metadata.license.isEmpty());
    }

    void aLicenceWithNoNamedHolderIsStillRead()
    {
        // A bare transcription may credit nobody in particular; the terms it is
        // published under are a separate fact and must survive alone.
        const SourceDocument document = parseOsis(
            osisWithRights(
                QStringLiteral("<rights type=\"x-license\">CC BY-NC-SA 4.0</rights>")),
            options(QStringLiteral("w"), QStringLiteral("t.osis")));

        QVERIFY(document.metadata.rights.isEmpty());
        QCOMPARE(document.metadata.license, QStringLiteral("CC BY-NC-SA 4.0"));
    }

    void theRightsAreReadFromTheHeader()
    {
        const SourceDocument document = parseOsis(
            osisWithRights(QStringLiteral("<rights>© 2017 by Nehemia Gordon</rights>")),
            options(QStringLiteral("w"), QStringLiteral("t.osis")));
        QCOMPARE(document.metadata.rights,
                 QStringLiteral("© 2017 by Nehemia Gordon"));
    }

    void aFileStatingNoRightsClaimsNone()
    {
        // Silence is not a licence. An empty field must stay empty rather than
        // acquire terms the file never stated.
        const SourceDocument document =
            parseOsis(osisWithRights(QString()),
                      options(QStringLiteral("w"), QStringLiteral("t.osis")));
        QVERIFY(document.metadata.rights.isEmpty());
    }

    void theVersificationWorkDoesNotSupplyTheTerms()
    {
        // Every published file carries a second <work> for the versification
        // system. Taking the last <rights> seen rather than the first would let
        // that one speak for the text.
        const QString osis = QStringLiteral(
            "<?xml version='1.0' encoding='UTF-8'?>"
            "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
            "<osisText osisIDWork='W' xml:lang='he'><header>"
            "<work osisWork='W'><title>A witness</title>"
            "<rights>© 2017 by Nehemia Gordon</rights></work>"
            "<work osisWork='bible'><rights>Public domain</rights></work>"
            "</header>"
            "<div type='book' osisID='Rev'>"
            "<verse osisID='Rev.1.1'>שלום</verse></div>"
            "</osisText></osis>");
        const SourceDocument document =
            parseOsis(osis, options(QStringLiteral("w"), QStringLiteral("t.osis")));
        QCOMPARE(document.metadata.rights,
                 QStringLiteral("© 2017 by Nehemia Gordon"));
    }

    void theRightsAreKeptWordForWord()
    {
        // "All rights reserved" is not a phrase to paraphrase, shorten, or
        // translate into a licence code.
        const QString statement =
            QStringLiteral("© 2018 Nehemia Gordon. All rights reserved.");
        const SourceDocument document = parseOsis(
            osisWithRights(QStringLiteral("<rights>%1</rights>").arg(statement)),
            options(QStringLiteral("w"), QStringLiteral("t.osis")));
        QCOMPARE(document.metadata.rights, statement);
    }

    void punctuationStandsApartInAManuscript()
    {
        QCOMPARE(
            tokenize(QStringLiteral("Matt.1.1"), QStringLiteral("(horah),"), {})
                .size(),
            4);
    }

    void punctuationBelongsToTheWordInATranslation()
    {
        // A bracket around a gloss is part of the gloss, not a word of its own:
        // "(horah)," is one interlinear cell rather than four.
        const QList<SourceToken> tokens = tokenize(
            QStringLiteral("Matt.1.1"),
            QStringLiteral("(horah), to do"),
            {},
            TokenStyle::Attached);

        QCOMPARE(tokens.size(), 3);
        QCOMPARE(tokens.at(0).text, QStringLiteral("(horah),"));
        QCOMPARE(tokens.at(1).text, QStringLiteral("to"));
        QCOMPARE(tokens.at(2).text, QStringLiteral("do"));
    }

    void theRoleDecidesHowAVerseIsSplit()
    {
        // The choice is made by parseOsis from the role, so a translation and a
        // manuscript holding the same text come back split differently.
        const QString osis = milah_test::witnessOsis(
            QStringLiteral("w"), QStringLiteral("(horah), to"));

        ParseOptions asManuscript = options(QStringLiteral("m"), QStringLiteral("m.osis"));
        ParseOptions asTranslation = asManuscript;
        asTranslation.role = SourceRole::Translation;

        // Held by name: parseOsis returns by value, and a verse pointer into a
        // temporary would dangle the moment the expression ended.
        const SourceDocument manuscriptDocument = parseOsis(osis, asManuscript);
        const SourceDocument translationDocument = parseOsis(osis, asTranslation);

        const SourceVerse *manuscript =
            manuscriptDocument.verse(QStringLiteral("Matt.1.1"));
        const SourceVerse *translation =
            translationDocument.verse(QStringLiteral("Matt.1.1"));
        QVERIFY(manuscript && translation);

        QCOMPARE(translation->tokens.size(), 2);
        QCOMPARE(translation->tokens.at(0).text, QStringLiteral("(horah),"));
        // "(", "horah", ")", ",", "to" — the marks stand apart.
        QCOMPARE(manuscript->tokens.size(), 5);
    }

    void rejectsDtdAndEntityDeclarations()
    {
        QString unsafe = QString::fromUtf8(milah_test::kSampleOsis);
        unsafe.replace(
            QString::fromUtf8(milah_test::kXmlDeclaration),
            QString::fromUtf8(milah_test::kDoctypeDeclaration));

        bool threw = false;
        try {
            parseOsis(
                unsafe,
                options(QStringLiteral("unsafe"), QStringLiteral("unsafe.osis")));
        } catch (const OsisError &error) {
            threw = true;
            QVERIFY2(
                error.message().contains(QStringLiteral("DTD or ENTITY")),
                qPrintable(error.message()));
        }
        QVERIFY2(threw, "parseOsis accepted a document declaring an external entity");
    }
    void aPreambleSurvivesBeingWrittenAndReadAgain()
    {
        // The test that would have caught shipping a writer with no reader.
        // Milah writes matter standing before verse 1 as an introduction div
        // rather than as a verse; text outside a verse used to be dropped on
        // the way back in, so a preamble would have vanished from every file
        // Add to my library wrote.
        CombinedDraft preamble;
        preamble.reference.id = QStringLiteral("Rev.4.0");
        preamble.reference.book = QStringLiteral("Rev");
        preamble.reference.chapter = 4;
        preamble.reference.verse = QStringLiteral("0");
        ConsensusColumn incipit;
        incipit.text = QString::fromUtf8("חזון");
        preamble.columns.append(incipit);

        CombinedDraft first;
        first.reference.id = QStringLiteral("Rev.4.1");
        first.reference.book = QStringLiteral("Rev");
        first.reference.chapter = 4;
        first.reference.verse = QStringLiteral("1");
        ConsensusColumn opening;
        opening.text = QString::fromUtf8("שלום");
        first.columns.append(opening);

        const QString written = serializeCombinedOsis(
            {{preamble.reference.id, preamble}, {first.reference.id, first}});

        const SourceDocument read = parseOsis(
            written, options(QStringLiteral("w"), QStringLiteral("w.osis")));

        QVERIFY2(
            read.hasVerse(QStringLiteral("Rev.4.0")),
            "the preamble did not survive the round trip");
        QCOMPARE(
            read.verse(QStringLiteral("Rev.4.0"))->text.trimmed(),
            QString::fromUtf8("חזון"));
        // And the verse after it is still its own verse, not swallowed by the
        // div that ended before it.
        QVERIFY(read.hasVerse(QStringLiteral("Rev.4.1")));
        QCOMPARE(
            read.verse(QStringLiteral("Rev.4.1"))->text.trimmed(),
            QString::fromUtf8("שלום"));
    }
};

QTEST_MAIN(OsisTest)
#include "osis_test.moc"
