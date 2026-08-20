#include "core/osis.h"
#include "core/osis_fill.h"

#include <QtTest>

using namespace milah;

namespace {

/// Two chapters of a short book, with countable words: word 2 of verse 3 in
/// chapter 1 is "c1v3w2", so any mistake about where a passage begins is legible
/// rather than a wall of Hebrew.
///
/// No punctuation inside a word. The tokeniser splits on it — a first attempt
/// using "1-3-2" came back as three tokens apiece and turned a 24-word book into
/// a 72-word one, which is the fixture lying rather than the code.
SourceDocument book()
{
    QString verses;
    for (int chapter = 1; chapter <= 2; ++chapter) {
        for (int verse = 1; verse <= 3; ++verse) {
            QStringList words;
            for (int word = 1; word <= 4; ++word) {
                words << QStringLiteral("c%1v%2w%3").arg(chapter).arg(verse).arg(word);
            }
            verses += QStringLiteral("<verse osisID='Jas.%1.%2'>%3</verse>")
                          .arg(chapter)
                          .arg(verse)
                          .arg(words.join(QLatin1Char(' ')));
        }
    }
    return parseOsis(
        QStringLiteral(
            "<?xml version='1.0' encoding='UTF-8'?>"
            "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
            "<osisText osisIDWork='W' xml:lang='he'><header>"
            "<work osisWork='W'><title>A witness</title></work>"
            "</header>"
            "<div type='book' osisID='Jas'>%1</div>"
            "</osisText></osis>")
            .arg(verses),
        ParseOptions{});
}

} // namespace

class OsisFillTest final : public QObject
{
    Q_OBJECT

private slots:
    /// A folio carries Milah's own book code, `JAS`; an OSIS carries `Jas`.
    /// Comparing them exactly is a match that never happens, and the symptom is
    /// not an error — it is a continuation quietly deciding there is nothing to
    /// carry on.
    void aBookIsFoundWhateverItsCase()
    {
        QCOMPARE(bookNamed(book(), QStringLiteral("JAS")), QStringLiteral("Jas"));
        QCOMPARE(bookNamed(book(), QStringLiteral("jas")), QStringLiteral("Jas"));
        // The source's own spelling comes back, not the one asked with, because
        // that is what everything downstream compares against.
        QCOMPARE(bookNamed(book(), QStringLiteral("Jas")), QStringLiteral("Jas"));

        QVERIFY(bookNamed(book(), QStringLiteral("Matt")).isEmpty());
        QVERIFY(bookNamed(book(), QString()).isEmpty());
    }

    /// From the verse asked for to the end of the book — a folio runs over a
    /// chapter break as readily as not.
    void aPassageRunsToTheEndOfTheBook()
    {
        const Passage passage = gatherPassage(book(), QStringLiteral("JAS"), 1, 1, 0);
        // 24 words and the four numerals: verse 1 of each chapter is unnumbered.
        QCOMPARE(passage.words.size(), 28);
        QCOMPARE(passage.words.first(), QStringLiteral("c1v1w1"));
        QCOMPARE(passage.words.last(), QStringLiteral("c2v3w4"));
        QCOMPARE(passage.verses.first(), QStringLiteral("Jas.1.1"));
        QCOMPARE(passage.verses.last(), QStringLiteral("Jas.2.3"));
    }

    /// Starting partway in starts at that verse's first word, and at nothing
    /// else. This is the one the whole feature turns on.
    void aPassageStartsAtTheVerseAskedFor()
    {
        const Passage passage = gatherPassage(book(), QStringLiteral("JAS"), 1, 3, 0);
        // Its numeral first, which is what the scribe wrote first.
        QCOMPARE(passage.words.first(), QStringLiteral("3"));
        QCOMPARE(passage.words.at(1), QStringLiteral("c1v3w1"));
        QCOMPARE(passage.words.size(), 19);

        // A verse in the second chapter, reached by naming the chapter.
        const Passage later = gatherPassage(book(), QStringLiteral("JAS"), 2, 2, 0);
        QCOMPARE(later.words.first(), QStringLiteral("2"));
        QCOMPARE(later.words.at(1), QStringLiteral("c2v2w1"));
        QCOMPARE(later.words.size(), 10);
    }

