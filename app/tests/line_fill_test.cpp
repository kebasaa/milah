#include "core/line_fill.h"

#include <QtTest>

using namespace milah;

namespace {

/// A Hebrew line: read right to left, so the first word is the rightmost box.
QList<QRect> hebrewLine()
{
    return {
        QRect(700, 100, 90, 40),
        QRect(600, 100, 80, 40),
        QRect(500, 100, 60, 40),
    };
}

int totalWidth(const QList<QRect> &boxes)
{
    int width = 0;
    for (const QRect &box : boxes) {
        width += box.width();
    }
    return width;
}

TranscribedWord at(int line, const QRect &box)
{
    TranscribedWord word;
    word.line = line;
    word.box = box;
    return word;
}

/// Three lines down a folio, each of two words, read right to left.
QList<TranscribedWord> folio()
{
    return {
        at(0, QRect(700, 100, 90, 40)),
        at(0, QRect(600, 100, 80, 40)),
        at(1, QRect(700, 200, 90, 40)),
        at(1, QRect(600, 200, 80, 40)),
        at(2, QRect(700, 300, 90, 40)),
        at(2, QRect(600, 300, 80, 40)),
    };
}

} // namespace

class LineFillTest final : public QObject
{
    Q_OBJECT

private slots:
    /// Which way the line runs is read off the boxes rather than assumed, so a
    /// Latin marginal note on a Hebrew folio is not cut backwards.
    void theDirectionComesFromTheBoxes()
    {
        QCOMPARE(LineFill::directionOf(hebrewLine()), LineFill::Direction::RightToLeft);
        QCOMPARE(
            LineFill::directionOf({QRect(100, 10, 50, 20), QRect(200, 10, 50, 20)}),
            LineFill::Direction::LeftToRight);
        // One box says nothing; the manuscripts this is for read right to left.
        QCOMPARE(
            LineFill::directionOf({QRect(100, 10, 50, 20)}),
            LineFill::Direction::RightToLeft);
        QCOMPARE(LineFill::directionOf({}), LineFill::Direction::RightToLeft);
    }

    /// The ordinary case: the recogniser found what the text has.
    void asManyWordsAsBoxesChangesNothing()
    {
        QCOMPARE(LineFill::place(hebrewLine(), 3), hebrewLine());
    }

    /// The case this exists for. A cursive runs words together, so a line of
    /// five words comes back as three boxes — and the two words with nowhere to
    /// go are two words missing from the ground truth.
    void moreWordsThanBoxesCutsTheBoxesUp()
    {
        const QList<QRect> placed = LineFill::place(hebrewLine(), 5);
        QCOMPARE(placed.size(), 5);

        // Nothing invented and nothing lost: the pieces still cover exactly the
        // ink the recogniser found.
        QCOMPARE(totalWidth(placed), totalWidth(hebrewLine()));
        for (const QRect &box : placed) {
            QCOMPARE(box.top(), 100);
            QCOMPARE(box.height(), 40);
        }

        // Right to left, so the words come out with their left edges falling.
        for (int index = 1; index < placed.size(); ++index) {
            QVERIFY2(
                placed.at(index).left() < placed.at(index - 1).left(),
                "a right-to-left line came out left to right");
        }
    }

    /// A cut box tiles the original exactly — one column's far edge is the next
    /// one's near edge. A seam here becomes a sliver of the neighbouring word in
    /// the strip cut out for training.
    void aCutBoxTilesTheOriginal()
    {
        // One box, three words: the whole of the answer is the cut.
        const QList<QRect> placed = LineFill::place({QRect(500, 100, 100, 40)}, 3);
        QCOMPARE(placed.size(), 3);

        // Read right to left, so placed[2] is the leftmost.
        QCOMPARE(placed.at(2).left(), 500);
        QCOMPARE(placed.at(0).right(), 599);
        for (int index = placed.size() - 1; index > 0; --index) {
            QCOMPARE(placed.at(index).right() + 1, placed.at(index - 1).left());
        }
        QCOMPARE(totalWidth(placed), 100);
    }

    /// A width that does not divide evenly still tiles, with the odd pixels
    /// falling somewhere rather than being dropped.
    void anAwkwardWidthStillTiles()
    {
        const QList<QRect> placed = LineFill::place({QRect(0, 0, 100, 10)}, 7);
        QCOMPARE(placed.size(), 7);
        QCOMPARE(totalWidth(placed), 100);
        for (const QRect &box : placed) {
            QVERIFY(box.width() > 0);
        }
    }

