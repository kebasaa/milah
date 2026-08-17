#include "core/line_fill.h"

#include <algorithm>

namespace milah {

int lineAtPoint(const QList<TranscribedWord> &words, const QPoint &folioPixel)
{
    int best = -1;
    int bestTop = 0;
    int nearestBelow = -1;
    int nearestBelowTop = 0;

    for (const TranscribedWord &word : words) {
        if (word.line < 0 || word.box.isNull()) {
            continue;
        }
        // Inside a word: that is the line, and nothing else can beat it.
        if (word.box.contains(folioPixel)) {
            return word.line;
        }
        // Beside one — the margin at either end of a line is part of pointing
        // at that line.
        if (folioPixel.y() >= word.box.top() && folioPixel.y() <= word.box.bottom()) {
            if (best < 0 || word.box.top() < bestTop) {
                best = word.line;
                bestTop = word.box.top();
            }
            continue;
        }
        // Between two lines: the one below. "Start here" means from here on,
        // and what is below the gap is what comes next.
        if (word.box.top() > folioPixel.y()
            && (nearestBelow < 0 || word.box.top() < nearestBelowTop)) {
            nearestBelow = word.line;
            nearestBelowTop = word.box.top();
        }
    }

    if (best >= 0) {
        return best;
    }
    if (nearestBelow >= 0) {
        return nearestBelow;
    }
    // Below everything: the last line there is, so a click under the text block
    // means the end rather than nothing.
    int last = -1;
    int lastTop = 0;
    for (const TranscribedWord &word : words) {
        if (word.line >= 0 && !word.box.isNull() && (last < 0 || word.box.top() > lastTop)) {
            last = word.line;
            lastTop = word.box.top();
        }
    }
    return last;
}

QRect wordAtPoint(const QList<TranscribedWord> &words, const QPoint &folioPixel)
{
    for (const TranscribedWord &word : words) {
        if (!word.box.isNull() && word.box.contains(folioPixel)) {
            return word.box;
        }
    }
    return QRect();
}

QMap<int, QList<QRect>> fillableLines(const TranscribedPage &page)
{
    QMap<int, QList<QRect>> lines;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line < 0 || word.box.isNull() || word.marginal) {
                continue;
            }
            lines[word.line].append(word.box);
        }
    }
    return lines;
}

int pouredWordsBefore(const TranscribedPage &page, int line)
{
    if (page.fillStartLine < 0 || line <= page.fillStartLine) {
        return 0;
    }
    int words = 0;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.marginal || word.box.isNull()) {
                continue;
            }
            if (word.line >= page.fillStartLine && word.line < line) {
                ++words;
            }
        }
    }
    return words;
}

namespace LineFill {
namespace {

/// A box cut into `pieces` columns across its width, in the line's reading
/// order, tiling the original exactly.
///
/// Exactly, because the alternative is a seam. Rounding each column's width
/// independently loses or doubles a pixel at every join, and a strip cut out of
/// the picture along a seam carries a sliver of the next word into the training
/// data. Every edge here is computed from the whole width, so one column's far
/// edge *is* the next one's near edge rather than merely agreeing with it.
QList<QRect> columns(const QRect &box, int pieces, Direction direction)
{
    QList<QRect> out;
    out.reserve(pieces);
    for (int index = 0; index < pieces; ++index) {
        const int from = box.width() * index / pieces;
        const int to = box.width() * (index + 1) / pieces;
        out.append(QRect(box.left() + from, box.top(), to - from, box.height()));
    }
    // Cut left to right above. A right-to-left line wants them the other way
    // round, so that the first of them is the word that is read first.
    if (direction == Direction::RightToLeft) {
        std::reverse(out.begin(), out.end());
    }
    return out;
}

} // namespace

Direction directionOf(const QList<QRect> &boxes)
{
    if (boxes.size() < 2) {
        return Direction::RightToLeft;
    }
    return boxes.first().left() >= boxes.last().left() ? Direction::RightToLeft
                                                       : Direction::LeftToRight;
}

QList<QRect> place(const QList<QRect> &boxes, int words)
{
    if (words <= 0 || boxes.isEmpty()) {
        return {};
    }
    if (words == boxes.size()) {
        return boxes;
    }

    const Direction direction = directionOf(boxes);
    // Once, and as an int: every share below is a product of this with the word
    // count, and QList::size() is 64-bit, which makes std::min ambiguous and the
    // arithmetic harder to read for no gain — a line has tens of words, not
    // billions.
    const int found = int(boxes.size());
    QList<QRect> out;
    out.reserve(words);

    if (words < found) {
        // Fewer words than the recogniser found pieces, so it split something.
        // A spare piece joins the word before it, which puts all of the ink into
        // some word rather than leaving a fragment belonging to nobody.
        //
        // Spread evenly rather than piled onto the first word: nothing says
        // where the recogniser went wrong, so nothing should pretend it was all
        // in one place.
        for (int index = 0; index < found; ++index) {
            const int belongsTo = std::min(words - 1, index * words / found);
            if (belongsTo == out.size()) {
                out.append(boxes.at(index));
            } else {
                out.last() = out.last().united(boxes.at(index));
            }
        }
        return out;
    }

    // More words than boxes: each box takes its share of them, and a box that
    // has to hold more than one is cut across. The shares are computed from the
    // whole, so they come to exactly `words` — there is nothing left over and
    // nothing to trim.
    for (int index = 0; index < found; ++index) {
        const int share = words * (index + 1) / found - words * index / found;
        if (share == 1) {
            out.append(boxes.at(index));
        } else {
            out.append(columns(boxes.at(index), share, direction));
        }
    }
    return out;
}

QList<Laid> layOut(const QList<int> &counts, int startLine, int resumeAt, int passageWords)
{
    QList<Laid> out;
    if (passageWords <= 0) {
        return out;
    }

    int taken = std::clamp(resumeAt, 0, passageWords);
    for (int line = std::max(0, startLine); line < counts.size(); ++line) {
        if (taken >= passageWords) {
            break;
        }
        // A line nudged down to nothing is left out rather than written in
        // empty: the caller would otherwise have to ask for a box for no word.
        const int wanted = std::min(std::max(0, counts.at(line)), passageWords - taken);
        if (wanted == 0) {
            continue;
        }
        out.append(Laid{line, taken, wanted});
        taken += wanted;
    }
    return out;
}

} // namespace LineFill
} // namespace milah
