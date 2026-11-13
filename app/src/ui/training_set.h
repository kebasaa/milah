#pragma once

#include <QDateTime>
#include <QList>
#include <QSize>
#include <QString>

namespace milah {

struct TranscribedPage;
struct TranscriptionMetadata;

/// The ground truth accumulating for one manuscript's hand.
///
/// Fine-tuning takes several folios, and a transcriber does not correct several
/// folios in one sitting. So the corrected lines are kept between sessions and
/// between files: Matthew and James of MS Oo.1.32 are two transcriptions and one
/// hand, and both feed the same set.
///
/// Kept per shelfmark because that is what a transcriber can name, not because
/// the hand stops there. A scribe outlives a shelfmark — Oo.1.32 and Oo.1.16 are
/// one hand in two bindings — so the training dialog offers the sets to be
/// ticked in any combination, and the model that comes out is named after the
/// hand rather than the manuscript.
namespace TrainingSet {

/// One manuscript's set, as it stands on disk.
struct Set
{
    /// What its folder is called, and how it is named to the training command.
    QString slug;
    /// What to call it on screen — the shelfmark as the transcriber wrote it.
    QString label;
    /// The hand its folios are written in, as the transcriptions that saved
    /// them named it. Empty for a set saved before the hand was asked for.
    QString hand;
    int folios = 0;
    int lines = 0;
    qint64 bytes = 0;
};

/// Below this, Train is not offered. Two folios of a densely written manuscript.
///
/// A floor rather than a target. Kraken holds a tenth of the data back to
/// measure the model against, so a handful of lines leaves nothing to measure
/// with — and hours of somebody's processor deserve a set worth spending them
/// on. Five folios is where this starts being worth doing.
inline constexpr int EnoughLines = 50;

/// Every hand a set has been saved under, for offering back to a transcriber.
QStringList knownHands();

/// Which of `sets` the training window opens with ticked, for a transcription
/// in `hand`. The sets in that hand that are big enough; or, when no set is in
/// that hand or none is named, every set big enough — which is what the window
/// did before hands were recorded. Sets in another named hand are never ticked
/// for this one: two hands taught as one teach neither.
QList<bool> preselected(const QList<Set> &sets, const QString &hand);

/// Records the hand `metadata` names on its set, if the set exists already, so
/// naming the hand after folios were saved does not wait for the next save.
void nameHand(const TranscriptionMetadata &metadata);

/// Whether a set holds enough to train on. The one place the rule is applied:
/// the menu entry, the Training panel, the training window's ticks and the
/// save messages all ask this.
inline bool isTrainable(const Set &set) { return set.lines >= EnoughLines; }

/// One sentence saying whether `set` is enough to train on yet, for the
/// messages that follow a save.
QString readiness(const Set &set);

/// Which set a transcription belongs to: its shelfmark, else its manuscript
/// name, else `unnamed`.
///
/// The readable part is the name with everything a folder name might choke on
/// turned to underscores, and that alone is not unique — every shelfmark in
/// Hebrew comes out the same. So a short hash of the name as written follows
/// it, and two manuscripts never share a set because their names looked alike
/// to a file system.
QString slugFor(const TranscriptionMetadata &metadata);
/// The stem a folio's two files are filed under in its set: the folio's label
/// made fit for a file name, and a short hash of folioIdentity().
QString stemFor(const TranscribedPage &page);
/// What to show for that set, before it exists on disk.
QString labelFor(const TranscriptionMetadata &metadata);

/// `bytes` as kraken will see them: turned upright where the file carries an
/// orientation, and untouched where it does not.
///
/// Milah measures a picture with its orientation applied, as the image pane
/// shows it; kraken opens the file with Pillow and applies none. Anything
/// handed to kraken — a folio to read, a folio to cut strips from, a picture
/// beside ground truth — goes through here, so both sides measure one picture.
QByteArray uprightImage(const QByteArray &bytes);

/// Where a set's files live. Made if it is not there.
QString directoryOf(const QString &slug);

/// Keeps every set under `directory` instead of Milah's own data folder, for
/// the rest of the process. Empty puts it back. For the command-line export,
/// which writes where it is told rather than into the application's sets.
void setRoot(const QString &directory);

/// What exportTranscriptions() did, file by file, for a person to read.
struct ExportReport
{
    int folios = 0;
    int lines = 0;
    QStringList said;
};

/// Writes the finished lines of each `.trscrpt` in `files` as a training set
/// under `directory`/<hand>/<set>/: ALTO and the folio picture saved in the
/// file, ready for `ketos train -f alto <directory>/<hand>/*/*.xml`.
/// Transcriptions with no hand named go under `unnamed-hand`. With `hand`
/// given, only transcriptions in that hand are written.
///
/// Nothing is fetched: a folio whose picture the file does not hold is passed
/// over and named in the report, and nothing a person has not marked read is
/// written — the same rule saving from the window follows.
ExportReport exportTranscriptions(const QStringList &files,
                                  const QString &directory,
                                  const QString &hand = QString());

/// Every set on disk, by label.
QList<Set> known();
/// One set, or an empty one where nothing has been saved for it yet.
Set contentsOf(const QString &slug);

/// What a manuscript's set already holds for one folio.
struct Saved
{
    /// False where this folio has never been saved into the set.
    bool present = false;
    /// How many lines of it the set holds.
    int lines = 0;
    /// The folio has been corrected since. Saving again replaces what is there
    /// rather than adding beside it, so this is a thing to be told rather than
    /// a thing to be careful about — but nothing said it, and a set quietly
    /// holding an older reading of a line teaches the older reading.
    bool stale = false;
};

/// What the set holds for `page`, and whether it is still what the folio says.
///
/// The comparison is of the ground truth alone, read off both layouts with one
/// parser: what was written against what would be written. Coordinates are not
/// compared — they move with whichever picture the save happened to get — and
/// no picture is fetched to answer this, because the answer does not depend on
/// one and the fetch can cross the network.
///
/// **But it is rebuilt at the scale it was saved at.** Which lines are written
/// depends on the picture: a line is clamped to it and dropped when it comes
/// out too narrow. So the rebuild takes its size from the saved file's own
/// `<Page>`, and `boxSize` — the picture the folio's boxes are in, the one on
/// screen — carries the boxes across exactly as the save did. Rebuilding at a
/// nominal size instead clamped every box past it to nothing, and called a
/// folio saved a moment ago changed.
Saved savedFolio(
    const TranscribedPage &page,
    const TranscriptionMetadata &metadata,
    const QSize &boxSize = QSize());

/// Takes `page` out of its manuscript's set. False where it was not there.
///
/// What saving does when a folio has nothing finished left on it: lines that
/// stopped being ground truth — re-read, un-vouched, laid again by a fill —
/// must stop being taught, and the set holding on to them would go on teaching
/// the old reading.
bool withdraw(const TranscribedPage &page, const TranscriptionMetadata &metadata);

/// One folio of a set, as it stands on disk.
struct Folio
{
    /// What its files are called, without the extension.
    QString stem;
    /// What the transcriber calls it — "150r" — where the set recorded it, and
    /// the stem otherwise.
    QString label;
    /// folioIdentity() of the folio, as the file records it.
    QString identity;
    int lines = 0;
    QDateTime saved;
};

/// Every character the set's ground truth contains, once each, in code point
/// order. Spaces are not characters of a line here.
QString lettersOf(const QString &slug);

/// The folios a set holds, by label.
QList<Folio> foliosOf(const QString &slug);
/// The ground truth of one folio of a set, as written.
QByteArray altoOf(const QString &slug, const QString &stem);
/// Deletes one folio's files from a set. False where nothing was removed.
bool remove(const QString &slug, const QString &stem);

/// Writes this folio's corrected lines into its manuscript's set.
///
/// **Replaces** what was there for the same folio rather than adding beside it,
/// so going back over a folio improves the set instead of teaching the model the
/// same lines twice, once badly.
///
/// `image` should be the **largest scan the library holds**, not the picture
/// Milah shows. Training cuts every line out and shows it to the model over and
/// over; what it is shown is what it learns, and a line stretched from 56 px to
/// the 120 the model wants is invented detail. `boxSize` is then the picture the
/// boxes were stored against, so the geometry can be carried across — leave it
/// empty when the image is that same picture.
///
/// Returns how many lines were written — 0 when the folio has none finished, in
/// which case nothing is written and whatever the set held for the folio before
/// is withdrawn.
int add(
    const TranscribedPage &page,
    const TranscriptionMetadata &metadata,
    const QByteArray &image,
    const QSize &boxSize = QSize());

} // namespace TrainingSet

} // namespace milah
