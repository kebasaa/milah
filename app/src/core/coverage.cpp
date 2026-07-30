#include "core/coverage.h"

#include "core/books.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>

#include <algorithm>

namespace milah {
namespace {

QString locationKey(const Location &location)
{
    return QStringLiteral("%1.%2").arg(location.book).arg(location.chapter);
}

/// A verse number split into its leading number and whatever follows, so that
/// "10" sorts after "9" and "9a" after "9".
struct VerseKey
{
    bool hasNumber = false;
    int number = 0;
    QString rest;
    QString whole;
};

VerseKey parseVerseKey(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)(.*)$"));

    VerseKey key;
    key.whole = value;
    const QRegularExpressionMatch match = pattern.match(value);
    if (match.hasMatch()) {
        key.hasNumber = true;
        key.number = match.captured(1).toInt();
        key.rest = match.captured(2);
    }
    return key;
}

bool verseKeyLess(const VerseKey &left, const VerseKey &right)
{
    if (left.hasNumber && right.hasNumber) {
        if (left.number != right.number) {
            return left.number < right.number;
        }
        return QString::compare(left.rest, right.rest) < 0;
    }
    if (left.hasNumber != right.hasNumber) {
        // A numbered verse sorts before anything that does not start with a
        // number at all.
        return left.hasNumber;
    }
    return QString::compare(left.whole, right.whole) < 0;
}

} // namespace

QList<Location> commonLocations(const DocumentRefs &manuscripts)
{
    QList<Location> result;
    if (manuscripts.isEmpty()) {
        return result;
    }

    QList<QList<Location>> perSource;
    QList<QSet<QString>> perSourceKeys;
    perSource.reserve(manuscripts.size());
    perSourceKeys.reserve(manuscripts.size());

    for (const SourceDocument *source : manuscripts) {
        QList<Location> locations;
        QSet<QString> keys;
        for (const SourceVerse &verse : source->verses) {
            Location location;
            location.book = verse.reference.book;
            location.chapter = verse.reference.chapter;
            const QString key = locationKey(location);
            if (!keys.contains(key)) {
                keys.insert(key);
                locations.append(location);
            }
        }
        perSource.append(locations);
        perSourceKeys.append(keys);
    }

    for (const Location &location : perSource.first()) {
        const QString key = locationKey(location);
        bool inEvery = true;
        for (int index = 1; index < perSourceKeys.size(); ++index) {
            if (!perSourceKeys.at(index).contains(key)) {
                inEvery = false;
                break;
            }
        }
        if (inEvery) {
            result.append(location);
        }
    }

    std::stable_sort(
        result.begin(),
        result.end(),
        [](const Location &left, const Location &right) {
            const int bookOrder = compareBooks(left.book, right.book);
            if (bookOrder != 0) {
                return bookOrder < 0;
            }
            return left.chapter < right.chapter;
        });

    return result;
}

QStringList verseIdsAtLocation(
    const DocumentRefs &manuscripts,
    const Location &location)
{
    QStringList ids;
    QSet<QString> seen;

    for (const SourceDocument *source : manuscripts) {
        for (const SourceVerse &verse : source->verses) {
            if (verse.reference.book == location.book
                && verse.reference.chapter == location.chapter
                && !seen.contains(verse.reference.id)) {
                seen.insert(verse.reference.id);
                ids.append(verse.reference.id);
            }
        }
    }

    const auto verseNumber = [&manuscripts](const QString &verseId) {
        for (const SourceDocument *source : manuscripts) {
            if (const SourceVerse *verse = source->verse(verseId)) {
                return verse->reference.verse;
            }
        }
        return verseId;
    };

    std::stable_sort(
        ids.begin(),
        ids.end(),
        [&verseNumber](const QString &left, const QString &right) {
            return verseKeyLess(
                parseVerseKey(verseNumber(left)),
                parseVerseKey(verseNumber(right)));
        });

    return ids;
}

} // namespace milah
