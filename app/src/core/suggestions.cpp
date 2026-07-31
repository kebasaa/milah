#include "core/suggestions.h"

#include "core/data_paths.h"
#include "core/hebrew_forms.h"
// hebrew_forms brings isHebrewLetter, isAbbreviated, hasNiqqud and
// withoutNiqqud, all shared with the alignment so the two cannot disagree
// about what a vowel point or an abbreviation mark is.
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

// The final-form table these checks need lives in core/hebrew_forms.h, shared
// with the alignment's folding so the two cannot disagree about which letters
// have a final shape.

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

void AbbreviationTable::append(const AbbreviationTable &other)
{
    for (auto entry = other.m_entries.constBegin();
         entry != other.m_entries.constEnd();
         ++entry) {
        // The editor's own file wins: it is the more specific of the two.
        m_entries.insert(entry.key(), entry.value());
    }
}

AbbreviationTable AbbreviationTable::fromJson(const QJsonObject &document)
{
    AbbreviationTable table;
    for (const QJsonValue &value :
         document.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();

        // Keyed the same way a token will be, so a stem written pointed or
        // with its abbreviation mark still finds its row.
        const QString stem =
            abbreviationStem(entry.value(QStringLiteral("stem")).toString());

        QStringList expansions;
        for (const QJsonValue &word :
             entry.value(QStringLiteral("expansions")).toArray()) {
            const QString expansion =
                word.toString().normalized(QString::NormalizationForm_C);
            if (!expansion.isEmpty()) {
                expansions.append(expansion);
            }
        }

        if (stem.isEmpty() || expansions.isEmpty()) {
            continue;
        }
        table.m_entries.insert(stem, expansions);
    }
    return table;
}

AbbreviationTable AbbreviationTable::fromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return AbbreviationTable();
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? fromJson(document.object()) : AbbreviationTable();
}

const AbbreviationTable &AbbreviationTable::shared()
{
    static const AbbreviationTable table = [] {
        AbbreviationTable loaded =
            fromFile(locateDataFile(QStringLiteral("hebrew_abbreviations.json")));
        const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!directory.isEmpty()) {
            loaded.append(
                fromFile(directory + QStringLiteral("/hebrew_abbreviations.json")));
        }
        return loaded;
    }();
    return table;
}

QStringList AbbreviationTable::expansionsFor(const QString &rawText) const
{
    // The mark is the whole signal. Without this test the divine name's entry
    // would answer for every bare ה in the corpus.
    if (m_entries.isEmpty() || !isAbbreviated(rawText)) {
        return QStringList();
    }

    const QString stem = abbreviationStem(rawText);
    const auto exact = m_entries.constFind(stem);
    if (exact != m_entries.constEnd()) {
        return exact.value();
    }

    // One agglutinated prefix letter, put back on whatever it resolves to, so
    // לה֞ reads through the same row as ה֞ and comes out לאלהים.
    static const QString prefixes = QStringLiteral("ובכלמהש");
    if (stem.size() >= 2 && prefixes.contains(stem.at(0))) {
        const auto peeled = m_entries.constFind(stem.mid(1));
        if (peeled != m_entries.constEnd()) {
            QStringList prefixed;
            prefixed.reserve(peeled.value().size());
            for (const QString &expansion : peeled.value()) {
                prefixed.append(stem.at(0) + expansion);
            }
            return prefixed;
        }
    }

    return QStringList();
}

QStringList AbbreviationTable::unmarkedExpansionsFor(const QString &rawText) const
{
    if (m_entries.isEmpty() || isAbbreviated(rawText)) {
        return QStringList();
    }

    // One letter and nothing else. Without the mark this is the only signal
    // worth acting on: a lone letter is not a Hebrew word, whereas a longer
    // unmarked word is simply that word.
    const QString stem = abbreviationStem(rawText);
    if (stem.size() != 1 || !isHebrewLetter(stem.at(0))) {
        return QStringList();
    }

    return m_entries.value(stem);
}

AttestedForms AttestedForms::fromFile(const QString &path)
{
    AttestedForms forms;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return forms;
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        // The lists are meant to be read and appended to by hand, so they
        // carry a header explaining where they came from.
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QString key = comparisonKey(line);
        if (!key.isEmpty()) {
            forms.m_keys.insert(key);
        }
    }
    return forms;
}

void AttestedForms::unite(const AttestedForms &other)
{
    m_keys.unite(other.m_keys);
}

bool AttestedForms::contains(const QString &word) const
{
    const QString key = comparisonKey(word);
    return !key.isEmpty() && m_keys.contains(key);
}