    /// Fewer words than boxes: the recogniser split something, so the spare
    /// piece joins the word before it and no ink belongs to nobody.
    void fewerWordsThanBoxesJoinsThemUp()
    {
        const QList<QRect> placed = LineFill::place(hebrewLine(), 2);
        QCOMPARE(placed.size(), 2);
        // The line still reaches from end to end of what was found.
        QCOMPARE(placed.first().right(), hebrewLine().first().right());
        QCOMPARE(placed.last().left(), hebrewLine().last().left());
    }

    /// Nothing to place, or nowhere to place it: an empty answer rather than a
    /// box at the origin, which would draw a word onto the corner of the folio.
    void nothingToPlaceGivesNothing()
    {
        QVERIFY(LineFill::place(hebrewLine(), 0).isEmpty());
        QVERIFY(LineFill::place(hebrewLine(), -1).isEmpty());
        QVERIFY(LineFill::place({}, 4).isEmpty());
    }

    /// One word, one box, whichever way round.
    void oneWordTakesTheWholeLine()
    {
        QCOMPARE(LineFill::place({QRect(500, 100, 100, 40)}, 1),
                 QList<QRect>{QRect(500, 100, 100, 40)});
        const QList<QRect> joined = LineFill::place(hebrewLine(), 1);
        QCOMPARE(joined.size(), 1);
        QCOMPARE(joined.first(), QRect(500, 100, 290, 40));
    }

    /// Laying a passage down the folio, line by line, from the top.
    void aPassageFillsTheLinesInOrder()
    {
        const QList<LineFill::Laid> laid = LineFill::layOut({3, 4, 3}, 0, 0, 10);
        QCOMPARE(laid.size(), 3);
        QCOMPARE(laid.at(0).line, 0);
        QCOMPARE(laid.at(0).from, 0);
        QCOMPARE(laid.at(0).count, 3);
        QCOMPARE(laid.at(1).from, 3);
        QCOMPARE(laid.at(1).count, 4);
        QCOMPARE(laid.at(2).from, 7);
        QCOMPARE(laid.at(2).count, 3);
    }

    /// The case the whole thing exists for: a book that begins partway down a
    /// leaf, with the end of the book before it above.
    ///
    /// The lines above get nothing at all — not blanks, nothing — because the
    /// caller leaves a line it is not given alone, and that is what keeps the
    /// previous book's tail where it is.
    void aPassageThatStartsPartwayDownLeavesTheTopAlone()
    {
        const QList<LineFill::Laid> laid = LineFill::layOut({3, 3, 3, 3}, 2, 0, 6);
        QCOMPARE(laid.size(), 2);
        QCOMPARE(laid.at(0).line, 2);
        QCOMPARE(laid.at(0).from, 0);
        QCOMPARE(laid.at(1).line, 3);
    }

    /// Carrying a book across a leaf: a folio ends mid-verse far more often
    /// than not, so the next one starts partway into the passage.
    void aFolioResumesWhereTheLastOneStopped()
    {
        const QList<LineFill::Laid> laid = LineFill::layOut({3, 3}, 0, 40, 50);
        QCOMPARE(laid.size(), 2);
        QCOMPARE(laid.at(0).from, 40);
        QCOMPARE(laid.at(1).from, 43);
    }

    /// A passage shorter than the folio stops where it stops and does not begin
    /// again — the bottom of the leaf is somebody else's text or nothing.
    void aShortPassageStopsRatherThanRepeating()
    {
        const QList<LineFill::Laid> laid = LineFill::layOut({3, 3, 3}, 0, 0, 4);
        QCOMPARE(laid.size(), 2);
        QCOMPARE(laid.at(1).count, 1);

        // And a folio already past the end of its passage takes nothing.
        QVERIFY(LineFill::layOut({3, 3}, 0, 50, 50).isEmpty());
        QVERIFY(LineFill::layOut({3, 3}, 0, 0, 0).isEmpty());
    }

    /// A line nudged down to nothing is left out rather than written in empty,
    /// and the line after it carries on from the same place.
    void aLineTakingNoWordsIsLeftOut()
    {
        const QList<LineFill::Laid> laid = LineFill::layOut({2, 0, 2}, 0, 0, 4);
        QCOMPARE(laid.size(), 2);
        QCOMPARE(laid.at(0).line, 0);
        QCOMPARE(laid.at(1).line, 2);
        QCOMPARE(laid.at(1).from, 2);
    }

