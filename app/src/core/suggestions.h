#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

class QJsonObject;

namespace milah {

class HebrewLexicon;

/// What a scribal abbreviation may be short for.
///
/// Kept apart from PhraseRules because the two are matched differently. A
/// phrase rule is keyed on `comparisonKey`, which strips the very marks that
/// say a word is abbreviated: a rule written for ה֞ would collapse to ה and
/// fire on every definite article in the corpus. These entries are reached
/// only for a token whose own text still carries the mark.
///
/// Prefixed spellings share one entry. `לה֞` and `וה֞` peel their prefix
/// letter, look up `ה`, and get it back on the front of each expansion.
class AbbreviationTable
{
public:
    AbbreviationTable() = default;

    /// The bundled table, plus anything the editor has added alongside it.
    static const AbbreviationTable &shared();
    static AbbreviationTable fromJson(const QJsonObject &document);
    static AbbreviationTable fromFile(const QString &path);

    /// What `rawText` may stand for, most likely first, or empty when it is
    /// not an abbreviation or is one the table does not know.
    QStringList expansionsFor(const QString &rawText) const;

    /// What a token carrying no abbreviation mark may stand for.
    ///
    /// Answers only for a token that is a single Hebrew letter. A lone ה is
    /// very often the divine name with its mark left off — Cochin's James
    /// writes it that way ten times — but a longer unmarked word is simply
    /// that word, and a bare ישו is the form the phrase rules already speak to.
    ///
    /// Never consulted by the alignment, and it must stay that way. In Sloane
    /// 237 a lone ה is a detached definite article — מְנוֹרוֹת ה הַזָּהָב — so
    /// acting on this mechanically would drag two innocent articles into the
    /// divine name's column. It exists to raise a question with the editor,
    /// not to settle one.
    QStringList unmarkedExpansionsFor(const QString &rawText) const;

    bool isEmpty() const { return m_entries.isEmpty(); }
    void append(const AbbreviationTable &other);

private:
    /// Stem letters to expansions, in the order the file gives them.
    QHash<QString, QStringList> m_entries;
};

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
    /// Written as a scribal abbreviation. What it stands for is the editor's
    /// call — ה֞ is the divine name, but which of its names is a decision
    /// about the edition, so every reading the table knows is offered.
    Abbreviation,
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

/// True when the readings of one Combined verse are written with vowel points.
///
/// A majority of the words that could show pointing at all — two Hebrew letters
/// or more. One pointed word among unpointed ones is a witness reading that won
/// its column rather than a change of convention, so "any" would be wrong.
///
/// A verse with nothing to go on answers true, which leaves a replacement as
/// its table authored it: points can be stripped later but not invented.
///
/// This is what decides whether accepting a suggestion writes אֱלֹהִים or
/// אלהים, so that the edition keeps one spelling convention throughout.
bool readingsArePointed(const QList<std::optional<QString>> &words);

/// Reviews one verse's Combined words. `accepted` holds comparison keys the
/// editor has waved through.
QList<Suggestion> reviewVerse(
    const QList<std::optional<QString>> &words,
    const HebrewLexicon &lexicon,
    const PhraseRules &rules,
    const QSet<QString> &accepted = QSet<QString>(),
    const AbbreviationTable &abbreviations = AbbreviationTable());

} // namespace milah
