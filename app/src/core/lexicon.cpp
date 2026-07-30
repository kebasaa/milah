#include "core/lexicon.h"

#include "core/tokenize.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace milah {
namespace {

/// Prefixes that attach directly to a Hebrew word: conjunction vav,
/// prepositions bet, kaf, lamed, mem, the definite article he, and the
/// relative shin.
const QString &prefixLetters()
{
    static const QString letters = QStringLiteral("ובכלמהש");
    return letters;
}

const QRegularExpression &cantillation()
{
    static const QRegularExpression expression(QStringLiteral("[\\x{0591}-\\x{05AF}]"));
    return expression;
}

const QRegularExpression &punctuation()
{
    static const QRegularExpression expression(
        QStringLiteral("[\\p{P}\\p{S}]"),
        QRegularExpression::UseUnicodePropertiesOption);
    return expression;
}

QHash<QString, QString> readIndex(const QJsonObject &source)
{
    QHash<QString, QString> index;
    index.reserve(source.size());
    for (auto item = source.constBegin(); item != source.constEnd(); ++item) {
        QStringList numbers;
        const QJsonArray candidates = item.value().toArray();
        numbers.reserve(candidates.size());
        for (const QJsonValue &candidate : candidates) {
            numbers.append(candidate.toString());
        }
        index.insert(item.key(), numbers.join(QLatin1Char(' ')));
    }
    return index;
}

HebrewLexicon parse(const QByteArray &json)
{
    HebrewLexicon lexicon;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return lexicon;
    }
    return HebrewLexicon::fromJson(document.object());
}

} // namespace

HebrewLexicon HebrewLexicon::fromJson(const QJsonObject &document)
{
    HebrewLexicon lexicon;
    lexicon.m_pointed = readIndex(document.value(QStringLiteral("pointed")).toObject());
    lexicon.m_forms = readIndex(document.value(QStringLiteral("forms")).toObject());

    const QJsonObject entries = document.value(QStringLiteral("entries")).toObject();
    lexicon.m_entries.reserve(entries.size());
    for (auto item = entries.constBegin(); item != entries.constEnd(); ++item) {
        const QJsonObject record = item.value().toObject();
        lexicon.m_entries.insert(
            item.key(),
            LexiconEntry{
                item.key(),
                record.value(QStringLiteral("l")).toString(),
                record.value(QStringLiteral("x")).toString(),
                record.value(QStringLiteral("g")).toString(),
            });
    }
    return lexicon;
}

QString pointedKey(const QString &text)
{
    QString result = text.normalized(QString::NormalizationForm_D);
    result.remove(cantillation());
    result.remove(punctuation());
    return result.normalized(QString::NormalizationForm_C).trimmed();
}

const HebrewLexicon &HebrewLexicon::shared()
{
    static const HebrewLexicon lexicon = [] {
        QFile file(QStringLiteral(":/data/hebrew_lexicon.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return HebrewLexicon();
        }
        return parse(file.readAll());
    }();
    return lexicon;
}

HebrewLexicon HebrewLexicon::fromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return HebrewLexicon();
    }
    return parse(file.readAll());
}

QString HebrewLexicon::numbersFor(const QString &word) const
{
    // Pointed first: it distinguishes readings the consonants alone cannot.
    const QString pointed = pointedKey(word);
    if (!pointed.isEmpty()) {
        const auto match = m_pointed.constFind(pointed);
        if (match != m_pointed.constEnd()) {
            return match.value();
        }
    }

    const QString consonantal = comparisonKey(word);
    if (consonantal.isEmpty()) {
        return QString();
    }
    const auto match = m_forms.constFind(consonantal);
    if (match != m_forms.constEnd()) {
        return match.value();
    }

    // Hebrew prefixes agglutinate and the index holds the forms the Masoretic
    // text happens to use, so a word carrying an extra prefix misses. Peeling
    // one or two prefix letters recovers most of those. The stripped form is
    // only accepted when it is itself in the index, which keeps the heuristic
    // from inventing readings out of coincidental letter sequences.
    for (int peeled = 1; peeled <= 2 && peeled + 1 < consonantal.size(); ++peeled) {
        if (!prefixLetters().contains(consonantal.at(peeled - 1))) {
            break;
        }
        const auto stripped = m_forms.constFind(consonantal.mid(peeled));
        if (stripped != m_forms.constEnd()) {
            return stripped.value();
        }
    }

    return QString();
}

QList<LexiconEntry> HebrewLexicon::lookup(const QString &word) const
{
    QList<LexiconEntry> result;
    const QString numbers = numbersFor(word);
    if (numbers.isEmpty()) {
        return result;
    }

    for (const QString &number : numbers.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const auto entry = m_entries.constFind(number);
        if (entry != m_entries.constEnd()) {
            result.append(entry.value());
        } else {
            result.append(LexiconEntry{number, QString(), QString(), QString()});
        }
    }
    return result;
}

bool HebrewLexicon::knows(const QString &word) const
{
    return !numbersFor(word).isEmpty();
}

} // namespace milah
