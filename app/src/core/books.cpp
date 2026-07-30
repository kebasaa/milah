#include "books.h"

#include <QHash>

#include <limits>

namespace milah {
namespace {

const QHash<QString, int> &canonicalOrder()
{
    static const QHash<QString, int> order = [] {
        static const char *const books[] = {
            "Gen", "Exod", "Lev", "Num", "Deut", "Josh", "Judg", "Ruth",
            "1Sam", "2Sam", "1Kgs", "2Kgs", "1Chr", "2Chr", "Ezra", "Neh",
            "Esth", "Job", "Ps", "Prov", "Eccl", "Song", "Isa", "Jer", "Lam",
            "Ezek", "Dan", "Hos", "Joel", "Amos", "Obad", "Jonah", "Mic",
            "Nah", "Hab", "Zeph", "Hag", "Zech", "Mal", "Matt", "Mark",
            "Luke", "John", "Acts", "Rom", "1Cor", "2Cor", "Gal", "Eph",
            "Phil", "Col", "1Thess", "2Thess", "1Tim", "2Tim", "Titus",
            "Phlm", "Heb", "Jas", "1Pet", "2Pet", "1John", "2John", "3John",
            "Jude", "Rev",
        };

        QHash<QString, int> result;
        const int count = int(sizeof(books) / sizeof(books[0]));
        result.reserve(count);
        for (int index = 0; index < count; ++index) {
            result.insert(QString::fromLatin1(books[index]), index);
        }
        return result;
    }();

    return order;
}

} // namespace

int compareBooks(const QString &left, const QString &right)
{
    const int unknown = std::numeric_limits<int>::max();
    const int leftOrder = canonicalOrder().value(left, unknown);
    const int rightOrder = canonicalOrder().value(right, unknown);

    if (leftOrder != rightOrder) {
        return leftOrder < rightOrder ? -1 : 1;
    }

    return QString::compare(left, right);
}

int compareNumericAware(const QString &left, const QString &right)
{
    int leftIndex = 0;
    int rightIndex = 0;

    while (leftIndex < left.size() && rightIndex < right.size()) {
        const QChar leftChar = left.at(leftIndex);
        const QChar rightChar = right.at(rightIndex);

        if (leftChar.isDigit() && rightChar.isDigit()) {
            int leftEnd = leftIndex;
            while (leftEnd < left.size() && left.at(leftEnd).isDigit()) {
                ++leftEnd;
            }
            int rightEnd = rightIndex;
            while (rightEnd < right.size() && right.at(rightEnd).isDigit()) {
                ++rightEnd;
            }

            // Compare the runs as numbers, ignoring leading zeros, and only
            // fall back to their written form when the values are equal.
            const QStringView leftRun =
                QStringView(left).mid(leftIndex, leftEnd - leftIndex);
            const QStringView rightRun =
                QStringView(right).mid(rightIndex, rightEnd - rightIndex);

            int leftStart = 0;
            while (leftStart < leftRun.size() - 1 && leftRun.at(leftStart) == u'0') {
                ++leftStart;
            }
            int rightStart = 0;
            while (rightStart < rightRun.size() - 1 && rightRun.at(rightStart) == u'0') {
                ++rightStart;
            }

            const QStringView leftDigits = leftRun.mid(leftStart);
            const QStringView rightDigits = rightRun.mid(rightStart);

            if (leftDigits.size() != rightDigits.size()) {
                return leftDigits.size() < rightDigits.size() ? -1 : 1;
            }
            const int digitCompare = leftDigits.compare(rightDigits);
            if (digitCompare != 0) {
                return digitCompare < 0 ? -1 : 1;
            }

            leftIndex = leftEnd;
            rightIndex = rightEnd;
            continue;
        }

        if (leftChar != rightChar) {
            return leftChar < rightChar ? -1 : 1;
        }

        ++leftIndex;
        ++rightIndex;
    }

    const int leftRemaining = left.size() - leftIndex;
    const int rightRemaining = right.size() - rightIndex;
    if (leftRemaining == rightRemaining) {
        return 0;
    }
    return leftRemaining < rightRemaining ? -1 : 1;
}

} // namespace milah
