#include "core/hebrew_forms.h"

#include "core/suggestions.h"
#include "core/tokenize.h"

#include <QList>
#include <QStringList>
#include <QVarLengthArray>

#include <algorithm>

namespace milah {
namespace {

/// The five letters written differently at the end of a word.
struct FinalForm
{
    QChar plain;
    QChar final;
};

const QList<FinalForm> &finalForms()
{
    static const QList<FinalForm> forms = {
        {QChar(0x05DB), QChar(0x05DA)}, // kaf
        {QChar(0x05DE), QChar(0x05DD)}, // mem
        {QChar(0x05E0), QChar(0x05DF)}, // nun
        {QChar(0x05E4), QChar(0x05E3)}, // pe
        {QChar(0x05E6), QChar(0x05E5)}, // tsadi
    };
    return forms;
}

/// The marks a scribe abbreviates with: the punctuation geresh and gershayim,
/// and the whole Hebrew accent block.
///
/// The accents are included wholesale because in these manuscripts they are
/// not accents. They are late translations rather than copies of the Masoretic
/// text, and none of the witnesses carries cantillation at all — Sloane 237,
/// the one that is pointed, has no accent in the block anywhere. What they do
/// carry is a mark over an abbreviation, and copyists are not consistent about
/// which: Revelation and Matthew use U+059E, James U+0594 and U+059C, Matthew
/// also U+059D, and Matthew again the punctuation geresh U+05F3. Naming them
/// one at a time means missing the next one a scribe reaches for.
///
/// Widening this cannot invent an expansion: a word is only expanded when its
/// stem is in the abbreviation table as well.
/// A vowel point, and nothing from the accent block.
///
/// The two are neighbours in Unicode but opposites here: the accents are how a
/// scribe marks an abbreviation, the points are how an edition marks its
/// vowels. Counting the accents as pointing would call every abbreviation in
/// Cochin a pointed word.
///
/// Dagesh U+05BC, rafe U+05BF and the shin and sin dots U+05C1 and U+05C2 go
/// with the vowels: an unpointed text writes a bare מ and a bare ש. Maqaf
/// U+05BE is a word joiner rather than a point, and stays.
bool isNiqqud(QChar character)
{
    const char16_t code = character.unicode();
    return (code >= 0x05B0 && code <= 0x05BD) // sheva .. meteg, incl. dagesh
        || code == 0x05BF                     // rafe
        || code == 0x05C1 || code == 0x05C2   // shin and sin dots
        || code == 0x05C7;                    // qamats qatan
}

bool isAbbreviationMark(QChar character)
{
    const char16_t code = character.unicode();
    return (code >= 0x0591 && code <= 0x05AF) // accents, used here for abbreviation
        || code == 0x05F3                     // punctuation geresh
        || code == 0x05F4;                    // punctuation gershayim
}

/// Never reduce a word below this. Two consonants name so little that any two
/// of them collide, and the alphabet is small.
constexpr int kMinimumSkeleton = 3;

/// Endings common enough to be worth removing, longest first so that ינו is
/// not mistaken for a bare ו. Deliberately short: every entry is a chance to
/// take a letter that belonged to the word.
///
/// Spelt folded, because that is what this is asked of — the plural ים ends in
/// a final mem, which by this point is a plain one. A bare final kaf or mem is
/// left off the list entirely: מלאך ends in a kaf that belongs to it, and no
/// rule short of knowing the word tells that apart from a suffix.
const QStringList &nounSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral("ינו"), QStringLiteral("כמ"), QStringLiteral("המ"),
        QStringLiteral("ימ"),  QStringLiteral("ות"), QStringLiteral("יו"),
        QStringLiteral("תי"),  QStringLiteral("נו"), QStringLiteral("ה"),
        QStringLiteral("ו"),   QStringLiteral("י"),
    };
    return suffixes;
}

int countsLetters(const QString &text)
{
    int total = 0;
    for (const QChar character : text) {
        if (isHebrewLetter(character)) {
            ++total;
        }
    }
    return total;
}

/// A folded string put back the way a scribe would end it, so it can be looked
/// up in a list keyed on what was actually written.
QString asWritten(const QString &folded)
{
    const int last = lastLetterIndex(folded);
    if (last < 0) {
        return folded;
    }
    QString result = folded;
    if (const std::optional<QChar> final = finalOf(result.at(last))) {
        result[last] = *final;
    }
    return result;
}

} // namespace

bool isHebrewLetter(QChar character)
{
    return character.unicode() >= 0x05D0 && character.unicode() <= 0x05EA;
}

bool isPrefixLetter(QChar character)
{
    static const QString letters = QStringLiteral("ובכלמהש");
    return letters.contains(character);
}

std::optional<QChar> finalOf(QChar letter)
{
    for (const FinalForm &form : finalForms()) {
        if (form.plain == letter) {
            return form.final;
        }
    }
    return std::nullopt;
}

std::optional<QChar> plainOf(QChar letter)
{
    for (const FinalForm &form : finalForms()) {
        if (form.final == letter) {
            return form.plain;
        }
    }
    return std::nullopt;
}