    /// The words the folio before this one already holds are dropped, not
    /// carried alongside as an offset for everything downstream to remember —
    /// which is how a leaf came to repeat words the leaf before it had.
    void theWordsTheLastLeafHoldsAreDropped()
    {
        const Passage passage = gatherPassage(book(), QStringLiteral("JAS"), 1, 2, 3);
        // The numeral and two words gone, so the passage opens on the third.
        QCOMPARE(passage.words.first(), QStringLiteral("c1v2w3"));
        QCOMPARE(passage.verses.first(), QStringLiteral("Jas.1.2"));
        QCOMPARE(passage.words.size(), 21);

        // A whole verse taken — its numeral and its four words — so the passage
        // opens on the numeral of the verse after it.
        const Passage whole = gatherPassage(book(), QStringLiteral("JAS"), 1, 2, 5);
        QCOMPARE(whole.words.first(), QStringLiteral("3"));
        QCOMPARE(whole.words.at(1), QStringLiteral("c1v3w1"));
        QCOMPARE(whole.verses.first(), QStringLiteral("Jas.1.3"));
    }

    /// A skip past the end gives nothing, rather than reading off the end of the
    /// word list — the verse lists are indexed in step and a short read there
    /// would be a crash, not a wrong answer.
    void aSkipPastTheEndGivesNothing()
    {
        const Passage passage = gatherPassage(book(), QStringLiteral("JAS"), 2, 3, 99);
        QVERIFY(passage.isEmpty());
        QVERIFY(passage.verses.isEmpty());

        // And exactly to the end is empty too, not one word over — five here,
        // the verse's numeral and its four words.
        QVERIFY(gatherPassage(book(), QStringLiteral("JAS"), 2, 3, 5).isEmpty());
    }

    /// A book the source does not hold gives nothing rather than the first book
    /// it does — filling a folio of James from Matthew because the names did not
    /// match would be worse than filling nothing.
    void anAbsentBookGivesNothing()
    {
        QVERIFY(gatherPassage(book(), QStringLiteral("MATT"), 1, 1, 0).isEmpty());
    }

    /// A compound joined at a maqqef is one word, because the scribe wrote it
    /// once and it sits in one box.
    ///
    /// This is the drift on 158r of MS Oo.1.32, found by reading the poured
    /// text against the source word by word: 95 words agree, then the source's
    /// `בכל־דרכיו` arrives as two, and from there to the foot of the leaf every
    /// word sits one place late. core/tokenize.cpp splits it deliberately, and
    /// rightly — for collation, where the compound has to line up against a
    /// witness writing two words. A fill is not collation.
    void aCompoundJoinedAtAMaqqefIsOneWord()
    {
        // fromUtf8, not QStringLiteral: the latter reads these bytes as Latin-1
        // and turns one Hebrew word into nineteen characters of nonsense, which
        // the tokeniser then splits into nineteen words.
        const SourceDocument source = parseOsis(
            QString::fromUtf8(
                "<?xml version='1.0' encoding='UTF-8'?>"
                "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
                "<osisText osisIDWork='W' xml:lang='he'><header>"
                "<work osisWork='W'><title>A witness</title></work></header>"
                "<div type='book' osisID='Jas'>"
                "<verse osisID='Jas.1.1'>alpha \xd7\x91\xd7\x9b\xd7\x9c\xd6\xbe"
                "\xd7\x93\xd7\xa8\xd7\x9b\xd7\x99\xd7\x95 omega</verse>"
                "</div></osisText></osis>"),
            ParseOptions{});

        const Passage passage = gatherPassage(source, QStringLiteral("Jas"), 1, 1, 0);
        QCOMPARE(passage.words.size(), 3);
        QCOMPARE(passage.words.at(0), QStringLiteral("alpha"));
        QCOMPARE(passage.words.at(1),
                 QString::fromUtf8("\xd7\x91\xd7\x9b\xd7\x9c\xd6\xbe"
                                   "\xd7\x93\xd7\xa8\xd7\x9b\xd7\x99\xd7\x95"));
        QCOMPARE(passage.words.at(2), QStringLiteral("omega"));
        // The verse list stays in step, or the folio is cut into verses wrongly.
        QCOMPARE(passage.verses.size(), 3);
    }

