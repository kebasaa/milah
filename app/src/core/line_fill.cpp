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


QMap<int, int> wordCounts(const TranscribedPage &page)
{
    QMap<int, int> counts;
    const QMap<int, QList<QRect>> lines = fillableLines(page);
    for (auto entry = lines.constBegin(); entry != lines.constEnd(); ++entry) {
        // The transcriber's answer where there is one — including nought, which
        // says the whole line belongs further down and is not the same as never
        // having said anything.
        const auto said = page.lineWords.constFind(entry.key());
        counts.insert(
            entry.key(),
            said != page.lineWords.constEnd() ? *said : int(entry.value().size()));
    }
    return counts;
}


int vouchForLine(TranscribedPage &page, int line)
{
    if (line < 0) {
        return 0;
    }
    int vouched = 0;
    for (TranscribedVerse &verse : page.verses) {
        for (TranscribedWord &word : verse.words) {
            if (word.line != line || word.box.isNull() || word.hebrew.isEmpty()) {
                continue;
            }
            // See the header: a note beside the text is somebody else's to read.
            if (word.marginal || !word.unchecked) {
                continue;
            }
            word.unchecked = false;
            ++vouched;
        }
    }
    return vouched;
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

namespace {

/// Where a run of baseline points lies across the folio, for ordering two of
/// them. The mean rather than the first point: a segmenter's baseline can start
/// anywhere along the ink, and one outlying end would decide the order on its
/// own.
double meanX(const QList<QPoint> &points)
{
    if (points.isEmpty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const QPoint &point : points) {
        total += point.x();
    }
    return total / points.size();
}

} // namespace

bool joinLine(TranscribedPage &page, int line)
{
    if (line < 0) {
        return false;
    }

    // Asked of the words and not of page.lines, because the words are what every
    // other part of this file groups by, and a folio read before Milah kept the
    // segmenter's geometry has words with lines and no geometry at all.
    //
    // The *next line there is* rather than line + 1. A join leaves the numbering
    // with a gap in it, and a manuscript line the segmenter cut into three has
    // to be repairable by joining twice.
    bool here = false;
    int next = -1;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.box.isNull()) {
                continue;
            }
            if (word.line == line) {
                here = true;
            } else if (word.line > line && (next < 0 || word.line < next)) {
                next = word.line;
            }
        }
    }
    if (!here || next < 0) {
        return false;
    }

    // The break that used to stand at the seam. Left in place it would be
    // exported as a cut through the middle of the joined line, which is exactly
    // what the transcriber has just said is wrong — and the last word of `line`
    // is the only place it can be, since a break anywhere earlier belongs to a
    // manuscript line that genuinely ends there.
    TranscribedWord *lastOfLine = nullptr;
    for (TranscribedVerse &verse : page.verses) {
        for (TranscribedWord &word : verse.words) {
            if (word.line == line && !word.box.isNull()) {
                lastOfLine = &word;
            }
        }
    }
    if (lastOfLine) {
        lastOfLine->endsLine = false;
    }

    for (TranscribedVerse &verse : page.verses) {
        for (TranscribedWord &word : verse.words) {
            if (word.line == next) {
                word.line = line;
            }
        }
    }

    // The geometry, where the folio has any. Both entries may be missing — an
    // older file, or an imported ALTO without shapes — and the join is still
    // worth making; the export falls back to the rectangle round the words.
    int at = -1;
    int after = -1;
    for (int index = 0; index < page.lines.size(); ++index) {
        if (page.lines.at(index).index == line) {
            at = index;
        } else if (page.lines.at(index).index == next) {
            after = index;
        }
    }
    if (after >= 0) {
        if (at >= 0) {
            QList<QPoint> joined;
            if (meanX(page.lines.at(at).baseline)
                <= meanX(page.lines.at(after).baseline)) {
                joined = page.lines.at(at).baseline;
                joined.append(page.lines.at(after).baseline);
            } else {
                joined = page.lines.at(after).baseline;
                joined.append(page.lines.at(at).baseline);
            }
            page.lines[at].baseline = joined;
            // See the header: dropped rather than stitched.
            page.lines[at].boundary.clear();
            page.lines.removeAt(after);
        } else {
            page.lines[after].index = line;
            page.lines[after].boundary.clear();
        }
    }

    return true;
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
