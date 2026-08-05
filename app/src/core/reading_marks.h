#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace milah {

/// What a stretch of one witness's reading is, held against the witness the
/// verse is read against.
enum class ReadingMark {
    Plain,   ///< The reference and this witness agree here.
    Missing, ///< The reference reads this and some other witness does not.
    Added,   ///< This witness reads this and the reference does not.
};

/// A stretch of one reading and how it stands.
///
/// The text is always the reading's own characters, never the other's: a marked
/// reading still reads as itself, which is the whole reason a collation shows
/// every witness rather than one text and a list of departures from it.
struct MarkedSegment
{
    QString text;
    ReadingMark mark = ReadingMark::Plain;
};

/// One reading, cut into as few segments as its marks allow.
using MarkedReading = QList<MarkedSegment>;

/// The reference witness's own reading, marked Missing wherever a grapheme is
/// absent from *any* of `others`.
///
/// A witness that reads nothing here is given as an empty string and marks the
/// whole reading: silence is a variant. Which witnesses count is the caller's
/// question — one that does not reach this verse at all is not silence, it is
/// absence, and passing it in would mark every word of the verse.
MarkedReading markReferenceReading(const QString &reference, const QStringList &others);

/// Another witness's reading held against the reference's: what it adds is
/// marked Added, what the two share is Plain, and what the reference reads and
/// this witness does not is simply not there to draw.
MarkedReading markVariantReading(const QString &reference, const QString &text);

} // namespace milah
