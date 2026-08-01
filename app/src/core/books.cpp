#include "books.h"

#include <QHash>

#include <limits>

namespace milah {
namespace {

/// The canon in order, each OSIS id beside the name a reader knows it by. One
/// table for both, so an ordering and a name can never disagree about which
/// book is meant.
struct BookEntry
{
    const char *osisId;
    const char *name;
};

const BookEntry kBooks[] = {
    {"Gen", "Genesis"},
    {"Exod", "Exodus"},
    {"Lev", "Leviticus"},
    {"Num", "Numbers"},
    {"Deut", "Deuteronomy"},
    {"Josh", "Joshua"},
    {"Judg", "Judges"},
    {"Ruth", "Ruth"},
    {"1Sam", "1 Samuel"},
    {"2Sam", "2 Samuel"},
    {"1Kgs", "1 Kings"},
    {"2Kgs", "2 Kings"},
    {"1Chr", "1 Chronicles"},
    {"2Chr", "2 Chronicles"},
    {"Ezra", "Ezra"},
    {"Neh", "Nehemiah"},
    {"Esth", "Esther"},
    {"Job", "Job"},
    {"Ps", "Psalms"},
    {"Prov", "Proverbs"},
    {"Eccl", "Ecclesiastes"},
    {"Song", "Song of Songs"},
    {"Isa", "Isaiah"},
    {"Jer", "Jeremiah"},
    {"Lam", "Lamentations"},
    {"Ezek", "Ezekiel"},
    {"Dan", "Daniel"},
    {"Hos", "Hosea"},
    {"Joel", "Joel"},
    {"Amos", "Amos"},
    {"Obad", "Obadiah"},
    {"Jonah", "Jonah"},
    {"Mic", "Micah"},
    {"Nah", "Nahum"},
    {"Hab", "Habakkuk"},
    {"Zeph", "Zephaniah"},
    {"Hag", "Haggai"},
    {"Zech", "Zechariah"},
    {"Mal", "Malachi"},
    {"Matt", "Matthew"},
    {"Mark", "Mark"},
    {"Luke", "Luke"},
    {"John", "John"},
    {"Acts", "Acts"},
    {"Rom", "Romans"},
    {"1Cor", "1 Corinthians"},
    {"2Cor", "2 Corinthians"},
    {"Gal", "Galatians"},
    {"Eph", "Ephesians"},
    {"Phil", "Philippians"},
    {"Col", "Colossians"},
    {"1Thess", "1 Thessalonians"},
    {"2Thess", "2 Thessalonians"},
    {"1Tim", "1 Timothy"},
    {"2Tim", "2 Timothy"},
    {"Titus", "Titus"},
    {"Phlm", "Philemon"},
    {"Heb", "Hebrews"},
    {"Jas", "James"},
    {"1Pet", "1 Peter"},
    {"2Pet", "2 Peter"},
    {"1John", "1 John"},
    {"2John", "2 John"},
    {"3John", "3 John"},
    {"Jude", "Jude"},
    {"Rev", "Revelation"},
};

constexpr int kBookCount = int(sizeof(kBooks) / sizeof(kBooks[0]));

const QHash<QString, int> &canonicalOrder()
{
    static const QHash<QString, int> order = [] {
        QHash<QString, int> result;
        result.reserve(kBookCount);
        for (int index = 0; index < kBookCount; ++index) {
            result.insert(QString::fromLatin1(kBooks[index].osisId), index);
        }
        return result;
    }();

    return order;
}

/// What two ways of writing the same book have in common: the letters and
/// digits, folded to one case. "1 Chronicles", "1chronicles" and "1Chr" all come
/// down to something a lookup can match on, and a transcriber typing quickly is
/// not asked to get the spacing right.
QString lookupKey(const QString &text)
{
    QString key;
    key.reserve(text.size());
    for (const QChar character : text) {
        if (character.isLetterOrNumber()) {
            key.append(character.toLower());
        }
    }
    return key;
}

} // namespace

QStringList bookIds()
{
    static const QStringList ids = [] {
        QStringList result;
        result.reserve(kBookCount);
        for (int index = 0; index < kBookCount; ++index) {
            result.append(QString::fromLatin1(kBooks[index].osisId));
        }
        return result;
    }();

    return ids;
}

QStringList bookNames()
{
    static const QStringList names = [] {
        QStringList result;
        result.reserve(kBookCount);
        for (int index = 0; index < kBookCount; ++index) {
            result.append(QString::fromLatin1(kBooks[index].name));
        }
        return result;
    }();

    return names;
}

QString bookIdFor(const QString &nameOrId)
{
    static const QHash<QString, QString> byKey = [] {
        QHash<QString, QString> result;
        result.reserve(kBookCount * 2);
        // Ids first, and a name never overwrites one. The two columns key alike
        // for several books — "1 John" and "1John" reduce to the same thing —
        // and where they would ever disagree about which book a key means, the
        // id is the one the rest of Milah addresses verses by.
        for (int index = 0; index < kBookCount; ++index) {
            const QString id = QString::fromLatin1(kBooks[index].osisId);
            result.insert(lookupKey(id), id);
        }
        for (int index = 0; index < kBookCount; ++index) {
            const QString id = QString::fromLatin1(kBooks[index].osisId);
            const QString key = lookupKey(QString::fromLatin1(kBooks[index].name));
            if (!result.contains(key)) {
                result.insert(key, id);
            }
        }
        return result;
    }();

    return byKey.value(lookupKey(nameOrId));
}

QString bookName(const QString &osisId)
{
    static const QHash<QString, QString> names = [] {
        QHash<QString, QString> result;
        result.reserve(kBookCount);
        for (int index = 0; index < kBookCount; ++index) {
            result.insert(
                QString::fromLatin1(kBooks[index].osisId),
                QString::fromLatin1(kBooks[index].name));
        }
        return result;
    }();

    // A manuscript may carry something outside the canon, and an id names its
    // book better than an empty string does.
    return names.value(osisId, osisId);
}

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
