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

/// `boxes` with the ones that are pieces of a single word joined back together.
///
/// **A recogniser's box is not a word.** An ALTO `<String>` is a token of the
/// model's *own reading*, cut wherever the model thought a space fell — so on a
/// hand it cannot read, the cuts are noise. Measured on 158r of MS Oo.1.32,
/// whose text is known: Kraken returns 19 boxes for a line of 13 words, and 493
/// across a leaf holding 236. A fill that takes one box for one word therefore
/// lays half again too many words on every line and runs further ahead of the
/// manuscript with each one.
///
/// So the gaps decide instead. Within a word the pieces nearly touch; between
/// words the scribe left space. The split between those two is found by Otsu's
/// method over the line's own gaps — no fixed pixel count, because a line's
/// spacing depends on the hand, the folio and the resolution it was fetched at.
/// On that folio it gives 14 words for the line of 13, and 255 for the leaf of
/// 236: wrong by 8% where the boxes alone are wrong by 109%.
///
/// **A stopgap, and worth naming as one.** The real answer is a model that reads
/// the hand, after which the tokens are words and none of this is needed. Until
/// then this is the difference between a pour that drifts a little and one that
/// is unusable by the foot of the leaf.
QList<QRect> mergeWords(const QList<QRect> &boxes);

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
