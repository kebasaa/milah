#pragma once

#include <QChar>
#include <QString>

#include <optional>

namespace milah {

class AttestedForms;

/// True for an unpointed Hebrew consonant, final forms included.
bool isHebrewLetter(QChar character);

/// The final form of `letter`, when it has one.
std::optional<QChar> finalOf(QChar letter);

/// The plain form of `letter`, when it is a final form.
std::optional<QChar> plainOf(QChar letter);

/// Index of the last Hebrew letter in `word`, or -1 when it has none. Marks and
/// punctuation after the letter are skipped, so a pointed word answers the same
/// as its bare consonants would.
int lastLetterIndex(const QString &word);

/// The key the alignment compares two readings by: `comparisonKey()` with the
/// five final letters folded to their plain form.
///
/// Deliberately NOT `comparisonKey()` itself. That one is the join key for the
/// shipped lexicon indexes, rabbinic.words.txt, the phrase rules and the user
/// dictionary, and it is mirrored character-for-character in
/// hebrew_manuscripts/tools/python/tools/build_lexicon.py; folding there would
/// silently miss every lookup. Nothing may use this as an index key.
///
/// Folding matters because a scribe writes the same word differently depending
/// on where it falls: Cochin's מלאך and Sloane's הַמַּלְאָכוֹ differ in their kaf
/// only because one ends the word. Without this they score as far apart as two
/// words with nothing in common.
QString foldedKey(const QString &text);

/// True when the raw token carries a mark a scribe abbreviates with: geresh,
/// gershayim, or the two accents used the same way.
///
/// Must be asked of the token's own text. `comparisonKey` and `foldedKey` both
/// strip every one of these marks, so by the time a reading has been keyed
/// there is nothing left to tell ה֞ from a bare definite article.
bool isAbbreviated(const QString &rawText);

/// The letters an abbreviation is written with, for looking it up. Only
/// meaningful when `isAbbreviated` is true of the same text.
QString abbreviationStem(const QString &rawText);

/// True when the text carries vowel points.
///
/// Niqqud only, and deliberately so. The accent block U+0591-U+05AF is where
/// these manuscripts put their abbreviation marks, so a test that counted
/// combining marks in general would report Cochin's unpointed ה֞ as pointed and
/// invert every decision made on this.
bool hasNiqqud(const QString &text);

/// The same text without its vowel points: still readable Hebrew, not a key.
///
/// Distinct from comparisonKey and the other reductions here, which also drop
/// punctuation, fold letters and lowercase. Maqaf, geresh and stops all survive,
/// because what this returns goes into the edition rather than into an index —
/// it is how a pointed אֱלֹהִים from a data table is written into an unpointed
/// Combined text as אלהים.
QString withoutNiqqud(const QString &text);

/// A conservative guess at the consonantal skeleton a word is built on, or an
/// empty string when guessing would be worse than having no opinion.
///
/// Hebrew builds words from a root, usually of three letters, by adding
/// prefixes, suffixes and vowel letters around it. Two spellings that reduce to
/// the same skeleton are often the same word — but "often" is the whole
/// difficulty, so this refuses far more than it answers: never fewer than three
/// consonants, never a prefix peeled off a word that does not survive it, and
/// nothing at all when the answer would only be a guess.
///
/// `attested` decides whether a peeled prefix leaves a real word behind. מלאך
/// opens with a mem, which is a prefix letter, and peeling it unchecked yields
/// לאכ — a skeleton belonging to no word, free to collide with anything. Pass
/// nullptr to disable prefix peeling rather than to peel unchecked.
///
/// `key` must already be a foldedKey().
QString skeletonKey(const QString &key, const AttestedForms *attested);

/// Levenshtein distance between two readings, giving up early.
///
/// Returns `limit + 1` as soon as the answer is known to exceed `limit`,
/// without finishing the matrix. The alignment asks this of pairs that are
/// mostly hopeless, so the useful measure is how cheaply it says "no".
int boundedEditDistance(const QString &left, const QString &right, int limit);

} // namespace milah
