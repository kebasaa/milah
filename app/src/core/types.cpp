#include "core/types.h"

#include <QLocale>
#include <QSet>

namespace milah {
namespace {

/// Longest shelfmark still short enough to name a row. "MS.Oo.1.16.2" passes;
/// "British Library, Sloane MS 237" does not.
constexpr int LongestUsableShelfmark = 20;

/// The work id up to its first underscore: Sloane237_REV_Hebrew -> Sloane237.
/// Manuscripts without one fall back through their shelfmark and file name, so
/// a row is never left nameless.
QString baseAcronym(const SourceDocument &source)
{
    if (!source.metadata.workId.isEmpty()) {
        return source.metadata.workId.section(QLatin1Char('_'), 0, 0);
    }

    const QString shelfmark =
        source.metadata.identifiers.value(QStringLiteral("x-shelfmark"));
    if (!shelfmark.isEmpty() && shelfmark.size() <= LongestUsableShelfmark) {
        return shelfmark;
    }

    if (!source.name.isEmpty()) {
        const int extension = source.name.lastIndexOf(QLatin1Char('.'));
        return extension > 0 ? source.name.left(extension) : source.name;
    }

    return source.id;
}

/// What tells two witnesses of the same manuscript apart — the last segment of
/// the work id, so Ebr530_JOH_Hebrew_Consonantal contributes "Consonantal".
QString acronymQualifier(const SourceDocument &source)
{
    const QString workId = source.metadata.workId;
    if (!workId.contains(QLatin1Char('_'))) {
        return QString();
    }
    return workId.section(QLatin1Char('_'), -1);
}

} // namespace

QHash<QString, QString> sourceAcronyms(const QList<SourceDocument> &sources)
{
    QStringList bases;
    QHash<QString, int> shared;
    bases.reserve(sources.size());
    for (const SourceDocument &source : sources) {
        bases.append(baseAcronym(source));
        shared[bases.constLast()] += 1;
    }

    QHash<QString, QString> result;
    QSet<QString> taken;
    for (int index = 0; index < sources.size(); ++index) {
        QString label = bases.at(index);
        if (shared.value(label) > 1) {
            const QString qualifier = acronymQualifier(sources.at(index));
            if (!qualifier.isEmpty()) {
                label = QStringLiteral("%1 %2").arg(label, qualifier);
            }
        }

        QString unique = label;
        for (int suffix = 2; taken.contains(unique); ++suffix) {
            unique = QStringLiteral("%1 %2").arg(label).arg(suffix);
        }
        taken.insert(unique);
        result.insert(sources.at(index).id, unique);
    }

    return result;
}

bool isRightToLeft(const QString &language)
{
    // Resolved through the language code rather than QLocale(QString), which
    // silently falls back to the system locale for anything it cannot parse —
    // and would then call an unknown tag right-to-left on an Arabic desktop.
    const QLocale::Language resolved = QLocale::codeToLanguage(language);
    if (resolved == QLocale::AnyLanguage) {
        return false;
    }
    return QLocale(resolved).textDirection() == Qt::RightToLeft;
}

QString sourceRoleToString(SourceRole role)
{
    switch (role) {
    case SourceRole::Translation:
        return QStringLiteral("translation");
    case SourceRole::Combined:
        return QStringLiteral("combined");
    case SourceRole::Manuscript:
        break;
    }
    return QStringLiteral("manuscript");
}

SourceRole sourceRoleFromString(const QString &value)
{
    if (value == QLatin1String("translation")) {
        return SourceRole::Translation;
    }
    if (value == QLatin1String("combined")) {
        return SourceRole::Combined;
    }
    return SourceRole::Manuscript;
}

const SourceVerse *SourceDocument::verse(const QString &verseId) const
{
    const auto position = verseIndex.constFind(verseId);
    if (position == verseIndex.constEnd()) {
        return nullptr;
    }
    return &verses.at(*position);
}

bool SourceDocument::hasVerse(const QString &verseId) const
{
    return verseIndex.contains(verseId);
}

void SourceDocument::appendVerse(const SourceVerse &verse)
{
    verseIndex.insert(verse.reference.id, int(verses.size()));
    verses.append(verse);
}

const SourceToken *AlignmentColumn::cell(const QString &sourceId) const
{
    const auto position = cells.constFind(sourceId);
    if (position == cells.constEnd()) {
        return nullptr;
    }
    return &position.value();
}

} // namespace milah
