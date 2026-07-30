#include "core/diff.h"

#include <QtTest>

using namespace milah;

namespace {

QString before(const QList<DiffSegment> &segments, DiffOp op)
{
    QString result;
    for (const DiffSegment &segment : segments) {
        if (segment.op == op) {
            result += segment.before;
        }
    }
    return result;
}

QString after(const QList<DiffSegment> &segments, DiffOp op)
{
    QString result;
    for (const DiffSegment &segment : segments) {
        if (segment.op == op) {
            result += segment.after;
        }
    }
    return result;
}

} // namespace

class DiffTest final : public QObject
{
    Q_OBJECT

private slots:
    void identicalReadingsProduceOneEqualSegment()
    {
        const QList<DiffSegment> segments =
            diffGraphemes(QString::fromUtf8("ספר"), QString::fromUtf8("ספר"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).op, DiffOp::Equal);
        QCOMPARE(segments.at(0).before, QString::fromUtf8("ספר"));
        QCOMPARE(segments.at(0).after, QString::fromUtf8("ספר"));
    }

    void aSubstitutionShowsBothSides()
    {
        const QList<DiffSegment> segments =
            diffGraphemes(QString::fromUtf8("ספר"), QString::fromUtf8("סבר"));

        QCOMPARE(before(segments, DiffOp::Delete), QString::fromUtf8("פ"));
        QCOMPARE(after(segments, DiffOp::Insert), QString::fromUtf8("ב"));
        QCOMPARE(before(segments, DiffOp::Equal), QString::fromUtf8("סר"));
    }

    void aSuffixOnlyShowsAsAnInsertion()
    {
        const QList<DiffSegment> segments =
            diffGraphemes(QString::fromUtf8("משיח"), QString::fromUtf8("משיחה"));

        QVERIFY(before(segments, DiffOp::Delete).isEmpty());
        QCOMPARE(after(segments, DiffOp::Insert), QString::fromUtf8("ה"));
        QCOMPARE(segments.size(), 2);
    }

    void aDroppedPrefixShowsAsADeletion()
    {
        const QList<DiffSegment> segments =
            diffGraphemes(QString::fromUtf8("הלידה"), QString::fromUtf8("לידה"));

        QCOMPARE(before(segments, DiffOp::Delete), QString::fromUtf8("ה"));
        QVERIFY(after(segments, DiffOp::Insert).isEmpty());
        QCOMPARE(segments.at(0).op, DiffOp::Delete);
    }

    void pointingIsNotAVariant()
    {
        const QList<DiffSegment> segments =
            diffGraphemes(QString::fromUtf8("סֵפֶר"), QString::fromUtf8("ספר"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).op, DiffOp::Equal);
        // Each side keeps its own spelling, so a row can be drawn from it.
        QCOMPARE(segments.at(0).before, QString::fromUtf8("סֵפֶר"));
        QCOMPARE(segments.at(0).after, QString::fromUtf8("ספר"));
    }

    void pointingStaysWithItsLetter()
    {
        const QList<DiffSegment> segments =
            diffGraphemes(QString::fromUtf8("סֵפֶר"), QString::fromUtf8("סֵבֶר"));

        // The differing letter is reported with its points attached, never as
        // a bare mark stranded from its consonant.
        for (const DiffSegment &segment : segments) {
            for (const QString &cluster : graphemes(segment.before + segment.after)) {
                QVERIFY(!cluster.at(0).isMark());
            }
        }
        QCOMPARE(before(segments, DiffOp::Delete), QString::fromUtf8("פֶ"));
        QCOMPARE(after(segments, DiffOp::Insert), QString::fromUtf8("בֶ"));
    }

    void anEmptyReadingIsAllDeletion()
    {
        const QList<DiffSegment> segments =
            diffGraphemes(QString::fromUtf8("דוד"), QString());

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).op, DiffOp::Delete);
        QCOMPARE(segments.at(0).before, QString::fromUtf8("דוד"));
    }
};

QTEST_MAIN(DiffTest)
#include "diff_test.moc"
