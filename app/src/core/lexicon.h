#pragma once

#include <QHash>
#include <QList>
#include <QString>

class QJsonObject;

namespace milah {

/// One dictionary entry: Strong's own text, and the abridged
/// Brown-Driver-Briggs that STEPBible keys to the same number.
struct LexiconEntry
{
    QString strongs;
    QString lemma;
    QString transliteration;
    QString pronunciation;
    QString derivation;
    /// Strong's definition.
    QString gloss;
    /// Strong's list of KJV renderings.
    QString kjvUsage;
    /// The brief gloss Tyndale scholars give the word.
    QString briefGloss;
    /// Abridged BDB, its senses one per line.
    QString meaning;
    /// Part of speech, in STEPBible's notation — "H:N-M" for a masculine noun.
    QString morphology;
};

/// Strong's numbers for Hebrew words, looked up by the form as written.
///
/// The index is built from the Westminster Leningrad Codex, so it covers
/// biblical Hebrew. A Hebrew New Testament also carries proper nouns,
/// loanwords and later coinages that are simply not in it: a miss means "not
/// in the Tanakh", never "not a word". Nothing is guessed.
class HebrewLexicon
{
public:
    HebrewLexicon() = default;

    /// The bundled index, parsed on first use and kept for the session.
    static const HebrewLexicon &shared();
    /// Reads an index from a JSON file, for tests and for a replacement index.
    static HebrewLexicon fromFile(const QString &path);
    static HebrewLexicon fromJson(const QJsonObject &document);

    bool isEmpty() const { return m_entries.isEmpty(); }

    /// Candidate entries for `word`, likeliest first, empty when unknown.
    QList<LexiconEntry> lookup(const QString &word) const;
    bool knows(const QString &word) const;

private:
    /// The numbers `word` may stand for, as a space-separated list.
    QString numbersFor(const QString &word) const;

    /// Forms outnumber entries a hundredfold, so the candidate numbers are
    /// held as one joined string per form and split only when a word is
    /// actually looked up: a QStringList per form costs far more than the
    /// occasional split.
    QHash<QString, QString> m_pointed;
    QHash<QString, QString> m_forms;
    QHash<QString, LexiconEntry> m_entries;
};

/// The word with its niqqud but without cantillation, which manuscript text
/// rarely carries. Complements comparisonKey(), which drops the points too.
QString pointedKey(const QString &text);

/// The dictionary entry as shown on hovering a Strong's number. Lives here
/// rather than in the widget so that what the reader ends up seeing can be
/// asserted on without a mouse.
QString strongsTooltip(const QList<LexiconEntry> &entries);

} // namespace milah