    /// One word more on a line, then one fewer, is where it was — the whole
    /// reason the assignment is derived from the counts rather than edited in
    /// place.
    void nudgingBackAndForthReturnsToWhereItWas()
    {
        const QList<int> plain{3, 3, 3};
        QList<int> nudged = plain;
        nudged[1] += 1;
        const QList<LineFill::Laid> moved = LineFill::layOut(nudged, 0, 0, 9);
        QCOMPARE(moved.at(2).from, 7);

        nudged[1] -= 1;
        QCOMPARE(nudged, plain);
        const QList<LineFill::Laid> back = LineFill::layOut(nudged, 0, 0, 9);
        QCOMPARE(back.at(2).from, 6);
        QCOMPARE(back.size(), LineFill::layOut(plain, 0, 0, 9).size());
    }

    /// A start line past the end of the folio takes nothing, rather than
    /// wrapping round to the top.
    void aStartLinePastTheEndTakesNothing()
    {
        QVERIFY(LineFill::layOut({3, 3}, 5, 0, 9).isEmpty());
    }

    /// Pointing at a line says *begin here*, and the passage begins at its own
    /// beginning — the two are independent, and it is their pairing that used to
    /// go wrong.
    ///
    /// The dialog silently carried the previous folio's stopping place into
    /// `resumeAt` while also moving book, chapter and verse there, so a click on
    /// line 22 laid down mid-book, mid-verse text. The arithmetic was never the
    /// problem; what fed it was. Now nothing but the visible skip box sets it,
    /// and a skip of nothing means exactly that.
    void aStartLineDoesNotMoveWhereThePassageBegins()
    {
        for (const int start : {0, 2, 3}) {
            const QList<LineFill::Laid> laid = LineFill::layOut({3, 3, 3, 3}, start, 0, 12);
            QVERIFY(!laid.isEmpty());
            QCOMPARE(laid.at(0).line, start);
            QCOMPARE(laid.at(0).from, 0);
        }

        // And a skip does move it, by exactly what it says and nothing else.
        QCOMPARE(LineFill::layOut({3, 3, 3, 3}, 2, 5, 12).at(0).from, 5);
    }

    /// A click on a word is that word's line, which is the whole of the easy
    /// case and most of the real ones.
    void aPointInsideAWordIsThatLine()
    {
        QCOMPARE(lineAtPoint(folio(), QPoint(720, 120)), 0);
        QCOMPARE(lineAtPoint(folio(), QPoint(620, 220)), 1);
        QCOMPARE(lineAtPoint(folio(), QPoint(750, 330)), 2);
    }

    /// The margin at either end of a line is part of pointing at that line — a
    /// transcriber aiming at a line does not aim at a word of it.
    void aPointBesideALineIsThatLine()
    {
        QCOMPARE(lineAtPoint(folio(), QPoint(200, 215)), 1);
        QCOMPARE(lineAtPoint(folio(), QPoint(950, 110)), 0);
    }

    /// Between two lines, the lower one.
    ///
    /// "The transcription starts here" means from here on, and what lies below
    /// the gap is what comes next. Picking the line above would put the whole
    /// passage one line out, and nothing on screen would say so.
    void aPointBetweenTwoLinesIsTheLowerOne()
    {
        QCOMPARE(lineAtPoint(folio(), QPoint(650, 170)), 1);
        QCOMPARE(lineAtPoint(folio(), QPoint(650, 280)), 2);
        // Just under a line still means the next one, not the one just left.
        QCOMPARE(lineAtPoint(folio(), QPoint(650, 141)), 1);
    }

    /// Above everything, the first line; below everything, the last. A click in
    /// the head or foot margin is somebody aiming at the end of the page they
    /// are nearest.
    void aPointOffTheTextBlockTakesTheNearestEnd()
    {
        QCOMPARE(lineAtPoint(folio(), QPoint(650, 10)), 0);
        QCOMPARE(lineAtPoint(folio(), QPoint(650, 900)), 2);
    }

    /// Nothing read off the folio is -1, not 0 — which would silently mean the
    /// first line and start a passage in the wrong place.
    ///
    /// This is the ordinary case now, not an edge one: the right-click is the
    /// only way into the fill, and the folio it is used on most often has never
    /// been read. The dialog asks again once it has read it.
    void aFolioWithNoReadingHasNoLine()
    {
        QCOMPARE(lineAtPoint({}, QPoint(650, 200)), -1);

        // Words somebody typed have no line and no box, and cannot answer.
        TranscribedWord typed;
        typed.hebrew = QStringLiteral("בראשית");
        QCOMPARE(lineAtPoint({typed}, QPoint(650, 200)), -1);
    }

