#pragma once

#include "core/types.h"

#include <QString>
#include <QVarLengthArray>

namespace milah {

class AbbreviationTable;
class AttestedForms;
class HebrewLexicon;
class NameForms;

/**
 * How two readings are scored against each other when the alignment decides
 * whether they are the same word.
 *
 * Kept out of alignment.cpp so the scoring can be tested on its own, without
 * building OSIS documents to get at it.
 */

/// A reading to compare, with its letters summarised for a quick refusal.
struct AlignKey
{
    QString text;
    /// One bit per Hebrew letter present, for an O(1) "these share nothing" test.
    quint32 mask = 0;
};

/// Everything the scorer needs about one token, worked out once per token per
/// alignment pass rather than once per cell of the dynamic-programming matrix.
struct TokenProfile
{
    /// foldedKey() of the token, or its NFC text when that normalises away to
    /// nothing -- punctuation has to stay comparable to other punctuation.
    AlignKey written;
    /// What an abbreviation may stand for, most likely first. Empty for the
    /// overwhelming majority of tokens, which is what keeps the extra
    /// comparisons off the hot path.
    QVarLengthArray<AlignKey, 3> alternates;
    /// Strong's numbers this reading may carry, already parsed: splitting a
    /// joined string inside the matrix is exactly the per-cell work the
    /// profile exists to avoid.
    QVarLengthArray<quint32, 4> strongs;
    /// Roots the dictionary derives this reading from, already parsed.
    QVarLengthArray<quint32, 4> roots;
    /// Name groups this reading belongs to — its own, and any its abbreviation
    /// expansions belong to, so יש֞ו reaches the name rung as well. Held as
    /// numbers so sharesANumber() serves all three of these lists.
    QVarLengthArray<quint32, 4> names;
    /// The guessed consonantal skeleton, or empty for "no opinion".
    QString skeleton;
    /// Set when `written` is the raw text rather than a real key.
    bool punctuation = false;
};

/**
 * Fixed point, not floating point, and every constant a multiple of kScale.
 *
 * Two reasons it must stay integral. The traceback picks its move by comparing
 * scores for exact equality, so a rounding difference would make the
 * diagonal-first tie-break fire inconsistently. And the columns this produces
 * are addressed by index in saved projects, so the alignment has to be a
 * byte-exact function of the manuscript text on every machine that builds it.
 *
 * The three scores this replaced map onto the scale exactly (4 -> 48,
 * 1 -> 12, -2 -> -24), so an alignment where no later rung fires comes out
 * bit-identical to the one before the ladder existed.
 */
constexpr int kScale = 12;

/// The folded keys are the same word.
constexpr int kScoreMatch = 4 * kScale;
/// No reason to think these are the same word.
constexpr int kScoreMismatch = -2 * kScale;

/**
 * The name table vouches that these two spellings are one proper name.
 *
 * The strongest rung below outright agreement, and the only one that is not a
 * computation. A Strong's number arrives by looking an ambiguous form up and
 * may be one of four candidates; a name group is somebody who knows saying that
 * Cochin's יאהנניס and Sloane's יוֹחָנָן are the same word. Nothing derives that:
 * four edits over a seven-letter word, no Strong's number, no shared skeleton.
 * See core/name_forms.h.
 *
 * Below kScoreMatch because the witnesses do not agree — one writes the Greek
 * transliteration and the other the Hebrew name — which is the relation
 * kScoreExpansion encodes for abbreviations.
 *
 * 42 rather than a multiple of kScale: every value on the 12-grid between 36
 * and 48 is taken. It must not be 30 — kScoreRoot, kScoreNear and
 * kScoreExpansion are all 30, and the golden dump's tally tells the rungs apart
 * by score alone.
 *
 * Because this outranks the lexicon, a wrong group beats the dictionary
 * silently. What holds that in check is that the table is small, curated, and
 * countable in the dump's tally — not anything in this file. If it ever grows
 * past a page, this rung needs re-thinking rather than more entries.
 */
constexpr int kScoreName = 42;

/**
 * The name table vouches that these two spellings are DIFFERENT proper names.
 *
 * The table's contrapositive, and the only rung on the ladder that refuses
 * rather than proposes. Naming two words as different names is as much an
 * assertion as naming them the same, and it is the only thing that can say
 * Cochin's יהאנניס is not Sloane's יהושע.
 *
 * Dearer than two gaps, deliberately. kScoreGap == kScoreMismatch, so parking
 * two unrelated words in one column costs exactly what opening a gap on each
 * side costs, and the diagonal-first tie-break then takes the pairing —
 * which is how John came to sit on Jesus in the first place. Anything above
 * -48 would leave them there.
 *
 * Only where BOTH readings are named. One named word facing an unnamed one is
 * no evidence at all; only the table asserting two names is.
 */
constexpr int kScoreDifferentName = -5 * kScale;

/**
 * The lexicon gives both readings the same Strong's number: two inflections of
 * one lemma, however differently they are spelt.
 *
 * Below an exact agreement, because an ambiguous form carries up to four
 * candidates and a shared one is evidence rather than proof. Above anything
 * the spelling alone can earn, because this is the rung that recognises
 * שֶׁנְּתָנוֹ as נתן, which no comparison of letters would.
 */
constexpr int kScoreStrongs = 36;

/**
 * The dictionary derives both readings from the same root.
 *
 * Weaker than a shared Strong's number, which says the two are one word. This
 * says only that they are related, and relatedness shades off fast: מלך "king"
 * comes from מלך "to reign", but אדם "man" comes from אדם "to be red". The
 * generator caps how far a chain is walked for that reason; the score is set
 * below kScoreStrongs to say the same thing again.
 */
constexpr int kScoreRoot = 30;

/**
 * Both readings reduce to the same guessed consonantal skeleton.
 *
 * Matched on identity only, never graded. A one-letter difference between two
 * skeletons that were themselves guesses is not evidence of anything — the
 * alphabet has twenty-two letters and three-letter strings collide readily.
 *
 * Scored well below the lexicon's rungs because it is the one part of the
 * ladder with no authority behind it. Its value is coverage: the vocabulary
 * these manuscripts are written in — יאהנניס, פטמוש, המקהלים — carries no
 * Strong's number and never will.
 */
constexpr int kScoreSkeleton = 18;
/// The most two merely similar readings can score. Below kScoreMatch, so a
/// witness that agrees exactly always outbids one that only nearly does.
constexpr int kScoreNear = 30;

/**
 * How much of the longer reading may differ before the two are called
 * unrelated, as a percentage. At 50 a six-letter word tolerates three edits.
 *
 * Generous on purpose: the corpus is medieval translation, where the same word
 * turns up with a different prefix, a different suffix and a different opinion
 * about matres lectionis. The floor below is what stops that generosity
 * turning into noise.
 */
constexpr int kNearFloorPercent = 50;

/**
 * Below this many characters a reading is not evidence of anything.
 *
 * Cochin abbreviates the divine name to a single ה. Judged by proportion alone
 * that one letter is "half of" every two-letter word and a third of every
 * three-letter one, and the old containment rule scored it positively against
 * every word containing a ה -- הָאֱלֹהִים, הֻצְרָךְ, הַמַּלְאָכוֹ alike. A short
 * reading has to match exactly or not at all.
 */
constexpr int kNearMinLength = 3;

/**
 * An abbreviation resolving exactly onto the other reading.
 *
 * Below kScoreMatch because the manuscripts do not actually agree here -- one
 * spells the word and the other leaves it to be understood -- and above
 * anything the grading can reach, because an expansion the table vouches for
 * is better evidence than a coincidence of spelling.
 */
constexpr int kScoreExpansion = kScoreNear;

/**
 * Leaving a column unread, or bringing in a word no column holds.
 *
 * Must stay equal to kScoreMismatch. Two gaps cost exactly what one mismatch
 * does, so the matrix never opens an indel pair merely to dodge a bad
 * substitution -- it spends gaps only to absorb a difference in length. Make a
 * mismatch dearer than two gaps and unrelated single words split into two
 * columns, which the consensus then reads as a gap; consensus_test's
 * fallsBackToThePriorityWitnessForATie pins that behaviour.
 */
constexpr int kScoreGap = kScoreMismatch;

/// What the scorer is allowed to consult beyond the two strings themselves.
/// Every member is optional; a null one stands its rung down rather than
/// changing the answer, so the alignment stays a pure function of its inputs
/// rather than of whichever data files happened to be found beside the binary.
struct ScoringContext
{
    const AbbreviationTable *abbreviations = nullptr;
    const HebrewLexicon *lexicon = nullptr;
    const AttestedForms *attested = nullptr;
    const NameForms *names = nullptr;
};

/// Reads one token into the form the scorer compares.
TokenProfile profileFor(const SourceToken *token, const ScoringContext &context);

/// How much the alignment wants to put these two readings in one column.
int scoreProfiles(const TokenProfile &left, const TokenProfile &right);

} // namespace milah