int lastLetterIndex(const QString &word)
{
    for (int index = word.size() - 1; index >= 0; --index) {
        if (isHebrewLetter(word.at(index))) {
            return index;
        }
    }
    return -1;
}

QString foldedKey(const QString &text)
{
    QString result = comparisonKey(text);
    // Every position, not just the last: a token joined at a maqaf or hyphen
    // carries its first word's ending inside the key.
    for (int index = 0; index < result.size(); ++index) {
        if (const std::optional<QChar> plain = plainOf(result.at(index))) {
            result[index] = *plain;
        }
    }
    return result;
}

bool isAbbreviated(const QString &rawText)
{
    const QString decomposed = rawText.normalized(QString::NormalizationForm_D);
    for (const QChar character : decomposed) {
        if (isAbbreviationMark(character)) {
            return true;
        }
    }
    return false;
}

QString abbreviationStem(const QString &rawText)
{
    // The letters alone. Which mark was used, and where in the word it fell,
    // vary between copyists and between manuscripts; the letters do not.
    return foldedKey(rawText);
}

bool hasNiqqud(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    for (const QChar character : decomposed) {
        if (isNiqqud(character)) {
            return true;
        }
    }
    return false;
}

QString withoutNiqqud(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);

    QString result;
    result.reserve(decomposed.size());
    for (const QChar character : decomposed) {
        if (!isNiqqud(character)) {
            result.append(character);
        }
    }
    return result.normalized(QString::NormalizationForm_C);
}

QString skeletonKey(const QString &key, const AttestedForms *attested)
{
    if (countsLetters(key) < kMinimumSkeleton) {
        return QString();
    }

    QString result = key;

    // A prefix comes off only when what is left is a word in its own right.
    // This is the rule lexicon.cpp already follows when peeling for a Strong's
    // lookup, and for the same reason: without it the peeling invents readings
    // out of coincidental letter sequences.
    if (attested) {
        for (int peeled = 1; peeled <= 2 && peeled < result.size(); ++peeled) {
            if (!isPrefixLetter(result.at(peeled - 1))) {
                break;
            }
            const QString rest = result.mid(peeled);
            if (countsLetters(rest) < kMinimumSkeleton) {
                break;
            }
            // The attested lists are keyed unfolded, so the remainder is asked
            // about both as it stands and as it would be written at the end of
            // a word.
            if (attested->contains(rest) || attested->contains(asWritten(rest))) {
                result = rest;
                break;
            }
        }
    }

    // One suffix, longest first, and only while a skeleton is left. Not checked
    // against the attested list: a bare stem is frequently not a word anyone
    // writes, which is why the list is kept short rather than validated.
    for (const QString &suffix : nounSuffixes()) {
        if (result.size() > suffix.size() && result.endsWith(suffix)
            && countsLetters(result.left(result.size() - suffix.size()))
                >= kMinimumSkeleton) {
            result.chop(suffix.size());
            break;
        }
    }

    // Vowel letters, inside only. A ו or י opening or closing a word is far
    // more often part of the root than a mater.
    QString bare;
    bare.reserve(result.size());
    for (int index = 0; index < result.size(); ++index) {
        const QChar letter = result.at(index);
        const bool mater = (letter == QChar(0x05D5) || letter == QChar(0x05D9));
        const bool inside = index > 0 && index + 1 < result.size();
        if (mater && inside && countsLetters(bare) + (result.size() - index - 1)
                >= kMinimumSkeleton) {
            continue;
        }
        bare.append(letter);
    }
    result = bare;

    // No opinion is better than a manufactured one.
    if (countsLetters(result) < kMinimumSkeleton || result == key) {
        return QString();
    }
    return result;
}

int boundedEditDistance(const QString &left, const QString &right, int limit)
{
    const int rows = int(left.size());
    const int cols = int(right.size());
    const int beyond = limit + 1;

    if (limit < 0) {
        return beyond;
    }
    // Length alone already settles it: every extra character costs an edit.
    if (rows - cols > limit || cols - rows > limit) {
        return beyond;
    }
    if (rows == 0) {
        return cols > limit ? beyond : cols;
    }
    if (cols == 0) {
        return rows > limit ? beyond : rows;
    }

    QVarLengthArray<int, 32> previous(cols + 1);
    QVarLengthArray<int, 32> current(cols + 1);
    for (int col = 0; col <= cols; ++col) {
        previous[col] = col;
    }

    for (int row = 1; row <= rows; ++row) {
        current[0] = row;
        int best = current[0];
        for (int col = 1; col <= cols; ++col) {
            const int cost = left.at(row - 1) == right.at(col - 1) ? 0 : 1;
            current[col] = std::min(
                {previous.at(col - 1) + cost,
                 previous.at(col) + 1,
                 current.at(col - 1) + 1});
            best = std::min(best, current.at(col));
        }
        // The smallest value in a row never falls as the rows go on, so once
        // the whole row is past the limit the answer is too.
        if (best > limit) {
            return beyond;
        }
        previous = current;
    }

    return previous.at(cols) > limit ? beyond : previous.at(cols);
}

} // namespace milah