    /// The box under the cursor, or nothing. Beside a line is a line but not a
    /// word — the margin belongs to the line it runs alongside, and to no box.
    void aPointNamesTheBoxItIsInside()
    {
        QCOMPARE(wordAtPoint(folio(), QPoint(720, 120)), QRect(700, 100, 90, 40));
        QCOMPARE(wordAtPoint(folio(), QPoint(620, 220)), QRect(600, 200, 80, 40));

        QVERIFY(wordAtPoint(folio(), QPoint(200, 215)).isNull());
        QVERIFY(wordAtPoint(folio(), QPoint(650, 170)).isNull());
        QVERIFY(wordAtPoint({}, QPoint(720, 120)).isNull());
    }

    /// A marginal box is not a slot for a word of the work.
    ///
    /// Pouring a published transcription into one destroys the note and shifts
    /// every word after it by one for the rest of the leaf, which is worse than
    /// losing the note: the line breaks go wrong, and line breaks are what the
    /// training data is made of.
    void aFillGetsEveryBoxButTheMarginalia()
    {
        TranscribedPage page;
        TranscribedVerse verse;
        verse.words = folio();
        // The second word of line 1 is a note in the margin.
        verse.words[3].marginal = true;
        page.verses.append(verse);

        const QMap<int, QList<QRect>> lines = fillableLines(page);
        QCOMPARE(lines.value(0).size(), 2);
        QCOMPARE(lines.value(1).size(), 1);
        QCOMPARE(lines.value(1).first(), QRect(700, 200, 90, 40));
        QCOMPARE(lines.value(2).size(), 2);
    }

    /// A line that is all marginalia is absent, not empty — it is not a line of
    /// the work at all, and a caller asking for its boxes would otherwise be
    /// handed a line to pour nothing into.
    void aLineOfNothingButMarginaliaIsAbsent()
    {
        TranscribedPage page;
        TranscribedVerse verse;
        verse.words = folio();
        verse.words[2].marginal = true;
        verse.words[3].marginal = true;
        page.verses.append(verse);

        const QMap<int, QList<QRect>> lines = fillableLines(page);
        QVERIFY(!lines.contains(1));
        QCOMPARE(lines.size(), 2);

        // And a word nothing read has no line to belong to.
        QVERIFY(fillableLines(TranscribedPage{}).isEmpty());
    }

    /// The pieces of a word joined back up, because a recogniser's box is a
    /// token of its own reading and not a word of the manuscript.
    ///
    /// Measured on 158r of MS Oo.1.32, whose text is known: 19 boxes for a line
    /// of 13 words, 493 across a leaf holding 236. Taking one box for one word
    /// lays half again too many words on every line.
    void thePiecesOfAWordAreJoinedBackUp()
    {
        // Three words, the first and last of them broken in two by the
        // recogniser. Tight gaps of 4px inside a word, 40px between them.
        const QList<QRect> pieces = {
            QRect(700, 100, 40, 30),   // word one, right half
            QRect(656, 100, 40, 30),   // word one, left half   (gap 4)
            QRect(576, 100, 40, 30),   // word two              (gap 40)
            QRect(496, 100, 40, 30),   // word three, right half (gap 40)
            QRect(452, 100, 40, 30),   // word three, left half  (gap 4)
        };
        const QList<QRect> words = LineFill::mergeWords(pieces);
        QCOMPARE(words.size(), 3);
        // United, so a word's box covers the whole of it.
        QCOMPARE(words.at(0), QRect(656, 100, 84, 30));
        QCOMPARE(words.at(1), QRect(576, 100, 40, 30));
        QCOMPARE(words.at(2), QRect(452, 100, 84, 30));
    }

    /// Left to right too — the direction comes off the boxes, and a Latin
    /// marginal note on a Hebrew folio is read the way it was written.
    void joiningWorksInEitherDirection()
    {
        const QList<QRect> pieces = {
            QRect(100, 100, 40, 30),
            QRect(144, 100, 40, 30),   // gap 4
            QRect(224, 100, 40, 30),   // gap 40
        };
        const QList<QRect> words = LineFill::mergeWords(pieces);
        QCOMPARE(words.size(), 2);
        QCOMPARE(words.at(0), QRect(100, 100, 84, 30));
    }

    /// A line with nothing to join is left exactly as it is, and one box or
    /// none cannot be joined at all.
    void aLineOfWholeWordsIsLeftAlone()
    {
        const QList<QRect> even = {
            QRect(700, 100, 40, 30),
            QRect(620, 100, 40, 30),
            QRect(540, 100, 40, 30),
            QRect(460, 100, 40, 30),
        };
        QCOMPARE(LineFill::mergeWords(even).size(), 4);

        QCOMPARE(LineFill::mergeWords({QRect(700, 100, 40, 30)}).size(), 1);
        QVERIFY(LineFill::mergeWords({}).isEmpty());
    }

