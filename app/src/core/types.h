#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace milah {

enum class SourceRole {
    Manuscript,
    Translation,
    Combined,
};

QString sourceRoleToString(SourceRole role);
SourceRole sourceRoleFromString(const QString &value);

struct WorkMetadata
{
    QString workId;
    QString title;
    QString language;
    QString scope;
    /// The `<rights>` line: who holds the copyright and on what terms.
    ///
    /// Read from the file rather than assumed, because the terms differ from
    /// text to text — a collation can hold a CC-licensed transcription and an
    /// "all rights reserved" translation side by side, and the editor working
    /// on them is entitled to see which is which without opening the XML.
    QString rights;
    /// The `<type>` line's attribute: what kind of work this is. A witness and
    /// an edition are different things and the published library says which by
    /// this line — its manuscript files carry `x-manuscript` where its editions
    /// carry `x-bible`.
    ///
    /// Empty means an edition, which is what Milah's own collations are, so a
    /// caller that says nothing gets the file it always got.
    QString workType;
    QMap<QString, QString> identifiers;
};

struct SourceNote
{
    QString id;
    QString number;
    QString text;
    int charOffset = 0;
    int tokenIndex = 0;
};

struct SourceToken
{
    QString id;
    QString text;
    int start = 0;
    int end = 0;
    QList<SourceNote> notes;
};

struct VerseReference
{
    QString id;
    QString book;
    int chapter = 0;
    QString verse;
};

struct SourceVerse
{
    VerseReference reference;
    QString label;
    QString text;
    QList<SourceToken> tokens;
    /// The manuscript's own verse number, when it differs from the canonical one.
    std::optional<QString> altNumber;
};

/// A heading that belongs to no verse: a manuscript incipit, a chapter title,
/// or a division heading. `chapter` is set when the title introduces one.
struct SourceTitle
{
    QString id;
    QString type;
    bool canonical = false;
    QString text;
    std::optional<QString> book;
    std::optional<int> chapter;
    QList<SourceNote> notes;
};

/// A positioned marker such as a folio boundary (`pb`) or a manuscript verse
/// division. `verseId` is unset when the marker falls outside any verse.
struct SourceMilestone
{
    QString id;
    QString type;
    QString n;
    std::optional<QString> verseId;
    int charOffset = 0;
};

/// Verses are kept in document order, because the chapter list and the verse
/// order shown to the reader both depend on it. `verseIndex` maps an osisID to
/// its position in `verses`.
struct SourceDocument
{
    QString id;
    QString name;
    SourceRole role = SourceRole::Manuscript;
    QString rawOsis;
    WorkMetadata metadata;
    QList<SourceVerse> verses;
    QHash<QString, int> verseIndex;
    QList<SourceTitle> titles;
    QList<SourceMilestone> milestones;
    QStringList warnings;

    const SourceVerse *verse(const QString &verseId) const;
    bool hasVerse(const QString &verseId) const;
    void appendVerse(const SourceVerse &verse);
};

/// Short labels naming each source in the verse view, keyed by source id.
/// Taken from the OSIS work id, and unique across the sources given: two
/// witnesses that shorten to the same thing are told apart by a qualifier.
QHash<QString, QString> sourceAcronyms(const QList<SourceDocument> &sources);

/// Whether a language code, as carried in OSIS `xml:lang`, is written right to
/// left. Hebrew, Arabic and Syriac all answer yes without a hand-kept list.
bool isRightToLeft(const QString &language);

/// A column of the alignment grid. A source with no reading in this column is
/// simply absent from `cells`.
struct AlignmentColumn
{
    QString id;
    QHash<QString, SourceToken> cells;

    const SourceToken *cell(const QString &sourceId) const;
};

struct AlignedVerse
{
    VerseReference reference;
    QList<AlignmentColumn> columns;
};

struct ConsensusColumn
{
    std::optional<QString> text;
    std::optional<QString> sourceId;
    /// The reading was picked automatically without a majority behind it and
    /// the editor has not settled it since. Such words are shown muted.
    bool needsReview = false;
};

struct CombinedDraft
{
    VerseReference reference;
    QList<ConsensusColumn> columns;
    std::optional<QString> manualText;
};

enum class SpanConfidence {
    High,
    Low,
};

struct TranslationSpan
{
    QString id;
    QString translationId;
    QString verseId;
    int columnStart = 0;
    int columnEnd = 0;
    int tokenStart = 0;
    int tokenEnd = 0;
    SpanConfidence confidence = SpanConfidence::High;
    /// The editor took this group out of the interlinear. Marked rather than
    /// erased, because a verse whose spans have all gone is regenerated from
    /// the translation, and an erased group would simply come back.
    bool removed = false;
};

struct Location
{
    QString book;
    int chapter = 0;

    bool operator==(const Location &other) const
    {
        return book == other.book && chapter == other.chapter;
    }
};

/// Names a chapter: "Rev.1".
///
/// Deliberately the first two parts of an OSIS verse id, so a verse can find
/// the chapter it belongs to by taking them rather than by looking itself up in
/// every manuscript.
QString locationKey(const Location &location);

struct TranslationAssociation
{
    QString translationId;
    QString manuscriptId;
};

struct ProjectState
{
    QList<SourceDocument> sources;
    QList<TranslationAssociation> associations;
    QString priorityManuscriptId;
    QMap<QString, CombinedDraft> combined;
    QList<TranslationSpan> translationSpans;
    /// Columns the editor divided so the edition can read two words where a
    /// witness writes one, keyed by verse id. Indices are into the columns the
    /// alignment produces, once per extra column; see applyColumnSplits().
    QMap<QString, QList<int>> columnSplits;
    /// The manuscript a chapter is read against where the editor has chosen
    /// one, keyed by locationKey(). A chapter absent from this falls back to
    /// priorityManuscriptId, which is why that never drifts.
    QMap<QString, QString> chapterReferences;
    /// The manuscript a single verse is read against where the editor has said
    /// so, keyed by verse id — an exception to its chapter, which survives the
    /// chapter's reference being changed. A verse absent from both simply
    /// follows the rule in referenceForVerse().
    QMap<QString, QString> verseReferences;
    /// The editor's own remarks on Combined words, keyed "<verseId>:<column>".
    /// Distinct from the manuscripts' notes, which belong to the sources and
    /// are never written here.
    QMap<QString, QString> combinedNotes;
    /// The editor's wording for the interlinear, keyed the same way. A column
    /// absent from this follows the aligned translation instead.
    QMap<QString, QString> interlinearWords;
    std::optional<Location> location;
};

struct ProjectFile
{
    QString path;
    QString contentBase64;
};

struct MilahProjectPayload
{
    QString suggestedName;
    QJsonObject manifest;
    QList<ProjectFile> files;
};

} // namespace milah
