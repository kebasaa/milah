#include "core/tokenize.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

namespace milah {
namespace {

const QRegularExpression &whitespaceRuns()
{
    static const QRegularExpression expression(QStringLiteral("\\s+"));
    return expression;
}

const QRegularExpression &leadingWhitespace()
{
    static const QRegularExpression expression(QStringLiteral("^\\s+"));
    return expression;
}

const QRegularExpression &trailingWhitespace()
{
    static const QRegularExpression expression(QStringLiteral("\\s+$"));
    return expression;
}

/// How a manuscript's verse is cut into words.
///
/// A run of letters, then any number of apostrophe- or geresh-joined runs after
/// it — `ע׳י` is one abbreviation and not two words — and then an optional
/// **trailing** maqaf or hyphen, which ends the token.
///
/// That trailing joiner is the whole of the rule about compounds. A maqaf joins
/// two words in Hebrew; it does not make them one word, and Sloane 237 writes
/// 48 of its 434 words in compounds like אֲנִי-יוֹחָנָן ("I-John") using an
/// ASCII hyphen for the purpose. Held together, Sloane's John could never line
/// up against Cochin's, which is how Cochin's יהאנניס came to sit in a column
/// with Sloane's יהושע — John read as Jesus.
///
/// The joiner rides on the *first* piece rather than standing alone. Alone it
/// would open a column of its own in every one of those 48 places, against a
/// witness that writes no hyphen at all; and joinTokens closes a space after a
/// maqaf but not before one, so the text would rebuild as `אֲנִי -יוֹחָנָן`.
/// Carried on the first word it costs no column, rebuilds exactly, and keys as
/// `אני` — an exact match for what Cochin writes.
const QRegularExpression &tokenPattern()
{
    static const QRegularExpression expression(
        QStringLiteral(
            "[\\p{L}\\p{M}\\p{N}]+"
            "(?:['\\x{2019}\\x{05F3}\\x{05F4}][\\p{L}\\p{M}\\p{N}]+)*"
            "[-\\x{05BE}]?"
            "|[^\\s]"),
        QRegularExpression::UseUnicodePropertiesOption);
    return expression;
}

/// Everything up to the next space. A translation's brackets and stops belong
/// to the word they touch rather than standing on their own.
const QRegularExpression &wholeWordPattern()
{
    static const QRegularExpression expression(QStringLiteral("[^\\s]+"));
    return expression;
}

const QRegularExpression &hebrewMarks()
{
    static const QRegularExpression expression(
        QStringLiteral("[\\x{0591}-\\x{05BD}\\x{05BF}-\\x{05C7}]"));
    return expression;
}

const QRegularExpression &punctuationMarks()
{
    static const QRegularExpression expression(
        QStringLiteral("[\\p{P}\\p{S}]"),
        QRegularExpression::UseUnicodePropertiesOption);
    return expression;
}

QString trimWhitespace(QString value)
{
    return value.remove(leadingWhitespace()).remove(trailingWhitespace());
}

} // namespace

QString collapseWhitespace(const QString &value)
{
    QString result = value;
    result.replace(whitespaceRuns(), QStringLiteral(" "));
    return trimWhitespace(result);
}

int collapsedPrefixLength(const QString &raw, int offset)
{
    QString prefix = raw.left(offset);
    prefix.replace(whitespaceRuns(), QStringLiteral(" "));
    prefix.remove(leadingWhitespace());
    return int(prefix.size());
}

QStringList dividedWords(const QString &text)
{
    // Maqaf, hyphen and whitespace all join two words into what the witness
    // wrote as one; the edition may want them apart.
    static const QRegularExpression joiners(QStringLiteral("[\\x{05BE}\\-\\s]+"));

    QStringList words;
    for (const QString &part : text.split(joiners, Qt::SkipEmptyParts)) {
        const QString word = part.trimmed();
        if (!word.isEmpty()) {
            words.append(word);
        }
    }
    return words.isEmpty() ? QStringList{text} : words;
}

QString comparisonKey(const QString &text)
{
    QString result = text.normalized(QString::NormalizationForm_D);
    result.remove(hebrewMarks());
    result.remove(punctuationMarks());
    result = result.normalized(QString::NormalizationForm_C);
    result.replace(whitespaceRuns(), QStringLiteral(" "));
    return trimWhitespace(result).toLower();
}

QList<SourceToken> tokenize(
    const QString &verseId,
    const QString &text,
    const QList<SourceNote> &notes,
    TokenStyle style)
{
    QList<SourceToken> tokens;

    const QRegularExpression &pattern =
        style == TokenStyle::Attached ? wholeWordPattern() : tokenPattern();
    QRegularExpressionMatchIterator matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        SourceToken token;
        token.id = QStringLiteral("%1:t%2").arg(verseId).arg(tokens.size());
        token.text = match.captured(0);
        token.start = match.capturedStart(0);
        token.end = token.start + token.text.size();
        tokens.append(token);
    }

    for (const SourceNote &note : notes) {
        // Anchor the note to the last token that ends at or before its offset.
        int tokenIndex = 0;
        for (int index = 0; index < tokens.size(); ++index) {
            if (tokens.at(index).end <= note.charOffset) {
                tokenIndex = index;
            } else {
                break;
            }
        }

        if (tokenIndex < tokens.size()) {
            SourceNote anchored = note;
            anchored.tokenIndex = tokenIndex;
            tokens[tokenIndex].notes.append(anchored);
        }
    }

    return tokens;
}

QString joinTokens(const QList<std::optional<QString>> &tokens)
{
    static const QRegularExpression spaceBeforePunctuation(
        QStringLiteral("\\s+([,.;:!?\\x{05C3}])"));
    static const QRegularExpression spaceAfterMaqaf(
        QStringLiteral("([\\x{05BE}-])\\s+"));

    QStringList visible;
    for (const std::optional<QString> &token : tokens) {
        if (token.has_value() && !token->isEmpty()) {
            visible.append(*token);
        }
    }

    QString result = visible.join(QLatin1Char(' '));
    result.replace(spaceBeforePunctuation, QStringLiteral("\\1"));
    result.replace(spaceAfterMaqaf, QStringLiteral("\\1"));
    return trimWhitespace(result);
}

} // namespace milah
