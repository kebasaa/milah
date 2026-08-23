#include "core/page_layout.h"
#include "core/training_export.h"
#include "core/transcription.h"

#include <QtTest>

using namespace milah;

namespace {

TranscribedWord read(const char *hebrew, int line, const QRect &box)
{
    TranscribedWord word;
    word.hebrew = QString::fromUtf8(hebrew);
    word.line = line;
    word.box = box;
    // Checked: a person has been through it. The default for a word a machine
    // read is the opposite, which is the whole point of the flag.
    word.unchecked = false;
    return word;
}

} // namespace

class TrainingExportTest final : public QObject
{
    Q_OBJECT

private:
    /// Two lines off a folio: the first read and checked all the way through,
    /// the second still carrying one word nobody has looked at.
    static TranscribedPage samplePage()
    {
        TranscribedPage page;
        page.imageName = QStringLiteral("150r");

        TranscribedVerse verse;
        verse.words = {
            read("\xd7\x91\xd7\xa8\xd7\x90\xd7\xa9\xd7\x99\xd7\xaa", 0, QRect(700, 100, 90, 40)),
            read("\xd7\x91\xd7\xa8\xd7\x90", 0, QRect(640, 102, 50, 38)),
            read("\xd7\x90\xd7\x9c\xd7\x94\xd7\x99\xd7\x9d", 1, QRect(700, 160, 80, 42)),
            read("\xd7\x90\xd7\xaa", 1, QRect(660, 162, 30, 38)),
        };
        // One word of the second line is the machine's own guess still.
        verse.words[3].unchecked = true;
        page.verses = {verse};
        return page;
    }

private slots:
    /// The whole of the point: what comes out reads back through the very
    /// parser Milah uses on Kraken's own output. Nothing weaker would show that
    /// the file is usable, because a well-formed XML document that Kraken
    /// cannot train on looks exactly like one it can.
    void whatIsWrittenReadsBackAsItWasWritten()
    {
        const TrainingPage truth = trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QVERIFY(!truth.isEmpty());

        QString error;
        const RecognisedPage read = parseRecognisedPage(truth.alto, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read.imageSize, QSize(1024, 1373));
        QCOMPARE(read.words.size(), 2);
        QCOMPARE(read.words.at(0).text, QString::fromUtf8("\xd7\x91\xd7\xa8\xd7\x90\xd7\xa9\xd7\x99\xd7\xaa"));
        QCOMPARE(read.words.at(0).box, QRect(700, 100, 90, 40));
        QCOMPARE(read.words.at(1).box, QRect(640, 102, 50, 38));
        // Both off the one line that qualified.
        QCOMPARE(read.words.at(0).line, 0);
        QCOMPARE(read.words.at(1).line, 0);
    }
    /// **The number a progress bar fills towards**, and the reason it is here
    /// rather than counted off the folio somewhere else.
    ///
    /// The training panel put the count of finished lines over the count of
    /// things the *segmenter* drew, and linesOf() cuts a detection wherever a
    /// transcriber said the manuscript's line ends — so a folio with a break in
    /// it read "35 of 31 lines". Two meaningful numbers that are not the same
    /// quantity. Both now come out of this one call.
    void everyLineToBeReadIsCounted()
    {
        const TrainingPage truth =
            trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize(1024, 1373));
        // Two lines on the folio; one is finished and one still has an unread
        // word on it, so one is written and both are there to be read.
        QCOMPARE(truth.lines, 1);
        QCOMPARE(truth.candidates, 2);
    }

    /// A break makes two lines to read out of one detection, which is exactly
    /// the case the panel got wrong: the finished count already followed the
    /// break and the denominator did not.
    void aBreakAddsALineToRead()
    {
        TranscribedPage page = samplePage();
        // The first line is the finished one; cut it in two.
        page.verses[0].words[0].endsLine = true;

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.candidates, 3);
        // And both halves of the finished line are written, so the finished
        // count moves with the denominator rather than past it.
        QCOMPARE(truth.lines, 2);
        QVERIFY(truth.lines <= truth.candidates);
    }

    /// A folio with nothing finished still says how many lines there are to
    /// read. Answering nought would leave the panel dividing by a denominator
    /// of zero on precisely the folios somebody is about to start work on.
    void aFolioWithNothingFinishedStillCountsItsLines()
    {
        TranscribedPage page = samplePage();
        for (TranscribedWord &word : page.verses[0].words) {
            word.unchecked = true;
        }

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QVERIFY(truth.isEmpty());
        QCOMPARE(truth.lines, 0);
        QCOMPARE(truth.candidates, 2);
    }

    /// And a folio nothing has read has no lines to read either, so the bar has
    /// no denominator rather than a wrong one.
    void aFolioNothingHasReadCountsNothing()
    {
        TranscribedPage page;
        page.imageName = QStringLiteral("150r");
        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.candidates, 0);
        QCOMPARE(truth.lines, 0);
    }


    /// A line holding one unchecked word is the machine's guess, not anybody's
    /// reading, and training on it teaches the model the mistakes it already
    /// makes. So the finished line goes and the half-finished one stays behind.
    void aLineNobodyHasFinishedIsLeftOut()
    {
        const TrainingPage truth = trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 1);
        QCOMPARE(truth.words, 2);
        QVERIFY(!truth.alto.contains(QByteArray("\xd7\x90\xd7\x9c\xd7\x94\xd7\x99\xd7\x9d")));
    }

    /// A word somebody typed has no box, so nothing knows where on the folio it
    /// belongs and its line cannot be cut out of the picture. The line is left
    /// out rather than cut in the wrong place.
    void aLineWithATypedWordIsLeftOut()
    {
        TranscribedPage page = samplePage();
        page.verses[0].words[1].box = QRect();
        QVERIFY(trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373)).isEmpty());
    }

    /// Nothing at all rather than an empty file. An empty file looks like
    /// ground truth, trains on nothing, and says nothing about why.
    void aFolioWithNothingFinishedWritesNothing()
    {
        TranscribedPage page = samplePage();
        for (TranscribedWord &word : page.verses[0].words) {
            word.unchecked = true;
        }
        const TrainingPage truth = trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QVERIFY(truth.isEmpty());
        QCOMPARE(truth.lines, 0);

        // And a page nothing ever read: every word typed, so no word has a line.
        TranscribedPage typed;
        TranscribedVerse verse;
        TranscribedWord word;
        word.hebrew = QString::fromUtf8("\xd7\x91\xd7\xa8\xd7\x90");
        verse.words = {word};
        typed.verses = {verse};
        QVERIFY(trainingAlto(typed, QStringLiteral("150r.jpg"), QSize(1024, 1373)).isEmpty());
    }

    /// The size has to come from the image the boxes were stored against, so a
    /// caller with no image gets nothing rather than a file measured against a
    /// page of no size.
    void withoutAnImageSizeThereIsNothingToMeasureAgainst()
    {
        QVERIFY(trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize()).isEmpty());
    }

    /// Lines come out in the order the document holds the words, and the lines
    /// cut across the verses.
    ///
    /// The document's word order *is* reading order — everything from the grid
    /// to the exports rests on that — so walking it is walking down the page.
    /// This used to sort on the recogniser's line index instead, which cannot
    /// survive a hand-marked break: a break says "the line ends *here*", which
    /// is a statement about a position and not about an index.
    void linesFollowTheDocumentAndCutAcrossTheVerses()
    {
        TranscribedPage page;
        TranscribedVerse first;
        first.words = {
            read("\xd7\x90", 0, QRect(700, 100, 20, 30)),
            read("\xd7\x91", 0, QRect(660, 100, 20, 30)),
        };
        // A verse boundary in the middle of a line: one line, not two.
        TranscribedVerse second;
        second.words = {
            read("\xd7\x92", 0, QRect(620, 100, 20, 30)),
            read("\xd7\x93", 1, QRect(700, 160, 20, 30)),
        };
        page.verses = {first, second};

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 2);
        QCOMPARE(truth.words, 4);

        QString error;
        const RecognisedPage back = parseRecognisedPage(truth.alto, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.words.size(), 4);
        QCOMPARE(back.words.at(0).text, QString::fromUtf8("\xd7\x90"));
        QCOMPARE(back.words.at(3).text, QString::fromUtf8("\xd7\x93"));
        // The first three shared a recognised line whatever the verses said.
        QCOMPARE(back.words.at(2).line, 0);
        QCOMPARE(back.words.at(3).line, 1);
    }

    /// The line's box holds every word on it, and carries the baseline a
    /// baseline model reads — which is every Hebrew model here.
    ///
    /// Without the baseline Kraken skips the line: `logger.info(f'TextLine
    /// {line_id} without baseline'); continue`. One line at a time, all of them,
    /// leaving a training run with an empty set and nothing said about why.
    void aLineCarriesItsBaselineAndItsBox()
    {
        const TrainingPage truth = trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize(1024, 1373));
        // The two words span 640..789 across and 100..139 down, so the level
        // line through their middle is y=120. Asserted as the whole element
        // rather than as separate substrings, which would each also match one of
        // the words' own boxes and prove nothing.
        QVERIFY2(
            truth.alto.contains(QByteArray(
                "<TextLine ID=\"line_0\" BASELINE=\"640 120 789 120\" "
                "HPOS=\"640\" VPOS=\"100\" WIDTH=\"150\" HEIGHT=\"40\"")),
            truth.alto.constData());
        // And the outline Kraken cuts out, which it only logs the absence of.
        QVERIFY2(
            truth.alto.contains(
                QByteArray("<Polygon POINTS=\"640 100 789 100 789 139 640 139\"")),
            truth.alto.constData());
    }

    /// The file names the picture beside it, not the folio's label.
    ///
    /// Kraken resolves it with `base_directory.joinpath(<fileName>)`, so "150r"
    /// names nothing on disk and takes the whole page down with it.
    void theFileNamesThePictureBesideIt()
    {
        const TrainingPage truth = trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QVERIFY2(
            truth.alto.contains(QByteArray("<fileName>150r.jpg</fileName>")),
            truth.alto.constData());
        // And refuses to write a file that names nothing at all.
        QVERIFY(trainingAlto(samplePage(), QString(), QSize(1024, 1373)).isEmpty());
    }

    /// A break marked by hand splits a recognised line in two.
    ///
    /// The recogniser's idea of a line is often wrong on a hand it was not
    /// trained for — it runs two together as readily as it splits one — so this
    /// is how a person says otherwise, and it is the only thing that can.
    void aMarkedBreakSplitsALine()
    {
        TranscribedPage page = samplePage();
        // Make the second line finished too, so both lines qualify.
        page.verses[0].words[3].unchecked = false;
        // …and say the first line really ends after its first word.
        page.verses[0].words[0].endsLine = true;

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 3);
        QCOMPARE(truth.words, 4);

        QString error;
        const RecognisedPage back = parseRecognisedPage(truth.alto, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // The split line's halves are two lines, each boxed round its own word.
        QCOMPARE(back.words.at(0).line, 0);
        QCOMPARE(back.words.at(1).line, 1);
        QVERIFY2(
            truth.alto.contains(QByteArray(
                "<TextLine ID=\"line_0\" BASELINE=\"700 120 789 120\" "
                "HPOS=\"700\" VPOS=\"100\" WIDTH=\"90\" HEIGHT=\"40\"")),
            truth.alto.constData());
    }

    /// A break on a line's last word says what was already true, so it changes
    /// nothing — no empty line, no shifted numbering.
    void aBreakOnTheLastWordOfALineChangesNothing()
    {
        TranscribedPage plain = samplePage();
        TranscribedPage marked = samplePage();
        marked.verses[0].words[1].endsLine = true;   // already the end of line 0

        QCOMPARE(
            trainingAlto(marked, QStringLiteral("150r.jpg"), QSize(1024, 1373)).alto,
            trainingAlto(plain, QStringLiteral("150r.jpg"), QSize(1024, 1373)).alto);
    }

    /// Spaces between the words and nowhere else.
    ///
    /// Kraken builds a line's text by walking its String and SP elements and
    /// taking CONTENT or a space. Without the SPs the line is one long token,
    /// silently — the file parses, trains, and teaches nonsense.
    void theWordsAreSeparated()
    {
        const TrainingPage truth = trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize(1024, 1373));
        // Two words, so exactly one separator, and it falls between them.
        QCOMPARE(truth.alto.count(QByteArray("<SP/>")), 1);
        QVERIFY2(
            !truth.alto.contains(QByteArray("<TextLine")) || !truth.alto.contains(
                QByteArray("\"><SP/>")),
            truth.alto.constData());
    }

    /// A marginal note somebody has read is an ordinary word of its line.
    ///
    /// Its ink is in the picture the line is cut from, so the truth has to
    /// account for it. A model taught to ignore letters it can plainly see is a
    /// worse model than one taught to read them.
    void aMarginalNoteSomebodyHasReadIsGroundTruth()
    {
        TranscribedPage page = samplePage();
        page.verses[0].words[1].marginal = true;
        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));

        QCOMPARE(truth.lines, 1);
        QCOMPARE(truth.words, 2);
        QVERIFY(truth.alto.contains(QByteArray("\xd7\x91\xd7\xa8\xd7\x90\"")));
        // And the line still spans it: 640 to 789 across, not 700 to 789.
        QVERIFY2(truth.alto.contains(QByteArray("HPOS=\"640\"")), truth.alto.constData());
    }

    /// A marginal note nobody has read yet, at the end of a line, is trimmed
    /// away — text, box, baseline and polygon together.
    ///
    /// Kraken masks everything outside the polygon to zero before it shows the
    /// line to the model, so a polygon drawn round what is left really does keep
    /// that ink out. The line survives instead of being thrown away whole.
    void anUnreadMarginalNoteAtTheEndTrimsTheLine()
    {
        TranscribedPage page = samplePage();
        // The leftmost word of the first line is an unread note in the margin.
        page.verses[0].words[1].marginal = true;
        page.verses[0].words[1].unchecked = true;
        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));

        QCOMPARE(truth.lines, 1);
        QCOMPARE(truth.words, 1);
        // The note's reading is gone from the file, and so is its extent: the
        // line now starts at 700 rather than reaching out to 640.
        QVERIFY(!truth.alto.contains(QByteArray("\xd7\x91\xd7\xa8\xd7\x90\"")));
        QVERIFY2(!truth.alto.contains(QByteArray("HPOS=\"640\"")), truth.alto.constData());
        QVERIFY2(truth.alto.contains(QByteArray("HPOS=\"700\"")), truth.alto.constData());
        QVERIFY2(truth.alto.contains(QByteArray("BASELINE=\"700 ")), truth.alto.constData());
    }

    /// One in the middle of a line cannot be trimmed, so the line goes.
    ///
    /// There is no rectangle round the rest of the line that leaves it out. The
    /// polygon could be notched, but the extraction dewarps along the baseline
    /// and would hand the model a line with a hole in it — stranger to learn
    /// than nothing.
    void anUnreadMarginalNoteInTheMiddleDropsTheLine()
    {
        TranscribedPage page;
        TranscribedVerse verse;
        verse.words = {
            read("\xd7\x90", 0, QRect(700, 100, 40, 40)),
            read("\xd7\x91", 0, QRect(640, 100, 40, 40)),
            read("\xd7\x92", 0, QRect(580, 100, 40, 40)),
        };
        verse.words[1].marginal = true;
        verse.words[1].unchecked = true;
        page.verses = {verse};

        QCOMPARE(trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373)).lines, 0);
    }

    /// A line that is nothing but unread margin is absent, not empty. Kraken
    /// skips an empty line anyway; writing one is a claim we have not got.
    void aLineOfNothingButUnreadMarginIsAbsent()
    {
        TranscribedPage page = samplePage();
        for (TranscribedWord &word : page.verses[0].words) {
            if (word.line == 0) {
                word.marginal = true;
                word.unchecked = true;
            }
        }
        QCOMPARE(trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373)).lines, 0);
    }

    /// Every rectangle inside the picture, and lines too thin to teach left out.
    ///
    /// Kraken tests a polygon with `>=` against the width and height, so a box
    /// touching the last pixel column is out of bounds — and it then drops that
    /// line with a log warning rather than an error. Fifty lines saved would
    /// compile as forty-nine and nothing would say so.
    void nothingIsWrittenOutsideThePicture()
    {
        // A box running to the edge of the picture it was read from — which an
        // ALTO imported from elsewhere quite happily contains — carried up to
        // the master the ground truth is cut from. 1015 + 12 is 1027 across a
        // picture 1024 wide, and x3948/1024 that is well past the master's edge.
        TranscribedPage page;
        TranscribedVerse verse;
        verse.words = {read("\xd7\x90\xd7\x91", 0, QRect(1015, 40, 12, 20))};
        page.verses = {verse};

        const TrainingPage truth = trainingAlto(
            page, QStringLiteral("150r.jpg"), QSize(3948, 5295), QSize(1024, 1373));
        QCOMPARE(truth.lines, 1);
        // The last column of the picture, and not one pixel further: kraken's
        // test is `>=`, so 3948 is already out of bounds.
        QVERIFY2(truth.alto.contains(QByteArray(" 3947 ")), truth.alto.constData());
        QVERIFY2(!truth.alto.contains(QByteArray(" 3958 ")), truth.alto.constData());
        QVERIFY2(!truth.alto.contains(QByteArray("BASELINE=\"3913 50 3958")),
                 truth.alto.constData());

        // And a line trimmed down to a sliver is not written at all: kraken's
        // own floor is a 5px baseline, below which it drops the line silently.
        TranscribedPage thin;
        TranscribedVerse sliver;
        sliver.words = {read("\xd7\x90", 0, QRect(700, 100, 3, 40))};
        thin.verses = {sliver};
        QCOMPARE(trainingAlto(thin, QStringLiteral("150r.jpg"), QSize(1024, 1373)).lines, 0);
    }

    /// Kraken's own baseline and boundary are what get written, where the folio
    /// has them.
    ///
    /// It dewarps a training strip along the baseline and masks it to the
    /// boundary, so these two decide what the model is shown. Milah's level
    /// line and rectangle are the fallback now, not the rule.
    void theSegmentersOwnGeometryIsWhatIsWritten()
    {
        TranscribedPage page = samplePage();
        TranscribedLine drawn;
        drawn.index = 0;
        drawn.baseline = {QPoint(640, 130), QPoint(720, 126), QPoint(790, 121)};
        drawn.boundary = {QPoint(636, 96), QPoint(700, 99), QPoint(792, 94),
                          QPoint(790, 142), QPoint(636, 140)};
        page.lines = {drawn};

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 1);
        // Sloping and many-pointed, not the level two-point invention.
        QVERIFY2(truth.alto.contains(QByteArray("BASELINE=\"640 130 720 126 790 121\"")),
                 truth.alto.constData());
        QVERIFY2(truth.alto.contains(QByteArray("636 96 700 99 792 94")),
                 truth.alto.constData());
    }

    /// A folio read before Milah kept them still writes something a baseline
    /// model will take, because a line without one is skipped in silence.
    void aFolioWithoutItFallsBackToTheInvention()
    {
        const TrainingPage truth =
            trainingAlto(samplePage(), QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 1);
        QVERIFY2(truth.alto.contains(QByteArray("BASELINE=")), truth.alto.constData());
        // Level: both y values the same, which is what the invention produces.
        QVERIFY2(truth.alto.contains(QByteArray("BASELINE=\"640 120 789 120\"")),
                 truth.alto.constData());
    }

    /// **The fault a line box exists to make visible.** The segmenter ran two
    /// lines of the manuscript together, and the transcriber said so with a
    /// break. Both halves used to be handed the whole detection's polygon and
    /// baseline — identical, and covering two lines of ink — so the split fixed
    /// the *text* of the two strips and left both pictures wrong, which is the
    /// half that actually teaches the model.
    void splittingADetectionGivesEachHalfItsOwnShape()
    {
        TranscribedPage page;
        page.imageName = QStringLiteral("150r");

        TranscribedVerse verse;
        verse.words = {
            // The upper manuscript line…
            read("\xd7\x91\xd7\xa8\xd7\x90\xd7\xa9\xd7\x99\xd7\xaa", 0, QRect(700, 100, 90, 40)),
            read("\xd7\x91\xd7\xa8\xd7\x90", 0, QRect(640, 102, 50, 38)),
            // …and the lower one, which the segmenter put on the same line.
            read("\xd7\x90\xd7\x9c\xd7\x94\xd7\x99\xd7\x9d", 0, QRect(700, 160, 80, 42)),
            read("\xd7\x90\xd7\xaa", 0, QRect(660, 162, 30, 38)),
        };
        verse.words[1].endsLine = true;
        page.verses = {verse};

        TranscribedLine drawn;
        drawn.index = 0;
        drawn.baseline = {QPoint(636, 130), QPoint(792, 195)};
        drawn.boundary = {QPoint(636, 90), QPoint(792, 90), QPoint(792, 210), QPoint(636, 210)};
        page.lines = {drawn};

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 2);
        QCOMPARE(truth.words, 4);

        // Each half clipped to its own words, grown by half a line so the
        // ascenders the segmenter deliberately reached for are still inside.
        QVERIFY2(truth.alto.contains(QByteArray("POINTS=\"640 90 789 90 789 159 640 159\"")),
                 truth.alto.constData());
        QVERIFY2(truth.alto.contains(QByteArray("POINTS=\"660 139 779 139 779 210 660 210\"")),
                 truth.alto.constData());
    }

    /// A line that is the whole of its detection keeps the shape whole. The
    /// segmenter's outline reaches past the word boxes on purpose, round the
    /// ascenders, and squeezing every line to its words' band would shave the
    /// very ink the strip is cut for.
    void anUnsplitLineIsNotClippedAtAll()
    {
        TranscribedPage page = samplePage();
        // Only the first line is exported — the second still has an unchecked
        // word on it — so give the first a shape that reaches well past it.
        TranscribedLine drawn;
        drawn.index = 0;
        drawn.baseline = {QPoint(600, 130), QPoint(800, 126)};
        drawn.boundary = {QPoint(600, 60), QPoint(820, 60), QPoint(820, 180)};
        page.lines = {drawn};

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 1);
        QVERIFY2(truth.alto.contains(QByteArray("POINTS=\"600 60 820 60 820 180\"")),
                 truth.alto.constData());
        QVERIFY2(truth.alto.contains(QByteArray("BASELINE=\"600 130 800 126\"")),
                 truth.alto.constData());
    }

    /// Trimming an unread marginal note off the end clips the real shape rather
    /// than squaring the line off — it still follows the ink, it merely stops
    /// short of the note.
    void trimmingClipsTheShapeRatherThanSquaringIt()
    {
        TranscribedPage page = samplePage();
        page.verses[0].words[1].marginal = true;
        page.verses[0].words[1].unchecked = true;
        TranscribedLine drawn;
        drawn.index = 0;
        // Reaches out to 636, where the marginal word is; the kept word starts
        // at 700.
        drawn.baseline = {QPoint(636, 130), QPoint(720, 126), QPoint(790, 121)};
        drawn.boundary = {QPoint(636, 96), QPoint(792, 94), QPoint(790, 142)};
        page.lines = {drawn};

        const TrainingPage truth =
            trainingAlto(page, QStringLiteral("150r.jpg"), QSize(1024, 1373));
        QCOMPARE(truth.lines, 1);
        QCOMPARE(truth.words, 1);
        // Clipped to the kept word's span, and still sloping.
        QVERIFY2(!truth.alto.contains(QByteArray("636")), truth.alto.constData());
        QVERIFY2(truth.alto.contains(QByteArray("BASELINE=\"700 130 720 126 789 121\"")),
                 truth.alto.constData());
    }
};

QTEST_MAIN(TrainingExportTest)
#include "training_export_test.moc"
