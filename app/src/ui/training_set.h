#pragma once

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

/// Which set a transcription belongs to: its shelfmark, else its manuscript
/// name, else `unnamed`.
QString slugFor(const TranscriptionMetadata &metadata);
/// What to show for that set, before it exists on disk.
QString labelFor(const TranscriptionMetadata &metadata);

/// Where a set's files live. Made if it is not there.
QString directoryOf(const QString &slug);

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
Saved savedFolio(const TranscribedPage &page, const TranscriptionMetadata &metadata);

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
/// which case nothing is written at all.
int add(
    const TranscribedPage &page,
    const TranscriptionMetadata &metadata,
    const QByteArray &image,
    const QSize &boxSize = QSize());

} // namespace TrainingSet

} // namespace milah
