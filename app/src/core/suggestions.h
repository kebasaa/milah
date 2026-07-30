#pragma once

#include <QList>
#include <QSet>
#include <QString>

#include <optional>

class QJsonObject;

namespace milah {

class HebrewLexicon;

enum class SuggestionKind {
    /// Malformed for reasons that need no linguistic judgement: a letter in
    /// the wrong form for its position, a stray or doubled point.
    Orthography,
    /// Not attested in the Hebrew Bible. Says nothing about whether the word
    /// is right — a Hebrew New Testament is full of words the Tanakh has not.
    UnknownForm,
    /// Matched a rule from the phrase table, which encodes an editorial
    /// opinion rather than a fact.
    PhraseRule,
};

/// Something worth the editor's attention in one Combined word. Never applied
/// on its own: `replacement` is what would be written if it were accepted, and
/// is empty when there is nothing to propose.
struct Suggestion
{
    int column = -1;
    QString replacement;
    QString reason;
    SuggestionKind kind = SuggestionKind::Orthography;
};

/// A sequence of words that should read differently, matched on the
/// consonantal skeleton so pointing does not have to agree.
struct PhraseRule
{
    /// Comparison keys, one per consecutive column.
    QStringList match;
    /// What each matched word becomes; an empty entry leaves that word alone.
    QStringList replace;
    QString reason;
};

class PhraseRules
{
public:
    PhraseRules() = default;

    /// The bundled table, plus any rules the editor has added alongside it.
    static const PhraseRules &shared();
    static PhraseRules fromJson(const QJsonObject &document);
    static PhraseRules fromFile(const QString &path);

    const QList<PhraseRule> &rules() const { return m_rules; }
    bool isEmpty() const { return m_rules.isEmpty(); }
    void append(const PhraseRules &other);

private:
    QList<PhraseRule> m_rules;
};

/// Words the editor has said are fine, held by comparison key so a word is
/// silenced however it happens to be pointed.
class UserDictionary
{
public:
    UserDictionary() = default;
    explicit UserDictionary(const QString &path);

    /// The file this editor's accepted words live in, outside any project.
    static QString defaultPath();

    bool contains(const QString &word) const;
    /// Appends the word and rewrites the file. Returns false if it could not
    /// be written, so the caller can say so rather than silently forgetting.
    bool add(const QString &word);
    const QSet<QString> &keys() const { return m_keys; }

private:
    QString m_path;
    QSet<QString> m_keys;
};

/// Forms attested in a corpus Milah ships, held by comparison key.
///
/// The lexicon covers the Hebrew Bible, so on its own the unknown-word check
/// fires on the post-biblical vocabulary a Hebrew New Testament is full of.
/// This is the register those manuscripts inhabit. A word being here says it
/// is attested somewhere, not that it carries a Strong's number.
///
/// Every `*.words.txt` in the data directories is read and merged, so another
/// corpus — or a list of one's own — needs a file, not a rebuild.
class AttestedForms
{
public:
    AttestedForms() = default;

    static const AttestedForms &shared();
    /// Reads one list. Blank lines and lines opening with `#` are ignored.
    static AttestedForms fromFile(const QString &path);

    bool contains(const QString &word) const;
    const QSet<QString> &keys() const { return m_keys; }
    bool isEmpty() const { return m_keys.isEmpty(); }
    void unite(const AttestedForms &other);

private:
    QSet<QString> m_keys;
};

/// Reviews one verse's Combined words. `accepted` holds comparison keys the
/// editor has waved through.
QList<Suggestion> reviewVerse(
    const QList<std::optional<QString>> &words,
    const HebrewLexicon &lexicon,
    const PhraseRules &rules,
    const QSet<QString> &accepted = QSet<QString>());

} // namespace milah
