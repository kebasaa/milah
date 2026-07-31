#pragma once

#include "core/types.h"

#include <QList>
#include <QString>

#include <stdexcept>

namespace milah {

/// Manuscripts are referred to rather than copied: a SourceDocument carries the
/// whole raw OSIS text with it.
using DocumentRefs = QList<const SourceDocument *>;

class AlignmentError : public std::runtime_error
{
public:
    explicit AlignmentError(const QString &message)
        : std::runtime_error(message.toStdString())
        , m_message(message)
    {
    }

    QString message() const { return m_message; }

private:
    QString m_message;
};

class AbbreviationTable;
class AttestedForms;
class HebrewLexicon;

/// What the scorer may consult while aligning.
///
/// Passed in rather than reached for through a singleton, so that alignVerse
/// stays a pure function of its arguments. The columns it produces are
/// addressed by index in saved projects — column splits, note anchors and
/// translation spans all name one — so the same manuscripts have to yield the
/// same columns whether or not a data file happened to be found beside the
/// binary. Leaving a member null stands its part of the scoring down; it never
/// silently changes the answer.
struct AlignmentOptions
{
    const AbbreviationTable *abbreviations = nullptr;
    const HebrewLexicon *lexicon = nullptr;
    /// Vouches for a stem left behind by peeling a prefix. Without it the
    /// peeling is disabled rather than done unchecked.
    const AttestedForms *attested = nullptr;
};

/// Aligns one verse across every manuscript, starting from the priority
/// witness and merging each remaining witness in with a Needleman-Wunsch pass.
AlignedVerse alignVerse(
    const QString &verseId,
    const DocumentRefs &manuscripts,
    const QString &priorityId,
    const AlignmentOptions &options = {});

/// Widens the columns the editor has divided, so the Combined edition can read
/// two words where a witness writes one — `אֲשֶׁר־בָּהּ` as `אֲשֶׁר בָּהּ`.
///
/// `splits` holds indices into the columns `alignVerse` produced, once per
/// extra column wanted; an index repeated twice yields two extra. The inserted
/// columns follow their original and carry no witness reading, because the
/// division belongs to the edition and not to the manuscripts.
///
/// The alignment is recomputed whenever the sources, the chapter or the
/// reference witness change, so this has to be reapplied every time or the
/// Combined draft stops lining up with the columns it describes.
AlignedVerse applyColumnSplits(AlignedVerse aligned, const QList<int> &splits);

/// A divided word and the columns it now occupies: the column the alignment
/// produced, plus every column dividing it has since added.
struct ColumnGroup
{
    /// Index into the columns before any division, or -1 when out of range.
    int original = -1;
    /// Where the group starts among the columns actually on screen.
    int start = 0;
    int size = 1;
};

/// Which divided word a column on screen belongs to.
///
/// Dividing and joining both work on whole groups rather than on single cells,
/// so that a word and the columns holding it stay in step however many times it
/// has been divided. Both operations ask this, so neither can develop its own
/// idea of where a group begins.
///
/// `columnCount` is the count after applyColumnSplits, and the two agree about
/// which entries in `splits` count: one naming a column the alignment no longer
/// has is ignored by both.
ColumnGroup columnGroupFor(const QList<int> &splits, int columnCount, int columnIndex);

/// One manuscript's note on one aligned column.
struct ColumnNote
{
    QString sourceId;
    SourceNote note;
};

/// Every note any witness attaches to this column, in the order `sources` are
/// given, so a panel showing them does not reshuffle between selections.
///
/// The notes belong to the column rather than to the reading chosen from it: a
/// scribe's comment is worth reading whichever witness the edition follows
/// there, and a column the edition leaves empty may still be annotated.
QList<ColumnNote> columnNotes(const AlignmentColumn &column, const DocumentRefs &sources);

/// The manuscript a verse is read against: `preferred` when it has the verse,
/// otherwise the first of `sources` that does, and empty when none do.
///
/// A witness cannot be the reference for a verse it is silent for. The
/// alignment would begin from no words at all, and the consensus, finding no
/// majority, would fall back to that same silence — leaving the edition blank
/// exactly where another manuscript does read something.
QString referenceForVerse(
    const QString &verseId,
    const DocumentRefs &sources,
    const QString &preferred);

/// Picks a reading for each column: the majority where there is one, otherwise
/// the priority witness, flagging the column for review.
CombinedDraft generateCombined(
    const AlignedVerse &aligned,
    const DocumentRefs &manuscripts,
    const QString &priorityId);

/// The Combined reading as running text: the manual edit when there is one,
/// otherwise the chosen tokens joined up.
QString combinedText(const CombinedDraft &draft);

/// Where a column's word begins in the text `combinedText()` produces, for
/// anchoring a note to it in an exported edition. Measured by joining the
/// columns before it exactly as the text itself is joined, so the two cannot
/// drift apart. An index past the end gives the length of the whole text.
int columnCharOffset(const CombinedDraft &draft, int columnIndex);

/// Spreads a translation's tokens evenly across the columns its own manuscript
/// occupies, given as indices into the verse in reading order.
///
/// Scoped to that manuscript because the verse's columns include words only
/// other witnesses read: spread across all of them, a translation's first word
/// lands on a column its manuscript is silent for and everything after it is
/// out by one. An empty `columns` means the caller has no manuscript to go by,
/// and nothing is aligned.
///
/// Marked high confidence only when the counts match exactly.
QList<TranslationSpan> alignTranslation(
    const QString &translationId,
    const QString &verseId,
    int tokenCount,
    const QList<int> &columns);

} // namespace milah
