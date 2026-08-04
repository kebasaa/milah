#include "core/transcription_docx.h"

#include <QtTest>

using namespace milah;

namespace {

TranscribedWord word(
    const QString &hebrew, const QString &english = QString(), const QString &note = QString())
{
    TranscribedWord made;
    made.hebrew = hebrew;
    made.english = english;
    made.note = note;
    return made;
}

TranscribedVerse verse(const QString &number, const QList<TranscribedWord> &words)
{
    TranscribedVerse made;
    made.number = number;
    made.words = words;
    return made;
}

/// Every run of every paragraph, joined — what a reader would see, without the
/// markup around it.
QString textOf(const DocxDocument &document)
{
    QString out;
    for (const DocxParagraph &paragraph : document.paragraphs) {
        for (const DocxRun &run : paragraph.runs) {
            out += run.text;
        }
        out += QLatin1Char('\n');
    }
    return out;
}

int paragraphsInStyle(const DocxDocument &document, const QString &style)
{
    int count = 0;
    for (const DocxParagraph &paragraph : document.paragraphs) {
        if (paragraph.style == style) {
            ++count;
        }
    }
    return count;
}

/// A folio of Revelation with two verses, the second opening a chapter.
TranscriptionDocument twoChapters()
{
    TranscribedPage page;
    page.book = QStringLiteral("Rev");
    page.firstChapter = 4;
    page.verses.append(
        verse(QStringLiteral("1"), {word(QStringLiteral("אחר"), QStringLiteral("after")),
                                    word(QStringLiteral("זאת"), QStringLiteral("this"))}));

    TranscribedVerse opening =
        verse(QStringLiteral("1"), {word(QStringLiteral("ואראה"), QStringLiteral("and I saw"))});
    opening.startsNewChapter = true;
    page.verses.append(opening);

    TranscriptionDocument transcription;
    transcription.metadata.manuscriptName = QStringLiteral("Sloane MS 237");
    transcription.pages.append(page);
    return transcription;
}

} // namespace

