#include "core/osis.h"
#include "core/serialize.h"
#include "core/versification.h"

#include <QtTest>

using namespace milah;

namespace {

SourceDocument englishMalachi()
{
    // Three verses at the seams, numbered the English way, in an English file.
    const QString osis = QStringLiteral(
        "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
        "<osisText osisIDWork='KJV' xml:lang='en'><div type='book' osisID='Mal'>"
        "<verse osisID='Mal.3.18'>discern</verse>"
        "<verse osisID='Mal.4.1'>the day cometh</verse>"
        "<verse osisID='Mal.4.6'>turn the heart</verse>"
        "</div></osisText></osis>");
    ParseOptions options;
    options.id = QStringLiteral("kjv");
    options.role = SourceRole::Translation;
    return parseOsis(osis, options);
}

} // namespace

/// The English and the Hebrew numbering of the Hebrew Bible, made to agree
/// before witnesses numbered either way are read against each other.
class VersificationTest final : public QObject
{
    Q_OBJECT

private slots:
    void theTableIsShipped() { QVERIFY(!Versification::shared().isEmpty()); }

    void theKnownSeamsMap()
    {
        const Versification &map = Versification::shared();
        QCOMPARE(map.toHebrew(QStringLiteral("Mal.4.1")), QStringLiteral("Mal.3.19"));
        QCOMPARE(map.toHebrew(QStringLiteral("Mal.4.6")), QStringLiteral("Mal.3.24"));
        QCOMPARE(map.toHebrew(QStringLiteral("Joel.2.28")), QStringLiteral("Joel.3.1"));
        QCOMPARE(map.toHebrew(QStringLiteral("Joel.3.21")), QStringLiteral("Joel.4.21"));
        QCOMPARE(map.toHebrew(QStringLiteral("Gen.31.55")), QStringLiteral("Gen.32.1"));
        QCOMPARE(map.toHebrew(QStringLiteral("Jonah.1.17")), QStringLiteral("Jonah.2.1"));
        // Where the two agree, nothing moves.
        QCOMPARE(map.toHebrew(QStringLiteral("Gen.1.1")), QStringLiteral("Gen.1.1"));
        QCOMPARE(map.toHebrew(QStringLiteral("Mal.3.18")), QStringLiteral("Mal.3.18"));
        QCOMPARE(map.toHebrew(QStringLiteral("Rev.1.1")), QStringLiteral("Rev.1.1"));
    }

    void aPsalmHeadingIsTheHebrewFirstVerse()
    {
        const Versification &map = Versification::shared();
        QCOMPARE(map.toHebrew(QStringLiteral("Ps.3.1")), QStringLiteral("Ps.3.2"));
        QCOMPARE(map.toHebrew(QStringLiteral("Ps.3.0")), QStringLiteral("Ps.3.1"));
        // Two verses of heading.
        QCOMPARE(map.toHebrew(QStringLiteral("Ps.51.1")), QStringLiteral("Ps.51.3"));
        // No heading, no shift.
        QCOMPARE(map.toHebrew(QStringLiteral("Ps.1.1")), QStringLiteral("Ps.1.1"));
    }

    void whichNumberingASourceFollows()
    {
        SourceDocument document;
        document.metadata.language = QStringLiteral("he");
        QCOMPARE(numberingOf(document), Numbering::Hebrew);
        document.metadata.language = QStringLiteral("en");
        QCOMPARE(numberingOf(document), Numbering::English);
        // The header outranks the language.
        document.metadata.refSystem = QStringLiteral("Bible.Leningrad");
        QCOMPARE(numberingOf(document), Numbering::Hebrew);
        document.metadata.language = QStringLiteral("he");
        document.metadata.refSystem = QStringLiteral("Bible.KJV");
        QCOMPARE(numberingOf(document), Numbering::English);
    }

    void anEnglishDocumentIsRenumbered()
    {
        SourceDocument document = englishMalachi();
        QCOMPARE(toHebrewNumbering(document), 2);
        QVERIFY(document.hasVerse(QStringLiteral("Mal.3.18")));
        QVERIFY(document.hasVerse(QStringLiteral("Mal.3.19")));
        QVERIFY(document.hasVerse(QStringLiteral("Mal.3.24")));
        QVERIFY(!document.hasVerse(QStringLiteral("Mal.4.1")));

        const SourceVerse *moved = document.verse(QStringLiteral("Mal.3.19"));
        QCOMPARE(moved->text, QStringLiteral("the day cometh"));
        QCOMPARE(moved->reference.chapter, 3);
        QCOMPARE(moved->reference.verse, QStringLiteral("19"));
        // Its own number, for showing beside the shared one.
        QCOMPARE(moved->altNumber, std::optional<QString>(QStringLiteral("4:1")));
        // Every word moved with it.
        for (const SourceToken &token : moved->tokens) {
            QVERIFY(token.id.startsWith(QStringLiteral("Mal.3.19:")));
        }
    }

    void aHebrewDocumentIsLeftAlone()
    {
        SourceDocument document = englishMalachi();
        document.metadata.refSystem = QStringLiteral("Bible.WLC");
        QCOMPARE(toHebrewNumbering(document), 0);
        QVERIFY(document.hasVerse(QStringLiteral("Mal.4.1")));
    }

    void aWholeChapterShiftingByOneDoesNotCollide()
    {
        // Every English verse of Gen 32 moves one on, onto the number the
        // verse after it is leaving.
        QString verses;
        for (int verse = 1; verse <= 32; ++verse) {
            verses += QStringLiteral("<verse osisID='Gen.32.%1'>v%1</verse>").arg(verse);
        }
        ParseOptions options;
        options.id = QStringLiteral("kjv");
        SourceDocument document = parseOsis(
            QStringLiteral("<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
                           "<osisText osisIDWork='KJV' xml:lang='en'><div type='book' osisID='Gen'>"
                           "<verse osisID='Gen.31.55'>v55</verse>%1</div></osisText></osis>")
                .arg(verses),
            options);
        QCOMPARE(toHebrewNumbering(document), 33);
        QCOMPARE(document.verse(QStringLiteral("Gen.32.1"))->text, QStringLiteral("v55"));
        QCOMPARE(document.verse(QStringLiteral("Gen.32.33"))->text, QStringLiteral("v32"));
        QCOMPARE(document.verses.size(), 33);
    }

    void aHebrewBibleEditionSaysItsNumbering()
    {
        CombinedDraft draft;
        draft.reference = {QStringLiteral("Mal.3.19"), QStringLiteral("Mal"), 3,
                           QStringLiteral("19")};
        ConsensusColumn column;
        column.text = QStringLiteral("x");
        draft.columns.append(column);
        QVERIFY(serializeCombinedOsis({{draft.reference.id, draft}})
                    .contains(QStringLiteral("<refSystem>Bible.Leningrad</refSystem>")));
    }
};

QTEST_MAIN(VersificationTest)
#include "versification_test.moc"
