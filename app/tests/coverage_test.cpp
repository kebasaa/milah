#include "core/books.h"
#include "core/coverage.h"
#include "core/osis.h"
#include "test_data.h"

#include <QtTest>

using namespace milah;

namespace {

SourceDocument witness(const QString &id, const QStringList &chapters)
{
    ParseOptions options;
    options.id = id;
    options.name = id + QStringLiteral(".osis");
    options.role = SourceRole::Manuscript;
    return parseOsis(milah_test::witnessOsisCovering(id, chapters), options);
}

DocumentRefs refs(const QList<SourceDocument> &documents)
{
    DocumentRefs result;
    result.reserve(documents.size());
    for (const SourceDocument &document : documents) {
        result.append(&document);
    }
    return result;
}

QStringList labels(const QList<LocationCoverage> &locations)
{
    QStringList result;
    for (const LocationCoverage &entry : locations) {
        result.append(QStringLiteral("%1.%2")
                          .arg(entry.location.book)
                          .arg(entry.location.chapter));
    }
    return result;
}

} // namespace

class CoverageTest final : public QObject
{
    Q_OBJECT

private slots:
    void aChapterOnlyOneWitnessHasIsStillOffered()
    {
        // The whole point: witnesses of different extent, and the places where
        // they differ are the ones the editor most wants to reach.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("short"), {QStringLiteral("Rev.1"), QStringLiteral("Rev.2")}),
            witness(
                QStringLiteral("long"),
                {QStringLiteral("Rev.1"),
                 QStringLiteral("Rev.2"),
                 QStringLiteral("Rev.3")}),
        };
        const QList<LocationCoverage> covered = coveredLocations(refs(documents));

        QCOMPARE(
            labels(covered),
            QStringList({QStringLiteral("Rev.1"),
                         QStringLiteral("Rev.2"),
                         QStringLiteral("Rev.3")}));
        QCOMPARE(covered.at(0).complete, true);
        QCOMPARE(covered.at(0).sourceCount, 2);
        QCOMPARE(covered.at(1).complete, true);
        // Reached by one witness of two, so offered but marked.
        QCOMPARE(covered.at(2).complete, false);
        QCOMPARE(covered.at(2).sourceCount, 1);
    }

    void aGapIsNotFilledIn()
    {
        // A fragmentary witness leaves holes. Offering a chapter no manuscript
        // has would open a blank screen, so the list stays a union of what is
        // really there rather than a run from one to the highest.
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("fragment"),
                {QStringLiteral("Rev.1"), QStringLiteral("Rev.5"), QStringLiteral("Rev.9")}),
            witness(
                QStringLiteral("opening"),
                {QStringLiteral("Rev.1"),
                 QStringLiteral("Rev.2"),
                 QStringLiteral("Rev.3")}),
        };

        QCOMPARE(
            labels(coveredLocations(refs(documents))),
            QStringList({QStringLiteral("Rev.1"),
                         QStringLiteral("Rev.2"),
                         QStringLiteral("Rev.3"),
                         QStringLiteral("Rev.5"),
                         QStringLiteral("Rev.9")}));
    }

    void booksComeBackInCanonicalOrder()
    {
        // Loaded Revelation first, but Matthew is read first.
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("a"),
                {QStringLiteral("Rev.1"), QStringLiteral("Matt.2")}),
        };

        QCOMPARE(
            labels(coveredLocations(refs(documents))),
            QStringList({QStringLiteral("Matt.2"), QStringLiteral("Rev.1")}));
    }

    void oneManuscriptCoversEverythingItHas()
    {
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("only"),
                {QStringLiteral("Rev.1"), QStringLiteral("Rev.2")}),
        };
        const QList<LocationCoverage> covered = coveredLocations(refs(documents));

        QCOMPARE(covered.size(), 2);
        for (const LocationCoverage &entry : covered) {
            // Nothing is missing when there is nothing to be missing from, so
            // a lone manuscript must not have every chapter marked partial.
            QCOMPARE(entry.complete, true);
            QCOMPARE(entry.sourceCount, 1);
        }
    }

    void noManuscriptsMeansNoLocations()
    {
        QVERIFY(coveredLocations({}).isEmpty());
    }

    void aChaptersKeyIsTheStartOfItsVerseIds()
    {
        // The chapter reference map is keyed by locationKey() but looked up
        // from a verse id by taking its first two parts. If those ever stopped
        // agreeing, every chapter reference would silently stop applying, so
        // the agreement is asserted rather than assumed.
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("a"),
                {QStringLiteral("Rev.1"), QStringLiteral("1Cor.13")}),
        };

        for (const LocationCoverage &covered : coveredLocations(refs(documents))) {
            const QStringList verseIds =
                verseIdsAtLocation(refs(documents), covered.location);
            QVERIFY(!verseIds.isEmpty());
            for (const QString &verseId : verseIds) {
                QCOMPARE(
                    verseId.section(QLatin1Char('.'), 0, 1),
                    locationKey(covered.location));
            }
        }
    }

    void booksAreNamedForAReader()
    {
        QCOMPARE(bookName(QStringLiteral("Rev")), QStringLiteral("Revelation"));
        QCOMPARE(bookName(QStringLiteral("Matt")), QStringLiteral("Matthew"));
        QCOMPARE(bookName(QStringLiteral("1Cor")), QStringLiteral("1 Corinthians"));
        QCOMPARE(bookName(QStringLiteral("Song")), QStringLiteral("Song of Songs"));

        // A manuscript may carry something outside the canon; its id names its
        // own book rather than coming back empty.
        QCOMPARE(bookName(QStringLiteral("Sir")), QStringLiteral("Sir"));
        QCOMPARE(bookName(QString()), QString());
    }
};

QTEST_MAIN(CoverageTest)
#include "coverage_test.moc"
