#include "core/suggestions.h"

#include "core/data_paths.h"
#include "core/lexicon.h"
#include "core/tokenize.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

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

bool isHebrewLetter(QChar character)
{
    return character.unicode() >= 0x05D0 && character.unicode() <= 0x05EA;
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

/// Checks that need no view about the language, only about how Hebrew is
/// written. Each returns the corrected spelling so the editor can accept it in
/// one gesture rather than retyping the word.
std::optional<Suggestion> orthography(const QString &word, int column)
{
    if (word.isEmpty()) {
        return std::nullopt;
    }

    if (word.at(0).isMark()) {
        QString fixed = word;
        while (!fixed.isEmpty() && fixed.at(0).isMark()) {
            fixed.remove(0, 1);
        }
        return Suggestion{
            column,
            fixed,
            QStringLiteral("Starts with a vowel point that has no letter to sit on."),
            SuggestionKind::Orthography};
    }

    for (int index = 1; index < word.size(); ++index) {
        if (word.at(index).isMark() && word.at(index) == word.at(index - 1)) {
            QString fixed = word;
            fixed.remove(index, 1);
            return Suggestion{
                column,
                fixed,
                QStringLiteral("The same vowel point appears twice in a row."),
                SuggestionKind::Orthography};
        }
    }

    const int last = lastLetterIndex(word);
    if (last >= 0) {
        if (const std::optional<QChar> wanted = finalOf(word.at(last))) {
            QString fixed = word;
            fixed[last] = *wanted;
            return Suggestion{
                column,
                fixed,
                QStringLiteral("A word ending in %1 is written %2.")
                    .arg(word.at(last))
                    .arg(*wanted),
                SuggestionKind::Orthography};
        }

        for (int index = 0; index < last; ++index) {
            if (const std::optional<QChar> wanted = plainOf(word.at(index))) {
                QString fixed = word;
                fixed[index] = *wanted;
                return Suggestion{
                    column,
                    fixed,
                    QStringLiteral("%1 is a final form, but this is not the end "
                                   "of the word.")
                        .arg(word.at(index)),
                    SuggestionKind::Orthography};
            }
        }
    }

    return std::nullopt;
}

} // namespace

void PhraseRules::append(const PhraseRules &other)
{
    m_rules.append(other.m_rules);
}

PhraseRules PhraseRules::fromJson(const QJsonObject &document)
{
    PhraseRules rules;
    for (const QJsonValue &value : document.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject entry = value.toObject();

        PhraseRule rule;
        for (const QJsonValue &word : entry.value(QStringLiteral("match")).toArray()) {
            rule.match.append(comparisonKey(word.toString()));
        }
        for (const QJsonValue &word : entry.value(QStringLiteral("replace")).toArray()) {
            // Normalised, because a rules file is hand-written: the same
            // pointed word can be typed with its marks in either order, and
            // the unnormalised one would go into the edition and the exported
            // OSIS looking identical but comparing unequal to everything else.
            rule.replace.append(word.toString().normalized(QString::NormalizationForm_C));
        }
        rule.reason = entry.value(QStringLiteral("reason")).toString();

        // A rule that does not line up would silently propose the wrong word.
        if (rule.match.isEmpty() || rule.match.size() != rule.replace.size()
            || rule.match.contains(QString())) {
            continue;
        }
        rules.m_rules.append(rule);
    }
    return rules;
}

PhraseRules PhraseRules::fromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return PhraseRules();
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? fromJson(document.object()) : PhraseRules();
}

const PhraseRules &PhraseRules::shared()
{
    static const PhraseRules rules = [] {
        PhraseRules loaded =
            fromFile(locateDataFile(QStringLiteral("hebrew_phrase_rules.json")));
        // Rules the editor keeps outside the data directory, so the table can
        // grow without touching the shipped file.
        const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!directory.isEmpty()) {
            loaded.append(
                fromFile(directory + QStringLiteral("/hebrew_phrase_rules.json")));
        }
        return loaded;
    }();
    return rules;
}

QString UserDictionary::defaultPath()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        return QString();
    }
    return directory + QStringLiteral("/user-dictionary.txt");
}

UserDictionary::UserDictionary(const QString &path)
    : m_path(path)
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString key = comparisonKey(stream.readLine());
        if (!key.isEmpty()) {
            m_keys.insert(key);
        }
    }
}

bool UserDictionary::contains(const QString &word) const
{
    const QString key = comparisonKey(word);
    return !key.isEmpty() && m_keys.contains(key);
}

bool UserDictionary::add(const QString &word)
{
    const QString key = comparisonKey(word);
    if (key.isEmpty()) {
        return false;
    }
    if (m_keys.contains(key)) {
        return true;
    }
    m_keys.insert(key);

    if (m_path.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QStringList ordered(m_keys.constBegin(), m_keys.constEnd());
    ordered.sort();
    QTextStream stream(&file);
    for (const QString &entry : ordered) {
        stream << entry << '\n';
    }
    stream.flush();
    return file.commit();
}

QList<Suggestion> reviewVerse(
    const QList<std::optional<QString>> &words,
    const HebrewLexicon &lexicon,
    const PhraseRules &rules,
    const QSet<QString> &accepted)
{
    QList<Suggestion> suggestions;

    // Without a lexicon every word is "not attested", which would mark the
    // whole verse and say nothing. The orthography and phrase checks do not
    // depend on it, so only this one stands down.
    const bool canJudgeVocabulary = !lexicon.isEmpty();

    for (int column = 0; column < words.size(); ++column) {
        const std::optional<QString> &word = words.at(column);
        if (!word.has_value() || word->isEmpty()) {
            continue;
        }

        if (const std::optional<Suggestion> found = orthography(*word, column)) {
            suggestions.append(*found);
            // One flag per word: the spelling has to be settled before it is
            // worth asking whether the lexicon knows it.
            continue;
        }

        const QString key = comparisonKey(*word);
        if (!canJudgeVocabulary || key.isEmpty() || accepted.contains(key)
            || lexicon.knows(*word)) {
            continue;
        }
        suggestions.append(Suggestion{
            column,
            QString(),
            QStringLiteral("Not attested in the Hebrew Bible. Correct for a New "
                           "Testament word — add it to the dictionary to stop asking."),
            SuggestionKind::UnknownForm});
    }

    // Phrases run over the words the verse actually reads, not over the
    // columns. A column no witness filled is an artefact of the alignment, and
    // letting one sit between two words would hide the phrase they form.
    QList<int> spoken;
    for (int column = 0; column < words.size(); ++column) {
        if (words.at(column).has_value() && !words.at(column)->isEmpty()) {
            spoken.append(column);
        }
    }

    for (const PhraseRule &rule : rules.rules()) {
        const int span = int(rule.match.size());
        for (int start = 0; start + span <= spoken.size(); ++start) {
            bool matched = true;
            for (int offset = 0; offset < span && matched; ++offset) {
                const QString &word = *words.at(spoken.at(start + offset));
                matched = comparisonKey(word) == rule.match.at(offset);
            }
            if (!matched) {
                continue;
            }

            for (int offset = 0; offset < span; ++offset) {
                const int column = spoken.at(start + offset);
                const QString &replacement = rule.replace.at(offset);
                if (replacement.isEmpty() || replacement == *words.at(column)) {
                    continue;
                }
                suggestions.append(Suggestion{
                    column,
                    replacement,
                    rule.reason,
                    SuggestionKind::PhraseRule});
            }
        }
    }

    return suggestions;
}

} // namespace milah
