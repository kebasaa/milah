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
        QCOMPARE(passage.words.size(), 24);
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
        QCOMPARE(passage.words.first(), QStringLiteral("c1v3w1"));
        QCOMPARE(passage.words.size(), 16);

        // A verse in the second chapter, reached by naming the chapter.
        const Passage later = gatherPassage(book(), QStringLiteral("JAS"), 2, 2, 0);
        QCOMPARE(later.words.first(), QStringLiteral("c2v2w1"));
        QCOMPARE(later.words.size(), 8);
    }

    /// The words the folio before this one already holds are dropped, not
    /// carried alongside as an offset for everything downstream to remember —
    /// which is how a leaf came to repeat words the leaf before it had.
    void theWordsTheLastLeafHoldsAreDropped()
    {
        const Passage passage = gatherPassage(book(), QStringLiteral("JAS"), 1, 2, 3);
        QCOMPARE(passage.words.first(), QStringLiteral("c1v2w4"));
        QCOMPARE(passage.verses.first(), QStringLiteral("Jas.1.2"));
        QCOMPARE(passage.words.size(), 17);

        // A whole verse taken: the passage opens on the verse after it.
        const Passage whole = gatherPassage(book(), QStringLiteral("JAS"), 1, 2, 4);
        QCOMPARE(whole.words.first(), QStringLiteral("c1v3w1"));
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

        // And exactly to the end is empty too, not one word over.
        QVERIFY(gatherPassage(book(), QStringLiteral("JAS"), 2, 3, 4).isEmpty());
    }

    /// A book the source does not hold gives nothing rather than the first book
    /// it does — filling a folio of James from Matthew because the names did not
    /// match would be worse than filling nothing.
    void anAbsentBookGivesNothing()
    {
        QVERIFY(gatherPassage(book(), QStringLiteral("MATT"), 1, 1, 0).isEmpty());
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
