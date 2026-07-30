#include "core/alignment.h"
#include "core/osis.h"
#include "core/tokenize.h"
#include "test_data.h"

#include <QtTest>

using namespace milah;

namespace {

SourceDocument witness(const QString &id, const QString &text)
{
    const QString osis = milah_test::witnessOsis(id, text);

    ParseOptions options;
    options.id = id;
    options.name = id + QStringLiteral(".osis");
    options.role = SourceRole::Manuscript;
    return parseOsis(osis, options);
}

DocumentRefs refs(const QList<SourceDocument> &documents)
{
    DocumentRefs result;
    result.reserve(documents.size());
    for (const SourceDocument &document : documents) {
        result.append(&document);
    }
    return result;
}

} // namespace

class ConsensusTest final : public QObject
{
    Q_OBJECT

private slots:
    // --- dividing a witness's word across two columns ----------------------

    void aSplitAddsAColumnNoWitnessReads()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר דוד")),
        };
        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), refs(documents), QStringLiteral("a"));
        QCOMPARE(aligned.columns.size(), 2);

        const AlignedVerse widened = applyColumnSplits(aligned, {0});
        QCOMPARE(widened.columns.size(), 3);

        // The original keeps its readings; the new column has none, because the
        // division belongs to the edition and not to the manuscripts.
        QCOMPARE(widened.columns.at(0).id, aligned.columns.at(0).id);
        QVERIFY(widened.columns.at(0).cell(QStringLiteral("a")) != nullptr);
        QVERIFY(widened.columns.at(1).cells.isEmpty());
        // And everything after it keeps its reading, one place further along.
        QCOMPARE(widened.columns.at(2).id, aligned.columns.at(1).id);
        QVERIFY(widened.columns.at(2).cell(QStringLiteral("a")) != nullptr);
    }

    void splittingTheSameColumnTwiceAddsTwoColumns()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
        };
        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), refs(documents), QStringLiteral("a"));

        const AlignedVerse widened = applyColumnSplits(aligned, {0, 0});
        QCOMPARE(widened.columns.size(), aligned.columns.size() + 2);
        QVERIFY(widened.columns.at(1).cells.isEmpty());
        QVERIFY(widened.columns.at(2).cells.isEmpty());
        QCOMPARE(widened.columns.at(3).id, aligned.columns.at(1).id);
    }

    void unsplittingRestoresTheOriginalColumns()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר דוד")),
        };
        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), refs(documents), QStringLiteral("a"));

        // Joining a divided word back drops one entry from the list, which has
        // to leave the columns exactly as the alignment first made them.
        QList<int> splits = {0};
        QCOMPARE(applyColumnSplits(aligned, splits).columns.size(), 3);
        splits.removeOne(0);
        const AlignedVerse restored = applyColumnSplits(aligned, splits);

        QCOMPARE(restored.columns.size(), aligned.columns.size());
        for (int index = 0; index < aligned.columns.size(); ++index) {
            QCOMPARE(restored.columns.at(index).id, aligned.columns.at(index).id);
            QVERIFY(restored.columns.at(index).cell(QStringLiteral("a")) != nullptr);
        }
    }

    void anOutOfRangeSplitIsIgnored()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
        };
        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), refs(documents), QStringLiteral("a"));

        // A project saved against a longer verse must not corrupt a shorter one.
        QCOMPARE(applyColumnSplits(aligned, {99}).columns.size(), aligned.columns.size());
        QCOMPARE(applyColumnSplits(aligned, {-1}).columns.size(), aligned.columns.size());
        QCOMPARE(applyColumnSplits(aligned, {}).columns.size(), aligned.columns.size());
    }

    void aColumnKnowsWhichDividedWordItBelongsTo()
    {
        // Two original columns, the first divided twice: on screen that is
        // [0 0a 0b] [1], and dividing or joining has to act on whole groups.
        const QList<int> splits = {0, 0};

        const ColumnGroup first = columnGroupFor(splits, 4, 1);
        QCOMPARE(first.original, 0);
        QCOMPARE(first.start, 0);
        QCOMPARE(first.size, 3);

        const ColumnGroup second = columnGroupFor(splits, 4, 3);
        QCOMPARE(second.original, 1);
        QCOMPARE(second.start, 3);
        QCOMPARE(second.size, 1);

        QCOMPARE(columnGroupFor(splits, 4, -1).original, -1);
        QCOMPARE(columnGroupFor(splits, 4, 4).original, -1);
    }

    void anIgnoredSplitDoesNotShiftTheGroups()
    {
        // A project saved against a longer verse can name a column this one has
        // not. applyColumnSplits drops such an entry, so counting it here would
        // put every group one place out and divide the wrong word.
        const QList<int> splits = {0, 99};
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
        };
        const AlignedVerse aligned = applyColumnSplits(
            alignVerse(QStringLiteral("Matt.1.1"), refs(documents), QStringLiteral("a")),
            splits);
        QCOMPARE(aligned.columns.size(), 3);

        const ColumnGroup group = columnGroupFor(splits, int(aligned.columns.size()), 1);
        QCOMPARE(group.original, 0);
        QCOMPARE(group.start, 0);
        QCOMPARE(group.size, 2);

        const ColumnGroup last = columnGroupFor(splits, int(aligned.columns.size()), 2);
        QCOMPARE(last.original, 1);
        QCOMPARE(last.size, 1);
    }

    void aWordIsDividedAtItsMaqafOrSpace()
    {
        const QStringList maqaf = dividedWords(QString::fromUtf8("אֲשֶׁר־בָּהּ"));
        QCOMPARE(maqaf.size(), 2);
        QCOMPARE(maqaf.first(), QString::fromUtf8("אֲשֶׁר"));
        QCOMPARE(maqaf.last(), QString::fromUtf8("בָּהּ"));

        QCOMPARE(dividedWords(QString::fromUtf8("אֲשֶׁר בָּהּ")).size(), 2);
        QCOMPARE(dividedWords(QString::fromUtf8("אֲשֶׁר-בָּהּ")).size(), 2);

        // Nothing to divide at: the caller uses the count to decide whether to
        // offer dividing at all, so a plain word must come back alone.
        QCOMPARE(dividedWords(QString::fromUtf8("אֲשֶׁר")).size(), 1);
        QCOMPARE(dividedWords(QString()).size(), 1);
    }

    void usesAStrictMajority()
    {
        const QList<SourceDocument> majority = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר")),
            witness(QStringLiteral("b"), QString::fromUtf8("סֵפֶר")),
            witness(QStringLiteral("c"), QString::fromUtf8("דבר")),
        };
        const DocumentRefs sources = refs(majority);

        const CombinedDraft draft = generateCombined(
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("a")),
            sources,
            QStringLiteral("a"));

        QVERIFY(!draft.columns.isEmpty());
        QCOMPARE(draft.columns.at(0).text.value_or(QString()), QString::fromUtf8("ספר"));
        QCOMPARE(draft.columns.at(0).needsReview, false);
    }

    void fallsBackToThePriorityWitnessForATie()
    {
        const QList<SourceDocument> tie = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר")),
            witness(QStringLiteral("b"), QString::fromUtf8("דבר")),
        };
        const DocumentRefs sources = refs(tie);

        const CombinedDraft draft = generateCombined(
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("b")),
            sources,
            QStringLiteral("b"));

        QVERIFY(!draft.columns.isEmpty());
        QCOMPARE(draft.columns.at(0).text.value_or(QString()), QString::fromUtf8("דבר"));
        QCOMPARE(draft.columns.at(0).needsReview, true);
    }
};

QTEST_MAIN(ConsensusTest)
#include "consensus_test.moc"
