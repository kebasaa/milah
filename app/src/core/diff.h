#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace milah {

enum class DiffOp {
    Equal,
    Delete, ///< Present in the first reading only.
    Insert, ///< Present in the second reading only.
};

/// A stretch of two readings lined up against each other. `before` and `after`
/// are both set for Equal, and only one of them for Delete and Insert. An
/// Equal segment can still differ in pointing, which is why both sides are
/// carried: each row is drawn from its own text.
struct DiffSegment
{
    DiffOp op = DiffOp::Equal;
    QString before;
    QString after;
};

/// Splits `value` into grapheme clusters, so a Hebrew letter keeps its
/// pointing instead of being compared mark by mark.
QStringList graphemes(const QString &value);

/// Character-level difference between two readings, taken over grapheme
/// clusters compared by their unpointed form: pointing is not a variant.
/// Adjacent segments of the same kind are merged.
QList<DiffSegment> diffGraphemes(const QString &before, const QString &after);

} // namespace milah
