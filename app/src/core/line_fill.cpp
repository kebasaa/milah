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
    // The pieces of a word joined back up before anybody counts them. A box is
    // the recogniser's guess at a token, not a word of the manuscript — see
    // LineFill::mergeWords().
    for (auto line = lines.begin(); line != lines.end(); ++line) {
        line.value() = LineFill::mergeWords(line.value());
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

namespace {

/// The gap between each pair of neighbours along the line, in reading order.
QList<int> gapsAlong(const QList<QRect> &boxes, Direction direction)
{
    QList<int> gaps;
    for (int index = 0; index + 1 < boxes.size(); ++index) {
        const QRect &here = boxes.at(index);
        const QRect &next = boxes.at(index + 1);
        gaps.append(direction == Direction::RightToLeft ? here.left() - next.right()
                                                        : next.left() - here.right());
    }
    return gaps;
}

/// The split that best separates `values` into two groups — Otsu's method,
/// which is the standard way of finding a threshold in a histogram with two
/// humps in it, here the gaps inside words and the gaps between them.
///
/// Chosen from the line's own gaps rather than written down as a pixel count,
/// because how far apart a scribe set his words depends on the hand, the leaf
/// and the size the folio happened to be fetched at.
int splitOf(const QList<int> &values)
{
    QList<int> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double best = -1.0;
    int split = sorted.isEmpty() ? 0 : sorted.first();
    for (const int candidate : sorted) {
        int lowCount = 0;
        int highCount = 0;
        double lowSum = 0.0;
        double highSum = 0.0;
        for (const int value : sorted) {
            if (value < candidate) {
                ++lowCount;
                lowSum += value;
            } else {
                ++highCount;
                highSum += value;
            }
        }
        if (lowCount == 0 || highCount == 0) {
            continue;
        }
        const double difference = highSum / highCount - lowSum / lowCount;
        const double score = double(lowCount) * highCount * difference * difference;
        if (score > best) {
            best = score;
            split = candidate;
        }
    }
    return split;
}

} // namespace

QList<QRect> mergeWords(const QList<QRect> &boxes)
{
    if (boxes.size() < 2) {
        return boxes;
    }

    const Direction direction = directionOf(boxes);
    QList<QRect> ordered = boxes;
    std::sort(ordered.begin(), ordered.end(), [direction](const QRect &a, const QRect &b) {
        return direction == Direction::RightToLeft ? a.left() > b.left()
                                                   : a.left() < b.left();
    });

    const QList<int> gaps = gapsAlong(ordered, direction);
    if (gaps.isEmpty()) {
        return ordered;
    }

    // How many words the split says are here, and how few this is willing to
    // believe. Otsu assumes two humps; a line whose spacing is even has no split
    // to find and it can pick a threshold that swallows the line whole. The rail
    // does not fire on a line it reads properly — measured on MS Oo.1.32 it left
    // every line alone and only caught the degenerate ones.
    int words = 1;
    const int split = splitOf(gaps);
    for (const int gap : gaps) {
        if (gap >= split) {
            ++words;
        }
    }
    const int floor = (int(ordered.size()) * 2 + 4) / 5;
    words = std::clamp(std::max(words, floor), 1, int(ordered.size()));

    // Cut at the widest gaps, which is what makes the count exact: `words`
    // groups need `words - 1` splits, and the widest gaps are the likeliest
    // spaces.
    QList<int> widest = gaps;
    std::sort(widest.begin(), widest.end(), std::greater<int>());
    const int threshold = widest.at(words - 2 < 0 ? 0 : words - 2);
    int cuts = words - 1;

    QList<QRect> merged;
    merged.append(ordered.first());
    for (int index = 0; index < gaps.size(); ++index) {
        // `>=` with a budget, so a line whose widest gaps tie does not come out
        // with more groups than were asked for.
        if (gaps.at(index) >= threshold && cuts > 0) {
            merged.append(ordered.at(index + 1));
            --cuts;
            continue;
        }
        merged.last() = merged.last().united(ordered.at(index + 1));
    }
    return merged;
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