const AttestedForms &AttestedForms::shared()
{
    static const AttestedForms forms = [] {
        AttestedForms loaded;
        // Every list in every search path, merged: one may ship with Milah
        // while another is the editor's own.
        for (const QString &directory : dataSearchPaths()) {
            const QDir folder(directory);
            const QStringList names =
                folder.entryList({QStringLiteral("*.words.txt")}, QDir::Files, QDir::Name);
            for (const QString &name : names) {
                loaded.unite(fromFile(folder.filePath(name)));
            }
        }
        return loaded;
    }();
    return forms;
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

namespace {

/// Writes a replacement the way the edition around it is written, so accepting
/// a suggestion does not point an unpointed text a word at a time.
QString spelledLikeTheEdition(const QString &replacement, bool pointed)
{
    return pointed ? replacement : withoutNiqqud(replacement);
}

/// Offers what a scribal abbreviation stands for. The most likely reading is
/// what accepting proposes; the rest are named in the reason, because choosing
/// among the divine names is an editorial decision and not Milah's to make.
std::optional<Suggestion> abbreviation(
    const QString &word,
    int column,
    const AbbreviationTable &table,
    bool pointed)
{
    // Marked first. Failing that, a lone letter may be an abbreviation whose
    // mark was left off -- but only the editor can tell that from a detached
    // definite article, so the two are worded differently.
    const QStringList marked = table.expansionsFor(word);
    const QStringList expansions =
        marked.isEmpty() ? table.unmarkedExpansionsFor(word) : marked;
    if (expansions.isEmpty()) {
        return std::nullopt;
    }

    QStringList offered;
    offered.reserve(expansions.size());
    for (const QString &expansion : expansions) {
        offered.append(spelledLikeTheEdition(expansion, pointed));
    }

    QString reason = marked.isEmpty()
        ? QStringLiteral("A lone %1 is usually the divine name with its mark "
                         "left off, but it may be a detached definite article. "
                         "For %2")
              .arg(abbreviationStem(word), offered.first())
        : QStringLiteral("Written as an abbreviation, for %1").arg(offered.first());
    if (offered.size() > 1) {
        reason += QStringLiteral(" — or %1")
                      .arg(offered.mid(1).join(QStringLiteral(", ")));
    }
    reason += QLatin1Char('.');

    return Suggestion{
        column, offered.first(), reason, SuggestionKind::Abbreviation};
}

} // namespace

bool readingsArePointed(const QList<std::optional<QString>> &words)
{
    int countable = 0;
    int pointed = 0;
    for (const std::optional<QString> &word : words) {
        if (!word.has_value() || word->isEmpty()) {
            continue;
        }
        // A word of one letter cannot show a convention either way, and the
        // abbreviations this has to judge are exactly those.
        int letters = 0;
        for (const QChar character : *word) {
            if (isHebrewLetter(character)) {
                ++letters;
            }
        }
        if (letters < 2) {
            continue;
        }
        ++countable;
        if (hasNiqqud(*word)) {
            ++pointed;
        }
    }

    // Nothing to go on: leave a replacement as its table authored it. Points
    // can be stripped afterwards, but not invented.
    if (countable == 0) {
        return true;
    }
    return pointed * 2 > countable;
}

QList<Suggestion> reviewVerse(
    const QList<std::optional<QString>> &words,
    const HebrewLexicon &lexicon,
    const PhraseRules &rules,
    const QSet<QString> &accepted,
    const AbbreviationTable &abbreviations)
{
    QList<Suggestion> suggestions;
    QSet<int> abbreviated;

    // Settled once for the whole verse: every replacement below comes from a
    // data table written pointed, and has to be written the way this edition
    // is written before it is offered.
    const bool pointed = readingsArePointed(words);

    // Without a lexicon every word is "not attested", which would mark the
    // whole verse and say nothing. The orthography and phrase checks do not
    // depend on it, so only this one stands down.
    const bool canJudgeVocabulary = !lexicon.isEmpty();

    for (int column = 0; column < words.size(); ++column) {
        const std::optional<QString> &word = words.at(column);
        if (!word.has_value() || word->isEmpty()) {
            continue;
        }

        // Before anything else: an abbreviation is not misspelt, and the
        // lexicon will happily recognise the letters it is written with while
        // missing the word entirely — ה֞ looks like a definite article.
        if (const std::optional<Suggestion> found =
                abbreviation(*word, column, abbreviations, pointed)) {
            suggestions.append(*found);
            abbreviated.insert(column);
            continue;
        }

        if (const std::optional<Suggestion> found = orthography(*word, column)) {
            suggestions.append(*found);
            // One flag per word: the spelling has to be settled before it is
            // worth asking whether the lexicon knows it.
            continue;
        }

        const QString key = comparisonKey(*word);
        if (!canJudgeVocabulary || key.isEmpty()) {
            continue;
        }
        // The Hebrew Bible first: it is the authority, and a word it holds is
        // settled without consulting anything else. Only what it does not have
        // is put to the shipped corpora and the editor's own dictionary, which
        // say a word is attested somewhere, not that it is biblical.
        if (lexicon.knows(*word) || accepted.contains(key)) {
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
                if (rule.replace.at(offset).isEmpty()) {
                    continue;
                }
                // Spelled to match the edition before the comparison, not
                // after: an unpointed edition already reading ישוע would
                // otherwise keep being offered the pointed יֵשׁוּעַ as a change.
                const QString replacement =
                    spelledLikeTheEdition(rule.replace.at(offset), pointed);
                if (replacement == *words.at(column)) {
                    continue;
                }
                // A rule matched on the skeleton cannot see the abbreviation
                // mark, so it reads יש֞ו as the bare ישו and offers its own
                // reason for it — which for an abbreviation is the wrong
                // reason. The expansion already said the useful thing.
                if (abbreviated.contains(column)) {
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
