#pragma once

#include "core/types.h"

#include <QList>
#include <QString>

#include <optional>

namespace milah {

/// Collapses runs of whitespace to a single space and trims both ends.
QString collapseWhitespace(const QString &value);

/// Length of `raw`'s first `offset` characters once whitespace runs are
/// collapsed and leading whitespace is dropped. Maps a character offset taken
/// against raw OSIS text onto the collapsed verse text notes are anchored in.
int collapsedPrefixLength(const QString &raw, int offset);

/// The form two readings are compared in: decomposed, stripped of Hebrew
/// pointing and punctuation, recomposed and lowercased.
QString comparisonKey(const QString &text);

/// Splits verse text into tokens and anchors each note to the token it follows.
QList<SourceToken> tokenize(
    const QString &verseId,
    const QString &text,
    const QList<SourceNote> &notes);

/// Rejoins chosen readings into running text, closing up the spacing around
/// punctuation and maqaf.
QString joinTokens(const QList<std::optional<QString>> &tokens);

} // namespace milah
