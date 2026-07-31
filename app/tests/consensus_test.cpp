#include "core/alignment.h"
#include "core/osis.h"
#include "core/tokenize.h"
#include "test_data.h"

#include <QtTest>

using namespace milah;

namespace {

// Shared with alignment_test.cpp so the two cannot develop different ideas of
// what a witness is.
using milah_test::refs;
using milah_test::witness;

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

    // --- the notes a column carries -----------------------------------------

    void aWitnessNoteIsFoundOnItsColumn()
    {
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("a"),
                QString::fromUtf8("ספר<note n=\"1\">a scribal remark</note> דוד")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר דוד")),
        };
        const DocumentRefs sources = refs(documents);
        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("a"));

        const QList<ColumnNote> notes = columnNotes(aligned.columns.at(0), sources);
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().sourceId, QStringLiteral("a"));
        QCOMPARE(notes.first().note.number, QStringLiteral("1"));
        QCOMPARE(notes.first().note.text, QStringLiteral("a scribal remark"));

        // The word the note follows, and no other.
        QVERIFY(columnNotes(aligned.columns.at(1), sources).isEmpty());
    }

    void notesFromSeveralWitnessesComeBackInSourceOrder()
    {
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("a"),
                QString::fromUtf8("ספר<note n=\"1\">alpha</note> דוד")),
            witness(
                QStringLiteral("b"),
                QString::fromUtf8("ספר<note n=\"2\">beta</note> דוד")),
        };
        const DocumentRefs sources = refs(documents);
        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("a"));

        const QList<ColumnNote> notes = columnNotes(aligned.columns.at(0), sources);
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes.at(0).sourceId, QStringLiteral("a"));
        QCOMPARE(notes.at(1).sourceId, QStringLiteral("b"));

        // The caller's order decides, so the panel can list witnesses however
        // the window does and never reshuffle between one selection and the next.
        const DocumentRefs reversed = {sources.at(1), sources.at(0)};
        const QList<ColumnNote> swapped = columnNotes(aligned.columns.at(0), reversed);
        QCOMPARE(swapped.at(0).sourceId, QStringLiteral("b"));
        QCOMPARE(swapped.at(1).sourceId, QStringLiteral("a"));
    }

    void anUnannotatedVerseHasNoNotesAnywhere()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
        };
        const DocumentRefs sources = refs(documents);
        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("a"));

        for (const AlignmentColumn &column : aligned.columns) {
            QVERIFY(columnNotes(column, sources).isEmpty());
        }
    }

    // --- spreading a translation over its own manuscript's columns ----------

    void aTranslationStartsOnItsOwnManuscriptsFirstWord()
    {
        // Columns 0 and 2 belong to another witness; this manuscript reads 1, 3
        // and 4. Spread across the whole verse the first word would land on
        // column 0, which its manuscript is silent for.
        const QList<TranslationSpan> spans = alignTranslation(
            QStringLiteral("t"), QStringLiteral("Matt.1.1"), 3, {1, 3, 4});

        QCOMPARE(spans.size(), 3);
        QCOMPARE(spans.at(0).columnStart, 1);
        QCOMPARE(spans.at(1).columnStart, 3);
        QCOMPARE(spans.at(2).columnStart, 4);
        // Counts match, so nothing here was guessed at.
        QCOMPARE(spans.at(0).confidence, SpanConfidence::High);

        // No span may begin on a column the manuscript does not read.
        for (const TranslationSpan &span : spans) {
            QVERIFY(QList<int>({1, 3, 4}).contains(span.columnStart));
            QVERIFY(span.columnEnd > span.columnStart);
        }
    }

    void moreWordsThanColumnsStillFitInside()
    {
        const QList<TranslationSpan> spans = alignTranslation(
            QStringLiteral("t"), QStringLiteral("Matt.1.1"), 5, {2, 3});

        QCOMPARE(spans.size(), 5);
        for (const TranslationSpan &span : spans) {
            QVERIFY(span.columnStart >= 2);
            QVERIFY(span.columnEnd <= 4);
            // A guess, since the counts do not match.
            QCOMPARE(span.confidence, SpanConfidence::Low);
        }
    }

    void withNoColumnsNothingIsAligned()
    {
        QVERIFY(alignTranslation(
                    QStringLiteral("t"), QStringLiteral("Matt.1.1"), 3, {})
                    .isEmpty());
        QVERIFY(alignTranslation(
                    QStringLiteral("t"), QStringLiteral("Matt.1.1"), 0, {0, 1})
                    .isEmpty());
    }

    // --- which manuscript a verse is read against ---------------------------

    void thePreferredManuscriptIsUsedWhereItHasTheVerse()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר דוד")),
        };
        QCOMPARE(
            referenceForVerse(
                QStringLiteral("Matt.1.1"), refs(documents), QStringLiteral("b")),
            QStringLiteral("b"));
    }

    void aSilentManuscriptCannotBeTheReference()
    {
        // The point of the rule: reading a verse against a witness that has
        // nothing there would begin the alignment from no words at all, and
        // the consensus would then find nothing to choose.
        QList<SourceDocument> documents = {
            witness(QStringLiteral("absent"), QString::fromUtf8("ספר")),
            witness(QStringLiteral("present"), QString::fromUtf8("ספר")),
        };
        documents[0].verses.clear();
        documents[0].verseIndex.clear();

        QCOMPARE(
            referenceForVerse(
                QStringLiteral("Matt.1.1"), refs(documents), QStringLiteral("absent")),
            QStringLiteral("present"));
    }

    void noManuscriptWithTheVerseMeansNoReference()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר")),
        };
        QVERIFY(referenceForVerse(
                    QStringLiteral("Rev.9.9"), refs(documents), QStringLiteral("a"))
                    .isEmpty());
        QVERIFY(referenceForVerse(QStringLiteral("Matt.1.1"), {}, QStringLiteral("a"))
                    .isEmpty());
    }

    // --- anchoring a note to a word ----------------------------------------

    void aColumnKnowsWhereItsWordBegins()
    {
        CombinedDraft draft;
        for (const QString &word :
             {QString::fromUtf8("ספר"),
              QString::fromUtf8("דוד"),
              QString::fromUtf8("בן")}) {
            ConsensusColumn column;
            column.text = word;
            draft.columns.append(column);
        }

        const QString text = combinedText(draft);
        QCOMPARE(columnCharOffset(draft, 0), 0);
        // Each offset has to land on the word itself, not beside it.
        QCOMPARE(
            text.mid(columnCharOffset(draft, 1), 3), QString::fromUtf8("דוד"));
        QCOMPARE(text.mid(columnCharOffset(draft, 2), 2), QString::fromUtf8("בן"));

        // Past the end is clamped rather than run off the string.
        QCOMPARE(columnCharOffset(draft, 99), int(text.size()));
        QCOMPARE(columnCharOffset(draft, -1), 0);
    }

    void aManualVerseHasNoWordToPointAt()
    {
        CombinedDraft draft;
        ConsensusColumn column;
        column.text = QString::fromUtf8("ספר");
        draft.columns.append(column);
        draft.manualText = QString::fromUtf8("טקסט אחר לגמרי");

        // The columns no longer describe the text, so there is no place in it
        // that belongs to any one of them.
        QCOMPARE(columnCharOffset(draft, 0), 0);
        QCOMPARE(columnCharOffset(draft, 1), 0);
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
