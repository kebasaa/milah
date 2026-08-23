#pragma once

#include <QByteArray>
#include <QSize>

namespace milah {

struct TranscribedPage;

/// A folio's corrected lines, in the shape a recogniser is trained on.
///
/// Exists because no shipped model has seen every hand. Kraken's Hebrew models
/// are trained on square bookhands, and against a rapid cursive — MS Oo.1.32,
/// say — they produce noise: 41% of what they read off that manuscript is a
/// form the Hebrew Bible uses, against 60% on a bookhand, and reading the two
/// side by side one is Hebrew and the other is not. That gap is not closed by
/// finer sampling or a heavier engine; it is closed by showing the model the
/// hand. Kraken's own documentation and Party's both say so.
///
/// A transcriber correcting a folio is already making exactly what that takes.
/// This is the door out: what they have corrected, written back as ALTO, ready
/// for `ketos train`.
struct TrainingPage
{
    /// The ALTO, or empty when the folio has no line finished yet.
    QByteArray alto;
    int lines = 0;
    int words = 0;
    /// How many line groups the folio offers, whether or not they are finished.
    ///
    /// The denominator a progress bar fills towards, and **not** the number of
    /// things the segmenter drew: linesOf() cuts a detection wherever the
    /// transcriber said the manuscript's line ends, so a folio with breaks in it
    /// has more lines to read than the recogniser found. Counting one against
    /// the other put "35 of 31" on the training panel.
    ///
    /// Survives the empty case: a folio with nothing finished still has lines
    /// to read, and saying how many is the whole of what the panel is for.
    int candidates = 0;

    bool isEmpty() const { return alto.isEmpty(); }
};

/// The wholly corrected lines of `page`, as ALTO naming an image called
/// `imageName` and measuring `imageSize`.
///
/// `imageName` is the file beside the XML, not the folio's label. Kraken finds
/// the picture with `base_directory.joinpath(<fileName>)` — a label like "150r"
/// resolves to nothing, and the whole file is then skipped.
///
/// **A line is written only when every word on it has been checked and carries
/// a box.** Two refusals, and both matter:
///
/// - An unchecked word is the machine's own guess. Training on it teaches the
///   model the mistakes it already makes, which is worse than not training.
/// - A word with no box was typed rather than read, and nothing knows where on
///   the folio it belongs. A line holding one cannot be cut out of the picture,
///   so the line is left out rather than cut in the wrong place. Splitting a
///   read word therefore costs its line — a real limitation, and the honest one
///   while the geometry of a split is genuinely unknown.
///
/// **Marginalia are held out unless somebody has read them.** A word marked
/// marginal is on the leaf, so its ink is in the line's picture and the truth
/// has to account for it:
///
/// - checked, with a reading, it is an ordinary word of the line — a model that
///   learns marginalia beats one taught to ignore letters it can see;
/// - unchecked, at either **end** of the line, the line is written **trimmed**:
///   the polygon, the baseline and the text all stop short of it. Kraken masks
///   everything outside the polygon to zero, so a smaller polygon really does
///   keep that ink out of the picture the model sees;
/// - unchecked, in the **middle**, the line is left out. The polygon could be
///   notched around it, but the extraction dewarps along the baseline and would
///   hand the model a line with a hole in it — a stranger thing to learn than
///   nothing at all.
///
/// The result is what kraken's own reader takes, which is a stricter thing than
/// what Milah's reads — see kraken/lib/xml/alto.py:
///
/// - `MeasurementUnit` in pixels, or the file is refused outright;
/// - `<fileName>` naming the picture beside it;
/// - a `BASELINE` on every `<TextLine>`, because a baseline model — which is
///   what every Hebrew model here is — skips a line that has none, silently and
///   one at a time until there is nothing left to train on;
/// - `<SP/>` between the words, because the line's text is built by joining the
///   `<String CONTENT>` and `<SP/>` elements and nothing else. Without them a
///   line reads as one long token;
/// - every polygon and baseline **inside** the picture, tested with `>=` against
///   its width and height — so a box touching the last pixel column is out of
///   bounds — and every baseline at least 5px long.
///
/// Those last two are worth saying carefully, because kraken does not complain
/// about them. `_extract_line()` catches the failure per line and carries on
/// with a log warning, so a folio of fifty lines quietly compiles as
/// forty-seven. The bounds are therefore clamped into the picture here, and a
/// line under 8px wide is not written at all.
/// `boxSize` is the picture the boxes were stored against, when that is not the
/// picture being written beside the file. Ground truth is cut from the largest
/// scan the library holds, while a folio's boxes are in the coordinates of the
/// smaller one Milah shows — so the geometry is scaled here, once, the same way
/// applyRecognition() scales it the other way coming in. Empty means the two are
/// the same picture.
TrainingPage trainingAlto(
    const TranscribedPage &page,
    const QString &imageName,
    const QSize &imageSize,
    const QSize &boxSize = QSize());

} // namespace milah