    /// An ASCII hyphen joins the same way. Sloane 237 writes 48 of its 434
    /// words in compounds and uses a hyphen for the purpose.
    void aCompoundJoinedAtAHyphenIsOneWordToo()
    {
        const SourceDocument source = parseOsis(
            QStringLiteral(
                "<?xml version='1.0' encoding='UTF-8'?>"
                "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
                "<osisText osisIDWork='W' xml:lang='he'><header>"
                "<work osisWork='W'><title>A witness</title></work></header>"
                "<div type='book' osisID='Jas'>"
                "<verse osisID='Jas.1.1'>ani-yohanan saw</verse>"
                "</div></osisText></osis>"),
            ParseOptions{});

        const Passage passage = gatherPassage(source, QStringLiteral("Jas"), 1, 1, 0);
        QCOMPARE(passage.words.size(), 2);
        QCOMPARE(passage.words.at(0), QStringLiteral("ani-yohanan"));
    }

    /// A mark written against a word belongs to it.
    ///
    /// The tokeniser ends with a fallback matching any single character it did
    /// not recognise, so `רעים׃` comes back as the word and then the sof pasuq.
    /// Right for collation, which aligns words; wrong here, because the scribe
    /// wrote the mark against the word in one box, and a token of its own takes
    /// a box from the line and pushes everything after it along — the same fault
    /// as the maqqef, from the other side.
    ///
    /// Two of these in the whole of James, against one maqqef compound. With
    /// both mended, gatherPassage reproduces that book **word for word**: 1441
    /// against 1441, nothing out of place.
    void aMarkWrittenAgainstAWordBelongsToIt()
    {
        const SourceDocument source = parseOsis(
            QString::fromUtf8(
                "<?xml version='1.0' encoding='UTF-8'?>"
                "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
                "<osisText osisIDWork='W' xml:lang='he'><header>"
                "<work osisWork='W'><title>A witness</title></work></header>"
                "<div type='book' osisID='Jas'>"
                "<verse osisID='Jas.1.1'>\xd7\xa8\xd7\xa2\xd7\x99\xd7\x9d\xd7\x83 "
                "\xd7\x90\xd7\xaa\xd7\x9d</verse>"
                "</div></osisText></osis>"),
            ParseOptions{});

        const Passage passage = gatherPassage(source, QStringLiteral("Jas"), 1, 1, 0);
        QCOMPARE(passage.words.size(), 2);
        QCOMPARE(passage.words.at(0),
                 QString::fromUtf8("\xd7\xa8\xd7\xa2\xd7\x99\xd7\x9d\xd7\x83"));
        QCOMPARE(passage.verses.size(), 2);
    }