    /// **The rail.** Otsu looks for two humps in the gaps; a line whose spacing
    /// is even has none to find, and left to itself the method can pick a
    /// threshold that swallows the whole line. A line may not lose more than
    /// three fifths of its boxes to joining.
    void aLineCannotBeJoinedAwayToNothing()
    {
        QList<QRect> creeping;
        // Gaps widening by one pixel at a time: no split anywhere, which is the
        // shape that makes the method flail.
        int x = 900;
        for (int index = 0; index < 10; ++index) {
            creeping.append(QRect(x, 100, 30, 30));
            x -= 30 + 10 + index;
        }
        const QList<QRect> words = LineFill::mergeWords(creeping);
        QVERIFY2(words.size() >= 4, QByteArray::number(words.size()).constData());
        QVERIFY(words.size() <= 10);
    }

    /// The folio this was written for, with the answer known.
    ///
    /// Kraken's nineteen boxes for the first line of text on 158r of MS Oo.1.32,
    /// taken out of its own ALTO. That line is Jas 1:1 as far as שמחה —
    /// **thirteen words**, with מקודם beginning the line below. Nineteen is what
    /// the fill used to lay onto it.
    ///
    /// Fourteen rather than thirteen: this is a stopgap and is allowed to be a
    /// word out. It is not allowed to be six out, which is what taking a box for
    /// a word did.
    void theRealLineFromTheFolioThisWasWrittenFor()
    {
        const QList<QRect> boxes = {
            QRect(867, 112, 22, 32), QRect(842, 112, 8, 29),  QRect(809, 110, 20, 34),
            QRect(757, 107, 33, 36), QRect(724, 108, 17, 36), QRect(670, 106, 41, 37),
            QRect(626, 112, 27, 26), QRect(601, 109, 8, 30),  QRect(538, 106, 44, 37),
            QRect(503, 106, 24, 37), QRect(492, 108, 2, 32),  QRect(467, 108, 8, 31),
            QRect(434, 106, 17, 35), QRect(407, 106, 16, 36), QRect(377, 111, 13, 32),
            QRect(344, 111, 16, 32), QRect(286, 106, 39, 37), QRect(229, 106, 33, 34),
            QRect(174, 104, 38, 35),
        };
        QCOMPARE(boxes.size(), 19);
        QCOMPARE(LineFill::mergeWords(boxes).size(), 14);
    }

    /// Where a re-flow picks the passage up again.
    ///
    /// Wrong by one here and the folio repeats a word or drops one — which is
    /// exactly what marking a marginal box is supposed to stop, so it would look
    /// like the fix had not worked rather than like a fault of its own.
    void theCountBeforeALineIsWhereARePourStarts()
    {
        TranscribedPage page;
        TranscribedVerse verse;
        verse.words = folio();
        page.verses.append(verse);
        // The leaf opened with the tail of the book before it, and the pour
        // started on line 1.
        page.fillStartLine = 1;

        // Nothing before the line the pour started on.
        QCOMPARE(pouredWordsBefore(page, 1), 0);
        QCOMPARE(pouredWordsBefore(page, 0), 0);
        // Line 1's two words, and line 0's are not the fill's to count.
        QCOMPARE(pouredWordsBefore(page, 2), 2);
        QCOMPARE(pouredWordsBefore(page, 3), 4);
    }

    /// Nothing was ever poured onto a marginal box, so it is not among the words
    /// a re-flow has to count past.
    void marginaliaAreNotCountedBeforeALine()
    {
        TranscribedPage page;
        TranscribedVerse verse;
        verse.words = folio();
        verse.words[3].marginal = true;
        page.verses.append(verse);
        page.fillStartLine = 0;

        QCOMPARE(pouredWordsBefore(page, 2), 3);
        // And a folio nothing has been poured onto counts nothing at all.
        page.fillStartLine = -1;
        QCOMPARE(pouredWordsBefore(page, 2), 0);
    }

    /// The words need not arrive in any order — they come off the document, and
    /// a folio edited since it was read holds them however the editing left them.
    void theWordsNeedNotBeSorted()
    {
        QList<TranscribedWord> shuffled = folio();
        std::reverse(shuffled.begin(), shuffled.end());
        QCOMPARE(lineAtPoint(shuffled, QPoint(650, 170)), 1);
        QCOMPARE(lineAtPoint(shuffled, QPoint(720, 120)), 0);
    }
};

QTEST_MAIN(LineFillTest)
#include "line_fill_test.moc"