/// The two documents a transcriber is handed. Everything here is about what a
/// reader sees on the page: which words stand under which, what a verse nobody
/// has numbered is called, and where a remark lands.
class TranscriptionDocxTest final : public QObject
{
    Q_OBJECT

private slots:
    void theGlossesRunInTheOrderTheirWordsDo()
    {
        // The whole of what makes it an interlinear: the nth word below belongs
        // to the nth word above. Get this backwards and the document is worse
        // than useless — it is confidently wrong.
        TranscribedPage page;
        page.book = QStringLiteral("Gen");
        page.firstChapter = 1;
        page.verses.append(verse(
            QStringLiteral("1"),
            {word(QStringLiteral("בראשית"), QStringLiteral("in the beginning")),
             word(QStringLiteral("ברא"), QStringLiteral("created")),
             word(QStringLiteral("אלהים"), QStringLiteral("God"))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        const DocxDocument made = interlinearWordDocument(transcription);
        const QString text = textOf(made);
        const int first = text.indexOf(QStringLiteral("in the beginning"));
        const int second = text.indexOf(QStringLiteral("created"));
        const int third = text.indexOf(QStringLiteral("God"));
        QVERIFY(first >= 0 && second > first && third > second);
    }

    void aWordWithNoGlossKeepsItsPlace()
    {
        // Otherwise the two lines stop corresponding at the first word nobody
        // answered, and every gloss after it names the wrong Hebrew.
        TranscribedPage page;
        page.book = QStringLiteral("Gen");
        page.verses.append(verse(
            QStringLiteral("1"),
            {word(QStringLiteral("בראשית"), QStringLiteral("in the beginning")),
             word(QStringLiteral("ברא")),
             word(QStringLiteral("אלהים"), QStringLiteral("God"))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        const QString text = textOf(interlinearWordDocument(transcription));
        QVERIFY(text.contains(QStringLiteral("—")));
        QVERIFY(
            text.indexOf(QStringLiteral("—")) < text.indexOf(QStringLiteral("God")));
    }

    void aChapterBreakOpensOneHeading()
    {
        const DocxDocument reading = readingWordDocument(twoChapters());
        QCOMPARE(paragraphsInStyle(reading, QStringLiteral("Heading1")), 2);
        const QString text = textOf(reading);
        QVERIFY(text.contains(QStringLiteral("Revelation 4")));
        QVERIFY(text.contains(QStringLiteral("Revelation 5")));
    }

    void theReadingTextRunsTheVersesTogetherWithinAChapter()
    {
        // What makes it a text to read rather than a list: a chapter is one
        // paragraph, its verses running on inside it.
        TranscribedPage page;
        page.book = QStringLiteral("Rev");
        page.firstChapter = 1;
        page.verses.append(
            verse(QStringLiteral("1"), {word(QStringLiteral("אחד"))}));
        page.verses.append(
            verse(QStringLiteral("2"), {word(QStringLiteral("שנים"))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        const DocxDocument reading = readingWordDocument(transcription);
        QCOMPARE(paragraphsInStyle(reading, QStringLiteral("VerseHebrew")), 1);
        // And the numbers stand in the flow of it, raised.
        bool raised = false;
        for (const DocxParagraph &paragraph : reading.paragraphs) {
            for (const DocxRun &run : paragraph.runs) {
                if (run.superscript && run.text.trimmed() == QStringLiteral("2")) {
                    raised = true;
                }
            }
        }
        QVERIFY(raised);
    }

    void aVerseNobodyHasNumberedIsStillPrinted()
    {
        // Unlike the OSIS export, which cannot address it and refuses it. A
        // page of Hebrew half-identified is exactly what a transcriber wants to
        // read back, so it is printed under whatever verseHeading can say.
        TranscribedPage page;
        page.verses.append(verse(QString(), {word(QStringLiteral("מלה"))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        const DocxDocument made = interlinearWordDocument(transcription);
        QVERIFY(textOf(made).contains(QStringLiteral("מלה")));
        QCOMPARE(unnamedVerseCount(transcription), 1);
        // And the reading document prints it too.
        QVERIFY(textOf(readingWordDocument(transcription)).contains(QStringLiteral("מלה")));
    }

    void anIdentifiedVerseIsNotCountedAsUnnamed()
    {
        QCOMPARE(unnamedVerseCount(twoChapters()), 0);
    }

    void theEmptyVerseAFolioAlwaysCarriesIsNotPrinted()
    {
        // Every folio holds one empty verse with one empty word, because there
        // has to be somewhere to type. Printing it would be a page of blanks.
        TranscribedPage page;
        page.book = QStringLiteral("Rev");
        page.verses.append(verse(QString(), {word(QString())}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        QVERIFY(readingWordDocument(transcription).paragraphs.isEmpty());
        QVERIFY(interlinearWordDocument(transcription).paragraphs.isEmpty());
        QCOMPARE(unnamedVerseCount(transcription), 0);
    }

    void aNoteLandsAfterItsOwnWord()
    {
        // The defect this exists to catch: notes anchored at the start of the
        // verse instead of at the word they were written on. Both markers must
        // fall after their own word and before the next one.
        TranscribedPage page;
        page.book = QStringLiteral("Rev");
        page.verses.append(verse(
            QStringLiteral("1"),
            {word(QStringLiteral("aleph")),
             word(QStringLiteral("beth")),
             word(QStringLiteral("gimel"), QString(), QStringLiteral("Scratched out."))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        const DocxDocument reading = readingWordDocument(transcription);
        QCOMPARE(reading.footnotes.size(), 1);
        QCOMPARE(reading.footnotes.constFirst().text, QStringLiteral("Scratched out."));

        // The marker is the last thing in the paragraph, because the word it
        // belongs to is the last word of the verse.
        const DocxParagraph &text = reading.paragraphs.constLast();
        QVERIFY(text.runs.constLast().footnoteId >= FirstFootnoteId);
        QVERIFY(text.runs.constLast().text.isEmpty());
        // And the words before it are in the run just ahead of the marker.
        QVERIFY(text.runs.at(text.runs.size() - 2).text.endsWith(QStringLiteral("gimel")));
    }

    void twoNotesInOneVerseAreTwoFootnotes()
    {
        TranscribedPage page;
        page.book = QStringLiteral("Rev");
        page.verses.append(verse(
            QStringLiteral("1"),
            {word(QStringLiteral("aleph"), QString(), QStringLiteral("First.")),
             word(QStringLiteral("beth")),
             word(QStringLiteral("gimel"), QString(), QStringLiteral("Second."))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        const DocxDocument reading = readingWordDocument(transcription);
        QCOMPARE(reading.footnotes.size(), 2);
        QCOMPARE(reading.footnotes.at(0).id, FirstFootnoteId);
        QCOMPARE(reading.footnotes.at(1).id, FirstFootnoteId + 1);
        // The interlinear keeps them too — dropping them there would lose the
        // transcriber's work in one of the two documents they were given.
        QCOMPARE(interlinearWordDocument(transcription).footnotes.size(), 2);
    }

    void aTranscriptionWithNoGlossesIsStillReadable()
    {
        TranscribedPage page;
        page.book = QStringLiteral("Rev");
        page.verses.append(verse(QStringLiteral("1"), {word(QStringLiteral("מלה"))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);

        const DocxDocument made = interlinearWordDocument(transcription);
        // A heading, the Hebrew, and no gloss line at all — rather than an
        // empty paragraph under every verse.
        QCOMPARE(paragraphsInStyle(made, QStringLiteral("Gloss")), 0);
        QCOMPARE(paragraphsInStyle(made, QStringLiteral("VerseHebrew")), 1);
    }

    void theManuscriptNamesItself()
    {
        TranscriptionDocument transcription = twoChapters();
        transcription.metadata.shelfmark = QStringLiteral("British Library, Sloane MS 237");
        transcription.metadata.transcriber = QStringLiteral("J. Müller");
        transcription.metadata.date = QStringLiteral("1487");

        const DocxDocument reading = readingWordDocument(transcription);
        QCOMPARE(reading.title, QStringLiteral("Sloane MS 237"));
        QVERIFY(reading.subtitle.join(QLatin1Char('\n'))
                    .contains(QStringLiteral("British Library, Sloane MS 237")));
        QVERIFY(reading.subtitle.join(QLatin1Char('\n')).contains(QStringLiteral("1487")));
        QVERIFY(reading.subtitle.join(QLatin1Char('\n')).contains(QStringLiteral("J. Müller")));
    }

    void anUnnamedManuscriptStillGetsATitle()
    {
        TranscriptionDocument transcription = twoChapters();
        transcription.metadata.manuscriptName.clear();
        QVERIFY(!readingWordDocument(transcription).title.isEmpty());
        QVERIFY(readingWordDocument(transcription).subtitle.isEmpty());
    }

    void aWorkOutsideTheCanonIsHeadedByWhatTheTranscriberCalledIt()
    {
        TranscribedPage page;
        page.book = QStringLiteral("Tob");
        page.bookLabel = QStringLiteral("Tobit");
        page.firstChapter = 2;
        page.verses.append(verse(QStringLiteral("1"), {word(QStringLiteral("מלה"))}));

        TranscriptionDocument transcription;
        transcription.pages.append(page);
        QVERIFY(textOf(readingWordDocument(transcription))
                    .contains(QStringLiteral("Tobit 2")));
    }
};

QTEST_MAIN(TranscriptionDocxTest)
#include "transcription_docx_test.moc"
