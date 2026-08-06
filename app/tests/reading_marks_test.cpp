#include "core/reading_marks.h"

#include <QtTest>

using namespace milah;

namespace {

/// The reading's own text, put back together. Whatever the marking does, this
/// has to come out equal to what went in.
QString textOf(const MarkedReading &reading)
{
    QString out;
    for (const MarkedSegment &segment : reading) {
        out += segment.text;
    }
    return out;
}

/// The text of every segment carrying `mark`, joined.
QString marked(const MarkedReading &reading, ReadingMark mark)
{
    QString out;
    for (const MarkedSegment &segment : reading) {
        if (segment.mark == mark) {
            out += segment.text;
        }
    }
    return out;
}

} // namespace

/// The rule the verse card has drawn since the beginning, under test for the
/// first time — it used to be three helpers inside the widget that returned
/// HTML, which no suite could reach.
class ReadingMarksTest final : public QObject
{
    Q_OBJECT

private slots:
    void identicalReadingsAreAllPlain()
    {
        const QString word = QString::fromUtf8("בְּרֵאשִׁית");
        const MarkedReading reading = markReferenceReading(word, {word, word});
        QCOMPARE(textOf(reading), word);
        QVERIFY(marked(reading, ReadingMark::Missing).isEmpty());
    }

    void theReferenceIsStruckWhereAWitnessLacksALetter()
    {
        // The other witness reads the same word without its final letter.
        const MarkedReading reading = markReferenceReading(
            QString::fromUtf8("אמרתי"), {QString::fromUtf8("אמרת")});

        QCOMPARE(textOf(reading), QString::fromUtf8("אמרתי"));
        QCOMPARE(marked(reading, ReadingMark::Missing), QString::fromUtf8("י"));
        QCOMPARE(marked(reading, ReadingMark::Plain), QString::fromUtf8("אמרת"));
    }

    void aWitnessThatReadsNothingMarksTheWholeReading()
    {
        // Silence is a variant. A witness present in the verse but reading
        // nothing at this word disagrees with all of it — and this is the case
        // a well-meaning refactor loses first, because an empty string looks
        // like nothing to compare.
        const QString word = QString::fromUtf8("וכהנים");
        const MarkedReading reading = markReferenceReading(word, {QString()});
        QCOMPARE(marked(reading, ReadingMark::Missing), word);
        QVERIFY(marked(reading, ReadingMark::Plain).isEmpty());
    }

    void marksAreOredAcrossWitnesses()
    {
        // One witness drops the first letter, another the last. Both are
        // marked; what they both read stands plain.
        const MarkedReading reading = markReferenceReading(
            QString::fromUtf8("אבג"),
            {QString::fromUtf8("בג"), QString::fromUtf8("אב")});

        QCOMPARE(textOf(reading), QString::fromUtf8("אבג"));
        QCOMPARE(marked(reading, ReadingMark::Plain), QString::fromUtf8("ב"));
        QCOMPARE(marked(reading, ReadingMark::Missing), QString::fromUtf8("אג"));
    }

    void pointingIsNotAVariant()
    {
        // The diff compares grapheme clusters by their unpointed form, which is
        // the decision that keeps a collation about words rather than vowels.
        const MarkedReading reading = markReferenceReading(
            QString::fromUtf8("מֶלֶךְ"), {QString::fromUtf8("מלך")});
        QVERIFY(marked(reading, ReadingMark::Missing).isEmpty());
    }

    void aVariantShowsOnlyWhatItAdds()
    {
        const MarkedReading reading = markVariantReading(
            QString::fromUtf8("אמרת"), QString::fromUtf8("אמרתי"));

        // The witness's own reading, entire, and nothing of the reference's.
        QCOMPARE(textOf(reading), QString::fromUtf8("אמרתי"));
        QCOMPARE(marked(reading, ReadingMark::Added), QString::fromUtf8("י"));
    }

    void whatOnlyTheReferenceReadsIsNotDrawnOnAVariantsRow()
    {
        // The reference reads a word this witness does not. Putting it in this
        // row would be putting a word in a manuscript's mouth; the reference's
        // own row is where that absence is shown, struck.
        const MarkedReading reading = markVariantReading(
            QString::fromUtf8("אמרתי"), QString::fromUtf8("אמרת"));
        QCOMPARE(textOf(reading), QString::fromUtf8("אמרת"));
        QVERIFY(!textOf(reading).contains(QString::fromUtf8("י")));
    }

    void adjacentSegmentsOfTheSameMarkAreOne()
    {
        // A dropped Delete between two agreements must not leave two Plain
        // segments where a reader sees one word: two renderers that disagreed
        // about where a run begins would draw the same reading differently.
        const MarkedReading reading = markVariantReading(
            QString::fromUtf8("אבג"), QString::fromUtf8("אג"));
        QCOMPARE(textOf(reading), QString::fromUtf8("אג"));
        QCOMPARE(reading.size(), 1);
        QCOMPARE(reading.first().mark, ReadingMark::Plain);
    }

    void anEmptyReadingMarksNothing()
    {
        // Not one empty segment — nothing at all, so a caller counting segments
        // is not told there is something here.
        QVERIFY(markReferenceReading(QString(), {QString::fromUtf8("אב")}).isEmpty());
        QVERIFY(markVariantReading(QString::fromUtf8("אב"), QString()).isEmpty());
    }

    void aReadingWithNoOtherWitnessesStandsPlain()
    {
        // One manuscript loaded, or the only one that reaches this verse. There
        // is nothing to differ from, and marking it all would be nonsense.
        const QString word = QString::fromUtf8("שלום");
        QCOMPARE(marked(markReferenceReading(word, {}), ReadingMark::Plain), word);
    }
};

QTEST_MAIN(ReadingMarksTest)
#include "reading_marks_test.moc"
