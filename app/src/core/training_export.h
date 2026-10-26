#pragma once

#include <QByteArray>
#include <QList>
#include <QSize>
#include <QString>

namespace milah {

struct TranscribedPage;

/// What the export made of one line of a folio, and why.
///
/// **One answer, given once.** The training panel counts lines, the line view
/// colours them and the strip preview names them, and each used to work its own
/// idea of a finished line out of the words — so a chip could read nought over
/// a line the export was quietly refusing. Everything that says whether a line
/// will be taught now reads it from here.
struct LineVerdict
{
    enum class State {
        /// Written as it stands.
        Ready,
        /// Written, but with unread marginalia cut off one end or both.
        Trimmed,
        /// Not written. `reason` says why.
        Refused,
    };

    enum class Reason {
        None,
        /// A word on it nobody has checked — including a line that is nothing
        /// but a marginal note nobody has read.
        UncheckedWord,
        /// A word on it with no box: one the recogniser's line holds but nothing
        /// knows the place of, such as the far half of a merge across two lines.
        WordWithoutBox,
        /// A word on it whose reading has been emptied.
        NoReading,
        /// A word typed beside it — split off a read word, pasted, or typed in —
        /// that nothing places on the picture. Its letters are on this line or
        /// the next, and either way one of them is missing text.
        LooseWord,
        /// An unread marginal note in the middle of it, which no outline can
        /// leave out.
        MarginInMiddle,
        /// Narrower than kraken will cut a strip from.
        TooNarrow,
    };

    /// The recogniser's index for the line — TranscribedWord::line.
    int detection = -1;
    /// Which piece of that detection this is, counting from 0. A detection the
    /// transcriber has cut with a break comes out as several.
    int part = 0;
    State state = State::Refused;
    Reason reason = Reason::None;
    /// The word that decided a refusal, as a place in the folio's verses, or
    /// -1 where no single word did.
    int verse = -1;
    int word = -1;

    bool isTaught() const { return state != State::Refused; }
};

/// A short phrase for a refusal, fit for a chip on the folio: "2 unchecked" is
/// counted by the caller, so this says the kind — "typed word, no box".
QString describe(LineVerdict::Reason reason);

/// The identity a folio is filed under in a training set: its published
/// address where it has one, its label where it has not, and its file name
/// failing both.
///
/// Not the label alone, because labels are not unique — two scans of one codex
/// can both say "1r", and a label in Hebrew has no letter a file name keeps.
QString folioIdentity(const TranscribedPage &page);

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
    /// One per line group, in reading order. `candidates` is its size and
    /// `lines` the number of them taught, so the counts cannot disagree with it.
    QList<LineVerdict> verdicts;

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
/// - A word typed **beside** a line — a word with no line at all, standing among
///   that line's words in the folio's order — is the same refusal. Skipping it
///   would write the line without the letters it holds. Where it stands at the
///   seam between two lines, nothing says which of them it belongs to, so both
///   are left out.
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
///
/// Each `<TextLine>` is called `line_<detection>_<part>`: the recogniser's line,
/// and which piece of it after the transcriber's breaks. That is the number the
/// line view shows, so a strip refused in the preview can be found on the folio.
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

/// trainingAlto()'s verdicts alone, without writing the layout.
///
/// For what is asked on every keystroke — the line view's colours — where
/// building a third of a megabyte of XML to read a list off it would be most of
/// the cost of typing a letter. The same judgement, so the two cannot differ.
QList<LineVerdict> lineVerdicts(
    const TranscribedPage &page,
    const QSize &imageSize,
    const QSize &boxSize = QSize());

} // namespace milah
