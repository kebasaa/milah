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
