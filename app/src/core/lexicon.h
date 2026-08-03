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

    /// The Strong's numbers `word` may stand for, likeliest first.
    ///
    /// Two spellings that share one are inflections of the same lemma, which
    /// is what lets the alignment recognise שֶׁנְּתָנוֹ as נתן where no amount of
    /// comparing letters would. Ambiguous forms carry several candidates, so a
    /// shared number is good evidence rather than proof.
    QStringList strongsFor(const QString &word) const;

    /// The Strong's numbers of the roots `word` is built on, where the
    /// dictionary records a derivation.
    ///
    /// Weaker evidence than a shared number: this says the two words are
    /// related, not that they are the same word. מלך "king" derives from מלך
    /// "to reign", but אדם "man" derives from אדם "to be red", and only one of
    /// those pairs belongs in the same column. The chain is walked a bounded
    /// number of steps for exactly that reason — see
    /// hebrew_manuscripts/tools/python/tools/build_roots.py.
    ///
    /// Empty when no roots file was found, which stands the rung down rather
    /// than changing any other answer.
    QStringList rootsFor(const QString &word) const;

    /// True when a roots file was loaded alongside the dictionary.
    bool hasRoots() const { return !m_roots.isEmpty(); }

    /// Overlays a roots file. A path that is empty or unreadable leaves the
    /// lexicon as it was, so the rung stands down instead of failing.
    void loadRoots(const QString &path);

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
    /// A Strong's number to the number of the root it derives from. Read from
    /// a separate file, so an older data directory without one still loads.
    QHash<QString, QString> m_roots;
};

/// The word with its niqqud but without cantillation, which manuscript text
/// rarely carries. Complements comparisonKey(), which drops the points too.
QString pointedKey(const QString &text);

/// The dictionary entry as shown on hovering a Strong's number. Lives here
/// rather than in the widget so that what the reader ends up seeing can be
/// asserted on without a mouse.
QString strongsTooltip(const QList<LexiconEntry> &entries);

} // namespace milah
