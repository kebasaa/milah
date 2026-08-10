#include "core/align_score.h"

#include "core/hebrew_forms.h"
#include "core/lexicon.h"
#include "core/name_forms.h"
#include "core/suggestions.h"

#include <algorithm>

namespace milah {
namespace {

AlignKey keyFor(const QString &text)
{
    AlignKey key;
    key.text = text;
    for (const QChar character : key.text) {
        if (isHebrewLetter(character)) {
            key.mask |= 1u << (character.unicode() - 0x05D0);
        }
    }
    return key;
}

/// Scales how much of the longer reading survives unedited onto the range a
/// near match may score. Integer throughout, rounding half up: the traceback
/// compares scores for exact equality, so the arithmetic has to be reproducible
/// rather than merely close.
int closenessScore(int kept, int longest)
{
    constexpr int span = kScoreNear - kScoreMismatch;
    return kScoreMismatch + (span * kept * 2 + longest) / (longest * 2);
}

/// What two readings are worth on the strength of their spelling alone.
int gradedCloseness(const AlignKey &left, const AlignKey &right)
{
    const int shortest = std::min(left.text.size(), right.text.size());
    const int longest = std::max(left.text.size(), right.text.size());

    if (shortest < kNearMinLength) {
        return kScoreMismatch;
    }

    const int limit = (longest * (100 - kNearFloorPercent)) / 100;
    // Two cheap refusals before the matrix: the lengths are already too far
    // apart, or the readings have no letter in common at all.
    if (longest - shortest > limit) {
        return kScoreMismatch;
    }
    if ((left.mask & right.mask) == 0) {
        return kScoreMismatch;
    }

    const int distance = boundedEditDistance(left.text, right.text, limit);
    if (distance > limit) {
        return kScoreMismatch;
    }
    return closenessScore(longest - distance, longest);
}

/// Collects the Strong's numbers a reading may carry into `into`, ignoring
/// anything already there and stopping once it is full.
///
/// Readings shorter than the near-match floor are skipped on purpose. A single
/// ה resolves to a lemma like any other word, and letting it would put every
/// abbreviation in the corpus in the same lemma bucket as whatever short word
/// sat beside it -- the very noise the floor exists to stop.
void collectNumbers(
    const QStringList &numbers, QVarLengthArray<quint32, 4> &into)
{
    for (const QString &number : numbers) {
        if (into.size() == into.capacity()) {
            return;
        }
        // "H430" -- the letter names the language, the digits the entry.
        bool ok = false;
        const quint32 parsed = QStringView(number).mid(1).toUInt(&ok);
        if (ok && !into.contains(parsed)) {
            into.append(parsed);
        }
    }
}

void collectStrongs(
    const QString &reading,
    const HebrewLexicon &lexicon,
    QVarLengthArray<quint32, 4> &strongs,
    QVarLengthArray<quint32, 4> &roots)
{
    if (reading.size() < kNearMinLength) {
        return;
    }
    collectNumbers(lexicon.strongsFor(reading), strongs);
    collectNumbers(lexicon.rootsFor(reading), roots);
}

bool sharesANumber(
    const QVarLengthArray<quint32, 4> &left,
    const QVarLengthArray<quint32, 4> &right)
{
    for (const quint32 number : left) {
        if (right.contains(number)) {
            return true;
        }
    }
    return false;
}

/// Every reading a token might stand for: what the scribe wrote, then whatever
/// the abbreviation table says it may be short for.
QVarLengthArray<const AlignKey *, 4> readingsOf(const TokenProfile &profile)
{
    QVarLengthArray<const AlignKey *, 4> readings;
    readings.append(&profile.written);
    for (const AlignKey &alternate : profile.alternates) {
        readings.append(&alternate);
    }
    return readings;
}

} // namespace

TokenProfile profileFor(const SourceToken *token, const ScoringContext &context)
{
    TokenProfile profile;
    if (!token) {
        return profile;
    }

    const QString folded = foldedKey(token->text);
    if (folded.isEmpty()) {
        // A token that is nothing but punctuation normalises away entirely.
        // Comparing those as empty strings would make every stop equal to
        // every other, so they keep their own text instead.
        profile.written =
            keyFor(token->text.normalized(QString::NormalizationForm_C));
        profile.punctuation = true;
        return profile;
    }
    profile.written = keyFor(folded);

    QStringList expansions;
    if (context.abbreviations) {
        // Asked of the raw text: the mark that says this is an abbreviation is
        // exactly what folding threw away.
        expansions = context.abbreviations->expansionsFor(token->text);
        for (const QString &expansion : expansions) {
            profile.alternates.append(keyFor(foldedKey(expansion)));
        }
    }

    profile.skeleton = skeletonKey(profile.written.text, context.attested);

    if (context.names) {
        // The written form, and then anything an abbreviation stands for: יש֞ו
        // is a name too, and reaches the table through its expansion.
        const int group = context.names->groupForFoldedKey(profile.written.text);
        if (group >= 0) {
            profile.names.append(quint32(group));
        }
        for (const QString &expansion : expansions) {
            const int other = context.names->groupFor(expansion);
            if (other >= 0 && !profile.names.contains(quint32(other))) {
                profile.names.append(quint32(other));
            }
        }
    }

    if (context.lexicon) {
        // What the scribe wrote, and then anything an abbreviation stands for:
        // ה֞ carries no useful lemma of its own, but אלהים carries H430.
        collectStrongs(
            token->text, *context.lexicon, profile.strongs, profile.roots);
        for (const QString &expansion : expansions) {
            collectStrongs(
                expansion, *context.lexicon, profile.strongs, profile.roots);
        }
    }

    return profile;
}

int scoreProfiles(const TokenProfile &left, const TokenProfile &right)
{
    if (left.written.text == right.written.text) {
        return kScoreMatch;
    }
    // A stop is not a near miss for a word, however the letters fall.
    if (left.punctuation || right.punctuation) {
        return kScoreMismatch;
    }
    // Two names the table says are different. Returned outright rather than
    // carried in `best`: the cascade below ends in std::max with the graded
    // closeness, which would lift a refusal straight back to a mismatch and
    // leave the two words together. A refusal is not a weak positive.
    if (!left.names.isEmpty() && !right.names.isEmpty()
        && !sharesANumber(left.names, right.names)) {
        return kScoreDifferentName;
    }

    // A cascade of authority: a name somebody vouched for, then the same word,
    // then words off the same root, then a guess. Only the first that applies
    // is taken, because a weaker rung firing on the same pair says nothing the
    // stronger one has not.
    int best = kScoreMismatch;
    if (sharesANumber(left.names, right.names)) {
        best = kScoreName;
    } else if (sharesANumber(left.strongs, right.strongs)) {
        best = kScoreStrongs;
    } else if (sharesANumber(left.roots, right.roots)) {
        best = kScoreRoot;
    } else if (!left.skeleton.isEmpty() && left.skeleton == right.skeleton) {
        // Only where the lexicon had nothing to say: a guess is not worth
        // consulting when there is an authority to hand.
        best = kScoreSkeleton;
    }

    if (left.alternates.isEmpty() && right.alternates.isEmpty()) {
        return std::max(best, gradedCloseness(left.written, right.written));
    }

    // One side is an abbreviation, so try what it may stand for as well. Rare
    // enough that the extra comparisons never reach the hot path.
    for (const AlignKey *first : readingsOf(left)) {
        for (const AlignKey *second : readingsOf(right)) {
            const int score = first->text == second->text
                ? kScoreExpansion
                : gradedCloseness(*first, *second);
            best = std::max(best, score);
        }
    }
    return best;
}

} // namespace milah
