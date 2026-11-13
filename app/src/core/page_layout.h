#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

class QByteArray;

namespace milah {

/// What a handwriting recogniser made of a folio: words, and where on the
/// picture each one was read from.
///
/// Two formats say this, and Milah reads both. ALTO is what kraken writes, and
/// so what Milah's own Transcribe button produces. PAGE is what an institution
/// running eScriptorium is likely to hand over. They differ in spelling rather
/// than in substance, so one reader answers both and everything downstream —
/// the overlay, the grid, the unchecked flag — never learns which was opened.

/// One word, and the ink it came from.
struct RecognisedWord
{
    QString text;
    /// In the pixel space of the image the recogniser was given — see
    /// RecognisedPage::imageSize, which is what makes it comparable to ours.
    QRect box;
    /// Which line it came from, counting from zero. For ordering and for
    /// telling a line break from a word gap; never shown.
    int line = 0;
};

/// One line of the folio as the segmenter drew it, before any of it was read.
///
/// **Kept because a recogniser trains on lines and cuts them out itself.** It
/// dewarps a line along its baseline and masks everything outside its boundary
/// to zero, so these two decide the strip a model is shown. Milah used to keep
/// only the word boxes and rebuild both from them -- a level baseline through
/// the middle of the boxes, and a rectangle round the outside.
///
/// Measured on 158r of MS Oo.1.32, that substitution is not close. A baseline
/// there falls a median of 12 pixels across a leaf whose letters are some 30
/// tall, so a level one shears the strip by a third of its own height; and the
/// boundary covers 0.65 of its bounding rectangle, so the rectangle takes in
/// half again as much ink -- the ascenders and descenders of the lines above
/// and below.
struct RecognisedLine
{
    /// Counting from zero, the same numbering a word carries.
    int index = 0;
    /// Along the line, in the same pixel space as the boxes. Two points for a
    /// straight line, more where the segmenter followed a curve.
    QList<QPoint> baseline;
    /// The outline of the line's own ink, which the mask is cut from.
    QList<QPoint> boundary;

    bool isEmpty() const { return baseline.size() < 2; }
};

struct RecognisedPage
{
    /// The page size the file declares, not assumed equal to the folio Milah
    /// holds: a box scaled by the wrong factor lands somewhere plausible and
    /// wrong, which is the worst way for this to fail.
    QSize imageSize;
    QList<RecognisedWord> words;
    /// The lines the segmenter found, where the file records them. Empty for a
    /// writer that does not, which is what the fallback in
    /// core/training_export.h is for.
    QList<RecognisedLine> lines;
};

/// Reads an ALTO or PAGE document, told apart by its root element.
///
/// Returns an empty page and fills `error` on anything it cannot honestly read.
/// Refuses rather than guesses in three places, each of which would otherwise
/// draw boxes in the wrong place and look convincing doing it: a measurement
/// unit that is not pixels, a page that declares no size, and a PAGE file
/// segmented into lines but not into words.
///
/// `error` may be null. Nothing here throws: unlike an OSIS file, this is the
/// output of a program the transcriber just ran, and a message beside the
/// Transcribe button is the whole of what they can do about it.
RecognisedPage parseRecognisedPage(const QByteArray &xml, QString *error);

} // namespace milah
