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

const QRegularExpression &tokenPattern()
{
    static const QRegularExpression expression(
        QStringLiteral(
            "[\\p{L}\\p{M}\\p{N}]+"
            "(?:['\\x{2019}\\x{05F3}\\x{05F4}-][\\p{L}\\p{M}\\p{N}]+)*"
            "|[^\\s]"),
        QRegularExpression::UseUnicodePropertiesOption);
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
    const QList<SourceNote> &notes)
{
    QList<SourceToken> tokens;

    QRegularExpressionMatchIterator matches = tokenPattern().globalMatch(text);
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
