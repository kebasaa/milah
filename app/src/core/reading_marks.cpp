#include "core/reading_marks.h"

#include "core/diff.h"

namespace milah {
namespace {

/// Which of the reference reading's graphemes are absent from `other`.
///
/// Indexed by the reference's own grapheme clusters, so an Insert — something
/// `other` reads and the reference does not — contributes nothing: this asks
/// what the reference has lost, not what the other has gained.
QList<bool> missingFrom(const QString &reference, const QString &other)
{
    QList<bool> flags;
    for (const DiffSegment &segment : diffGraphemes(reference, other)) {
        if (segment.op == DiffOp::Insert) {
            continue;
        }
        flags.append(QList<bool>(graphemes(segment.before).size(), segment.op == DiffOp::Delete));
    }
    return flags;
}

/// Appends, joining onto the segment before it when the mark is the same.
///
/// Coalescing here rather than in each renderer is what stops two of them
/// disagreeing about where a marked run begins — and a Delete dropped from
/// between two agreements would otherwise leave two segments where the reader
/// sees one word.
void append(MarkedReading &into, const QString &text, ReadingMark mark)
{
    if (text.isEmpty()) {
        return;
    }
    if (!into.isEmpty() && into.last().mark == mark) {
        into.last().text += text;
        return;
    }
    into.append(MarkedSegment{text, mark});
}

} // namespace

MarkedReading markReferenceReading(const QString &reference, const QStringList &others)
{
    const QStringList clusters = graphemes(reference);

    QList<bool> differs(clusters.size(), false);
    for (const QString &other : others) {
        const QList<bool> flags = missingFrom(reference, other);
        for (int index = 0; index < flags.size() && index < differs.size(); ++index) {
            differs[index] = differs.at(index) || flags.at(index);
        }
    }

    MarkedReading marked;
    for (int index = 0; index < clusters.size(); ++index) {
        append(
            marked,
            clusters.at(index),
            differs.at(index) ? ReadingMark::Missing : ReadingMark::Plain);
    }
    return marked;
}

MarkedReading markVariantReading(const QString &reference, const QString &text)
{
    MarkedReading marked;
    for (const DiffSegment &segment : diffGraphemes(reference, text)) {
        switch (segment.op) {
        case DiffOp::Equal:
            append(marked, segment.after, ReadingMark::Plain);
            break;
        case DiffOp::Insert:
            append(marked, segment.after, ReadingMark::Added);
            break;
        case DiffOp::Delete:
            // What the reference reads here and this witness does not. Drawing
            // it would put a word in this witness's mouth that it never says;
            // the reference's own row is where that absence is shown.
            break;
        }
    }
    return marked;
}

} // namespace milah
