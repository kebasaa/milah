#include "core/diff.h"

#include <QRegularExpression>
#include <QTextBoundaryFinder>

namespace milah {
namespace {

const QRegularExpression &combiningMarks()
{
    static const QRegularExpression expression(
        QStringLiteral("\\p{M}"),
        QRegularExpression::UseUnicodePropertiesOption);
    return expression;
}

/// The form two clusters are matched in. Mirrors comparisonKey() for a single
/// grapheme: pointing is dropped so a pointed letter matches its bare form.
QString clusterKey(const QString &cluster)
{
    QString result = cluster.normalized(QString::NormalizationForm_D);
    result.remove(combiningMarks());
    return result.normalized(QString::NormalizationForm_C).toLower();
}

void appendSegment(
    QList<DiffSegment> &segments,
    DiffOp op,
    const QString &before,
    const QString &after)
{
    if (!segments.isEmpty() && segments.last().op == op) {
        segments.last().before += before;
        segments.last().after += after;
        return;
    }
    segments.append(DiffSegment{op, before, after});
}

} // namespace

QStringList graphemes(const QString &value)
{
    QStringList result;
    if (value.isEmpty()) {
        return result;
    }

    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, value);
    int start = 0;
    while (true) {
        const int next = finder.toNextBoundary();
        if (next < 0) {
            break;
        }
        if (next > start) {
            result.append(value.mid(start, next - start));
            start = next;
        }
    }
    if (start < value.size()) {
        result.append(value.mid(start));
    }
    return result;
}

QList<DiffSegment> diffGraphemes(const QString &before, const QString &after)
{
    const QStringList a = graphemes(before);
    const QStringList b = graphemes(after);
    const int rows = int(a.size());
    const int columns = int(b.size());

    QStringList aKeys;
    aKeys.reserve(rows);
    for (const QString &cluster : a) {
        aKeys.append(clusterKey(cluster));
    }
    QStringList bKeys;
    bKeys.reserve(columns);
    for (const QString &cluster : b) {
        bKeys.append(clusterKey(cluster));
    }

    // Longest common subsequence lengths, filled from the end so the walk
    // below can take the greedy step at each cell.
    QList<int> lcs((rows + 1) * (columns + 1), 0);
    const auto at = [columns](int row, int column) { return row * (columns + 1) + column; };
    for (int row = rows - 1; row >= 0; --row) {
        for (int column = columns - 1; column >= 0; --column) {
            lcs[at(row, column)] = aKeys.at(row) == bKeys.at(column)
                ? lcs.at(at(row + 1, column + 1)) + 1
                : std::max(lcs.at(at(row + 1, column)), lcs.at(at(row, column + 1)));
        }
    }

    QList<DiffSegment> segments;
    int row = 0;
    int column = 0;
    while (row < rows && column < columns) {
        if (aKeys.at(row) == bKeys.at(column)) {
            appendSegment(segments, DiffOp::Equal, a.at(row), b.at(column));
            ++row;
            ++column;
        } else if (lcs.at(at(row + 1, column)) >= lcs.at(at(row, column + 1))) {
            appendSegment(segments, DiffOp::Delete, a.at(row), QString());
            ++row;
        } else {
            appendSegment(segments, DiffOp::Insert, QString(), b.at(column));
            ++column;
        }
    }
    for (; row < rows; ++row) {
        appendSegment(segments, DiffOp::Delete, a.at(row), QString());
    }
    for (; column < columns; ++column) {
        appendSegment(segments, DiffOp::Insert, QString(), b.at(column));
    }

    return segments;
}

} // namespace milah
