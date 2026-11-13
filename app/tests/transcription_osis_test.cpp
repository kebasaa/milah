#include "core/transcription_osis.h"

#include <QtTest>

using namespace milah;

namespace {

TranscribedWord word(const QString &hebrew, const QString &english = QString(),
                     const QString &note = QString())
{
    TranscribedWord result;
    result.hebrew = hebrew;
    result.english = english;
    result.note = note;
    return result;
}

TranscribedPage page(const QString &number, const QList<TranscribedWord> &words)
{
    TranscribedPage result;
    result.book = QStringLiteral("Jas");
    result.firstChapter = 1;
    TranscribedVerse verse;
    verse.number = number;
    verse.words = words;
    result.verses.append(verse);
    return result;
}

QStringList textOf(const CombinedDraft &draft)
{
    QStringList words;
    for (const ConsensusColumn &column : draft.columns) {
        words.append(column.text.value_or(QString()));
    }
    return words;
}

} // namespace

/// The transcription's OSIS export: one verse per id, whatever folios it
/// spans. A folio ends in the middle of a verse far more often than not.
class TranscriptionOsisTest final : public QObject
{
    Q_OBJECT

private slots:
    void aVerseOnOneFolioIsOneDraft()
    {
        TranscriptionDocument document;
        document.pages.append(page(QStringLiteral("2"),
                                   {word(QStringLiteral("א")), word(QStringLiteral("ב"))}));

        const TranscriptionDrafts out = transcriptionDrafts(document);
        QCOMPARE(out.drafts.size(), 1);
        const CombinedDraft draft = out.drafts.value(QStringLiteral("Jas.1.2"));
        QCOMPARE(draft.reference.book, QStringLiteral("Jas"));
        QCOMPARE(draft.reference.chapter, 1);
        QCOMPARE(draft.reference.verse, QStringLiteral("2"));
        QCOMPARE(textOf(draft), QStringList({QStringLiteral("א"), QStringLiteral("ב")}));
    }

    void aVerseSpanningTwoFoliosKeepsBothHalves()
    {
        // The bug this pins: the second folio's half used to replace the first.
        TranscriptionDocument document;
        document.pages.append(page(QStringLiteral("2"),
                                   {word(QStringLiteral("א")), word(QStringLiteral("ב"))}));
        document.pages.append(page(QStringLiteral("2"),
                                   {word(QStringLiteral("ג")), word(QStringLiteral("ד"))}));

        const TranscriptionDrafts out = transcriptionDrafts(document);
        QCOMPARE(out.drafts.size(), 1);
        QCOMPARE(textOf(out.drafts.value(QStringLiteral("Jas.1.2"))),
                 QStringList({QStringLiteral("א"), QStringLiteral("ב"),
                              QStringLiteral("ג"), QStringLiteral("ד")}));
    }

    void glossesAndNotesAreNumberedAcrossTheFolioBreak()
    {
        TranscriptionDocument document;
        document.pages.append(page(QStringLiteral("2"),
                                   {word(QStringLiteral("א"), QStringLiteral("one"),
                                         QStringLiteral("first note"))}));
        document.pages.append(page(QStringLiteral("2"),
                                   {word(QStringLiteral("ב")),
                                    word(QStringLiteral("ג"), QStringLiteral("three"),
                                         QStringLiteral("second note"))}));

        const TranscriptionDrafts out = transcriptionDrafts(document);
        const QString id = QStringLiteral("Jas.1.2");

        // Column 2 is the second folio's second word, not its own column 1.
        const QMap<int, QString> glosses = out.glosses.value(id);
        QCOMPARE(glosses.value(0), QStringLiteral("one"));
        QCOMPARE(glosses.value(2), QStringLiteral("three"));
        QVERIFY(!glosses.contains(1));

        const QList<SourceNote> notes = out.apparatus.notes.value(id);
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes.at(0).tokenIndex, 0);
        QCOMPARE(notes.at(0).number, QStringLiteral("1"));
        QCOMPARE(notes.at(1).text, QStringLiteral("second note"));
        QCOMPARE(notes.at(1).tokenIndex, 2);
        QCOMPARE(notes.at(1).number, QStringLiteral("2"));
        // Two words and their separating spaces stand before it: "א ב ".
        QCOMPARE(notes.at(1).charOffset, 4);
    }

    void eachFolioBeginsWithAPageBreak()
    {
        TranscriptionDocument document;
        TranscribedPage first = page(QStringLiteral("2"), {word(QStringLiteral("א")),
                                                           word(QStringLiteral("ב"))});
        first.imageLabel = QStringLiteral("1r");
        TranscribedPage second = page(QStringLiteral("2"), {word(QStringLiteral("ג"))});
        second.imageLabel = QStringLiteral("1v");
        document.pages = {first, second};

        const TranscriptionDrafts out = transcriptionDrafts(document);
        const QList<SourceMilestone> breaks = out.apparatus.milestones;
        QCOMPARE(breaks.size(), 2);
        QCOMPARE(breaks.at(0).type, QStringLiteral("pb"));
        QCOMPARE(breaks.at(0).n, QStringLiteral("1r"));
        QCOMPARE(breaks.at(0).charOffset, 0);
        // Before the second folio's first word, past the space that joins it
        // to the first folio's "א ב".
        QCOMPARE(breaks.at(1).n, QStringLiteral("1v"));
        QCOMPARE(breaks.at(1).verseId, std::optional<QString>(QStringLiteral("Jas.1.2")));
        QCOMPARE(breaks.at(1).charOffset, 4);
    }

    void aVerseWithNoNumberIsCountedNotExported()
    {
        TranscriptionDocument document;
        document.pages.append(page(QString(), {word(QStringLiteral("א"), {},
                                                    QStringLiteral("stranded"))}));

        const TranscriptionDrafts out = transcriptionDrafts(document);
        QVERIFY(out.drafts.isEmpty());
        QCOMPARE(out.unnamed, 1);
        QCOMPARE(out.strandedNotes, 1);
    }
};

QTEST_MAIN(TranscriptionOsisTest)
#include "transcription_osis_test.moc"