    /// **The verse numbers the scribe wrote are words of the passage.**
    ///
    /// Oo.1.32 numbers its verses in the running text, in Arabic digits — `2:`,
    /// `3:` … `20:` — so the segmenter finds a box for each. A pour that walks
    /// past them lays the verse's first word onto the numeral's box and puts
    /// every word after it one place out for the rest of the leaf: the same
    /// fault as the maqqef and the sof pasuq, in a third disguise.
    ///
    /// The number comes from the OSIS `n=` attribute. Where a file omits it,
    /// core/osis.cpp fills the label in from the verse number itself, so there
    /// is always one to write.
    void aVerseOpensWithTheNumberTheScribeWrote()
    {
        const SourceDocument source = parseOsis(
            QStringLiteral(
                "<?xml version='1.0' encoding='UTF-8'?>"
                "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
                "<osisText osisIDWork='W' xml:lang='he'><header>"
                "<work osisWork='W'><title>A witness</title></work></header>"
                "<div type='book' osisID='Jas'>"
                "<verse osisID='Jas.1.1' n='1'>alpha beta</verse>"
                "<verse osisID='Jas.1.2' n='2'>gamma delta</verse>"
                "<verse osisID='Jas.1.3' n='3'>epsilon</verse>"
                "</div></osisText></osis>"),
            ParseOptions{});

        const Passage passage = gatherPassage(source, QStringLiteral("Jas"), 1, 1, 0);
        const QStringList expected = {
            QStringLiteral("alpha"),
            QStringLiteral("beta"),
            QStringLiteral("2"),
            QStringLiteral("gamma"),
            QStringLiteral("delta"),
            QStringLiteral("3"),
            QStringLiteral("epsilon"),
        };
        QCOMPARE(passage.words, expected);
        // The numeral belongs to the verse it opens, not the one it follows —
        // which is what cuts the folio into verses in the right places.
        QCOMPARE(passage.verses.at(2), QStringLiteral("Jas.1.2"));
        QCOMPARE(passage.verses.size(), passage.words.size());
    }

    /// Verse 1 carries no numeral, because the manuscript writes none.
    ///
    /// Twice on these folios the numbering starts at 2: James opens `יעקב עבד ה`
    /// with nothing before it, and after the `פרק` heading on 159r the next
    /// chapter opens the same way. A number tells a verse apart from the one
    /// before it, and the first verse of a chapter has nothing to be told apart
    /// from.
    void theFirstVerseOfAChapterIsNotNumbered()
    {
        const Passage passage = gatherPassage(book(), QStringLiteral("JAS"), 1, 1, 0);
        QCOMPARE(passage.words.first(), QStringLiteral("c1v1w1"));
        // And again where the second chapter opens, four words into it.
        const int opensChapterTwo = passage.words.indexOf(QStringLiteral("c2v1w1"));
        QVERIFY(opensChapterTwo > 0);
        QCOMPARE(passage.words.at(opensChapterTwo - 1), QStringLiteral("c1v3w4"));
    }

    /// A verse the edition prints with no text gets no numeral either. This
    /// source prints Jas 1:21 empty, because the manuscript has none — and a
    /// number written onto the leaf for a verse that is not on it would take a
    /// box from the verse that is.
    void aVerseWithNoTextGetsNoNumber()
    {
        const SourceDocument source = parseOsis(
            QStringLiteral(
                "<?xml version='1.0' encoding='UTF-8'?>"
                "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>"
                "<osisText osisIDWork='W' xml:lang='he'><header>"
                "<work osisWork='W'><title>A witness</title></work></header>"
                "<div type='book' osisID='Jas'>"
                "<verse osisID='Jas.1.1' n='1'>alpha</verse>"
                "<verse osisID='Jas.1.2' n='2'></verse>"
                "<verse osisID='Jas.1.3' n='3'>beta</verse>"
                "</div></osisText></osis>"),
            ParseOptions{});

        const Passage passage = gatherPassage(source, QStringLiteral("Jas"), 1, 1, 0);
        const QStringList expected = {
            QStringLiteral("alpha"),
            QStringLiteral("3"),
            QStringLiteral("beta"),
        };
        QCOMPARE(passage.words, expected);
    }

    /// One word, one verse id, always — the folio is cut into verses by walking
    /// these two in step.
    void everyWordCarriesItsVerse()
    {
        const Passage passage = gatherPassage(book(), QStringLiteral("JAS"), 1, 1, 5);
        QCOMPARE(passage.words.size(), passage.verses.size());
    }
};

QTEST_MAIN(OsisFillTest)
#include "osis_fill_test.moc"
