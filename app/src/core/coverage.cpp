#include "core/coverage.h"

#include "core/books.h"

#include <QHash>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>

#include <algorithm>

namespace milah {
namespace {

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

QList<LocationCoverage> coveredLocations(const DocumentRefs &manuscripts)
{
    QList<LocationCoverage> result;
    if (manuscripts.isEmpty()) {
        return result;
    }

    // Counted rather than collected per source: what matters afterwards is how
    // many witnesses reach a place, not which list it came from.
    QHash<QString, int> reachedBy;
    QHash<QString, int> position;

    for (const SourceDocument *source : manuscripts) {
        QSet<QString> seenInThisSource;
        for (const SourceVerse &verse : source->verses) {
            Location location;
            location.book = verse.reference.book;
            location.chapter = verse.reference.chapter;
            const QString key = locationKey(location);
            if (seenInThisSource.contains(key)) {
                continue;
            }
            seenInThisSource.insert(key);

            if (!position.contains(key)) {
                position.insert(key, int(result.size()));
                LocationCoverage coverage;
                coverage.location = location;
                result.append(coverage);
            }
            reachedBy[key] += 1;
        }
    }

    for (LocationCoverage &coverage : result) {
        const QString key = locationKey(coverage.location);
        coverage.sourceCount = reachedBy.value(key);
        coverage.complete = coverage.sourceCount == int(manuscripts.size());
    }

    std::stable_sort(
        result.begin(),
        result.end(),
        [](const LocationCoverage &left, const LocationCoverage &right) {
            const int bookOrder = compareBooks(left.location.book, right.location.book);
            if (bookOrder != 0) {
                return bookOrder < 0;
            }
            return left.location.chapter < right.location.chapter;
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
