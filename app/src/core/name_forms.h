#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QJsonObject;

namespace milah {

/// One proper name, however the witnesses spell it.
///
/// The spellings themselves are not held here — they are the keys of the index
/// that finds this group. What a group carries is its identity and what the
/// edition should call it.
struct NameGroup
{
    /// Stable, ASCII, unique across the table: "john", "laodicea". Not shown to
    /// anyone. It is what a test asserts on, and what would catch John being
    /// filed under Jesus — an assertion that a spelling merely "resolves" would
    /// pass just as happily with the groups swapped.
    QString id;
    /// The form to offer the editor, written pointed. Points are stripped when
    /// the edition around it has none; they cannot be invented the other way.
    QString preferred;
    /// Why, in the editor's language. Shown as the suggestion's reason.
    QString note;
    /// Every spelling this group claims, already folded — the preferred form
    /// among them, so a witness reading the Hebrew name needs no separate
    /// listing.
    ///
    /// Held on the group rather than only in the index because a merge has to
    /// be able to rebuild the index from the groups: a replacing group may
    /// have dropped a spelling, and an index that kept it would go on
    /// resolving a form its group no longer claims.
    QStringList keys;
};

/// Which spellings are the same name.
///
/// The one thing in Milah that can say יאהנניס and יוֹחָנָן are one word. No
/// measure of similarity reaches that: four edits over a seven-letter word, no
/// Strong's number, no shared skeleton. These manuscripts write New Testament
/// names as Greek transliterations where other witnesses write the Hebrew, and
/// the relation between the two is historical rather than orthographic — which
/// is to say it has to be asserted by somebody who knows, not computed.
///
/// So this is a curated table and is meant to stay small. It is consulted by
/// the alignment, where a group outranks a shared lexicon number, and by the
/// suggestions, where it offers the Hebrew form. A wrong group is therefore
/// expensive: see the note on kScoreName in align_score.h.
class NameForms
{
public:
    NameForms() = default;

    /// The shipped table, with the editor's own file merged over it.
    static const NameForms &shared();
    static NameForms fromJson(const QJsonObject &document);
    /// An unreadable or absent file is an empty table, not an error: a missing
    /// name table costs recognition, not correctness.
    static NameForms fromFile(const QString &path);

    /// Which group an already-folded key belongs to, or -1.
    ///
    /// For the alignment, which holds a foldedKey() by the time it scores and
    /// must not re-normalise inside the matrix.
    int groupForFoldedKey(const QString &foldedText) const;

    /// Which group `rawText` belongs to, or -1. `peeled`, when given, receives
    /// the prefix letters taken off the front — empty when none were, so a
    /// caller can put them back on whatever it offers.
    int groupFor(const QString &rawText, QString *peeled = nullptr) const;

    const NameGroup *group(int index) const;
    const QList<NameGroup> &groups() const { return m_groups; }
    /// Every spelling the table knows, folded. For tests and diagnostics.
    QList<QString> keys() const { return m_index.keys(); }

    bool isEmpty() const { return m_groups.isEmpty(); }

    /// Merges `other` over this one, **replacing** a group whose id is already
    /// held rather than adding to it.
    ///
    /// Replace and not append, because a group is a set of spellings and an
    /// editor who finds a wrong one in the shipped table has to be able to take
    /// it out. Concatenation cannot express a removal.
    void append(const NameForms &other);

private:
    /// Rebuilds the lookup from the groups. First claim wins, so a spelling two
    /// groups both name belongs to the one that named it first, and the table
    /// never has to arbitrate at lookup time.
    void reindex();

    QList<NameGroup> m_groups;
    /// foldedKey -> index into m_groups.
    QHash<QString, int> m_index;
};

} // namespace milah
