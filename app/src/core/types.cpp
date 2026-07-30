#include "core/types.h"

namespace milah {

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
