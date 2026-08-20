#pragma once

#include "core/transcription.h"

#include <QList>
#include <QMap>
#include <QPoint>
#include <QRect>

namespace milah {

/// The recognised line a point on the folio falls on, in the folio's own pixels,
/// or -1 when nothing has been read off it.
///
/// Here rather than beside the widget that draws the boxes, because two places
/// need it and neither should have to know about the other: the folio view turns
/// a right-click into a place, and the fill dialog turns that place into a line
/// *after* it has run a recognition — the folio the transcriber pointed at
/// commonly has no boxes on it yet.
///
/// It is also the part of the gesture that can be got quietly wrong: a click a
/// few pixels below a line picking the line above puts a whole passage one line
/// out, and nothing on screen would say so. `words` need not be sorted.
int lineAtPoint(const QList<TranscribedWord> &words, const QPoint &folioPixel);

/// The box a point on the folio falls inside, in the folio's own pixels, or a
/// null rectangle when it falls in no box at all.
///
/// The box rather than an index into `words`, because the caller that needs this
/// is the folio's right-click and the caller that acts on it is the controller,
/// and a position in a list one of them flattened means nothing to the other. A
/// recogniser's boxes are distinct, so a box names a word.
///
/// Strictly inside: the margin beside a line belongs to the line, which is what
/// lineAtPoint() answers, but it belongs to no word.
QRect wordAtPoint(const QList<TranscribedWord> &words, const QPoint &folioPixel);

/// The boxes of each line that a fill may lay a word into, keyed by line.
///
/// Everything the recogniser found **except the marginalia**. A published
/// transcription holds the work and not the notes beside it, so a box somebody
/// has marked as outside the text is not a slot for a word of the work — pouring
/// into it destroys the note and shifts every word after it by one for the rest
/// of the leaf.
///
/// Lines with nothing left are absent rather than present and empty: a line
/// that is all marginalia is not a line of the work at all.
QMap<int, QList<QRect>> fillableLines(const TranscribedPage &page);

/// How many words of the poured text each line takes, keyed by line: the number
/// of boxes a fill may lay into, or what the transcriber has said instead.
///
/// **The box count is a guess.** A recogniser draws a box round each thing it
/// takes for a word, and on a hand it was not trained for it splits one word
/// into two boxes as readily as it runs two words into one — so a line's box
/// count is not its word count, and pouring by it puts every word below one
/// place out for the rest of the leaf. TranscribedPage::lineWords is where a
/// person says otherwise, and this is the one place the two are reconciled, so
/// the fill, the continuation and the re-flow cannot come to different answers.
///
/// A count for a line the folio no longer has is ignored rather than added:
/// re-reading a folio renumbers its lines, and a stale answer must not push the
/// lines that are still there along.
///
/// Lines with nothing to lay into are absent, exactly as fillableLines() leaves
/// them — a line that is all marginalia is not a line of the work. A stored
/// count of **zero** is different and is kept: it says the whole line belongs
/// further down, which layOut() honours by passing over it.
QMap<int, int> wordCounts(const TranscribedPage &page);

/// Puts the words of the line after `line` onto `line`, undoing a cut the
/// segmenter made in the wrong place.
///
/// Kraken finds lines by tracing baselines through the ink, and on a rapid
/// cursive it gets the grouping wrong both ways: it runs two manuscript lines
/// into one detection, and it cuts one line into two pieces lying side by side.
/// `TranscribedWord::endsLine` is how a transcriber says the first of those — it
/// only ever adds a break. This is the other direction, and it is the one that
/// had no answer at all.
///
/// What moves: every word of the next line there is takes `line` instead, and the
/// `endsLine` standing on the last word of `line` is cleared — it would
/// otherwise put back the very cut this removes, the moment the folio was
/// exported for training.
///
/// **Geometry.** The two baselines are ordered by where they lie across the
/// folio and joined, which is right for the fault this repairs: two pieces of
/// one physical line sit side by side, and a baseline read left to right runs
/// through both. The boundary is **dropped** rather than stitched — two rings
/// cannot be unioned without QtGui, which is not what this library links, and a
/// line with no boundary already falls back in the training export to the
/// rectangle round its words, which for two halves of one line is close to the
/// truth. A join therefore gives up the ink-following mask and keeps the dewarp.
///
/// The next line **there is**, not `line + 1`: a join leaves a gap in the
/// numbering, and a line the segmenter cut into three has to be repairable by
/// joining twice.
///
/// Returns false and changes nothing when there is no line after `line`,
/// which is what the foot of a folio always answers.
///
/// The words keep their boxes and their order in the verse, so nothing here
/// moves text. Re-laying the joined line is the caller's business — and worth
/// doing, since two side-by-side pieces only fall into one right-to-left run
/// once directionOf() sees them together.
bool joinLine(TranscribedPage &page, int line);

/// How many words of the passage were laid down before `line` — the lines from
/// the fill's own start line up to but not including it.
///
/// This is where a re-flow picks the passage up again. A box turning out to be a
/// marginal note means the word poured onto it belongs further down, so the
/// lines from that one to the foot of the leaf are laid again — and they have to
/// start at the word after everything already standing above them.
///
/// **Marginalia are not counted**, because nothing was ever poured onto them.
/// Lines above the fill's start are not counted either: they hold whatever the
/// leaf opened with, which the fill never touched.
int pouredWordsBefore(const TranscribedPage &page, int line);

/// Giving every word of a line a place on the picture, however many there are.
///
/// A recogniser draws a box round each word it thinks it found, and on a hand it
/// has not been trained for it gets the count wrong constantly — a line of
/// thirteen words comes back as eight boxes, because a cursive runs words
/// together and the segmenter follows the ink rather than the language. So when
/// a published transcription is poured onto a folio, the words and the boxes do
/// not correspond, and the words with no box are the ones that used to fall off
/// the end of the folio.
///
/// They must not. A recogniser is trained on **(line image, line text)** pairs,
/// and the line's text is all of its words. A word with nowhere to go is a word
/// missing from the ground truth, and a model taught a line with a word missing
/// learns to leave it out.
///
/// So the line is the fixed thing and the words are laid into it:
///
/// | | |
/// |---|---|
/// | words = boxes | one each, boxes untouched |
/// | words > boxes | a box holding *k* words is cut into *k* columns |
/// | words < boxes | the spare box is joined onto the word before it |
///
/// **The cut is a guess and is meant to be read as one.** A box drawn round two
/// words does not record where the space between them fell, so dividing it evenly
/// is the most that can honestly be said. It puts the word on the right line and
/// in the right part of it, which is what a transcriber checking the overlay
/// against the ink needs, and it costs the training nothing: what training reads
/// is the line's own extent, which is unchanged either way.
namespace LineFill {

/// Which way the line runs, worked out from where its boxes lie rather than
/// assumed. Hebrew reads right to left, so its first word is the rightmost box —
/// but a Latin marginal note on the same folio does not, and neither has to be
/// hard-coded when the boxes already say.
enum class Direction {
    RightToLeft,
    LeftToRight,
};

/// Which way `boxes` runs. A line of fewer than two boxes cannot say, and
/// answers RightToLeft — the manuscripts this is for.
Direction directionOf(const QList<QRect> &boxes);

/// One box per word, in the line's own reading order.
///
/// `boxes` are the recogniser's, in reading order. Returns exactly `words`
/// boxes, or an empty list when there is nothing to place — no words, or no
/// boxes to place them in.
QList<QRect> place(const QList<QRect> &boxes, int words);

/// Which stretch of a passage falls on one line.
struct Laid
{
    /// Which line, as an index into the counts given.
    int line = 0;
    /// The first word of the passage on it.
    int from = 0;
    int count = 0;
};

/// Which words of a passage fall on which lines of a folio.
///
/// `counts` is how many words each line takes, one per line. `startLine` is
/// where the passage begins — the lines above it get nothing, because a leaf
/// commonly opens with the end of the book before it and that text stays where
/// it is. `resumeAt` is how far into the passage this folio begins, which is
/// what carries a book across a leaf: a folio ends mid-verse far more often than
/// not.
///
/// Lines that would take nothing are absent rather than present and empty, so a
/// caller never has to ask for a box for no word. Stops when the passage runs
/// out, which is how a book ending halfway down a leaf leaves the rest alone.
QList<Laid> layOut(const QList<int> &counts, int startLine, int resumeAt, int passageWords);

} // namespace LineFill

} // namespace milah
