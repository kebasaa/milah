#include "core/books.h"

#include <QtTest>

using namespace milah;

/// What a transcriber writes in the Book field and what the exported file
/// addresses the verses by are two different strings, and this is the only
/// thing that joins them. A wrong answer here does not look wrong on screen —
/// it comes out as a verse filed under the wrong book.
class BooksTest final : public QObject
{
    Q_OBJECT

private slots:
    void theCanonIsListedBothWays()
    {
        QCOMPARE(bookIds().size(), bookNames().size());
        QVERIFY(bookIds().contains(QStringLiteral("Rev")));
        QVERIFY(bookNames().contains(QStringLiteral("Revelation")));
        // Canonical order, not alphabetical: Genesis opens and Revelation ends.
        QCOMPARE(bookIds().constFirst(), QStringLiteral("Gen"));
        QCOMPARE(bookIds().constLast(), QStringLiteral("Rev"));
        QCOMPARE(bookNames().constFirst(), QStringLiteral("Genesis"));
    }

    void aNameResolvesToItsId()
    {
        QCOMPARE(bookIdFor(QStringLiteral("Revelation")), QStringLiteral("Rev"));
        QCOMPARE(bookIdFor(QStringLiteral("Genesis")), QStringLiteral("Gen"));
        QCOMPARE(bookIdFor(QStringLiteral("Song of Songs")), QStringLiteral("Song"));
    }

    void anIdResolvesToItself()
    {
        QCOMPARE(bookIdFor(QStringLiteral("Rev")), QStringLiteral("Rev"));
        QCOMPARE(bookIdFor(QStringLiteral("1Chr")), QStringLiteral("1Chr"));
    }

    void caseAndSpacingDoNotMatter()
    {
        // A transcriber types quickly and is not asked to get either right.
        QCOMPARE(bookIdFor(QStringLiteral("revelation")), QStringLiteral("Rev"));
        QCOMPARE(bookIdFor(QStringLiteral("REV")), QStringLiteral("Rev"));
        QCOMPARE(bookIdFor(QStringLiteral("1 Chronicles")), QStringLiteral("1Chr"));
        QCOMPARE(bookIdFor(QStringLiteral("1chronicles")), QStringLiteral("1Chr"));
        QCOMPARE(bookIdFor(QStringLiteral("  1 CHRONICLES  ")), QStringLiteral("1Chr"));
        QCOMPARE(bookIdFor(QStringLiteral("songofsongs")), QStringLiteral("Song"));
    }

    void theNumberedBooksStayApart()
    {
        // The commonest way a resolver of this shape goes wrong: three letters
        // shared by three books, told apart only by the digit in front.
        QCOMPARE(bookIdFor(QStringLiteral("1 John")), QStringLiteral("1John"));
        QCOMPARE(bookIdFor(QStringLiteral("2 John")), QStringLiteral("2John"));
        QCOMPARE(bookIdFor(QStringLiteral("3 John")), QStringLiteral("3John"));
        // And the gospel is not any of them.
        QCOMPARE(bookIdFor(QStringLiteral("John")), QStringLiteral("John"));
    }

    void anUnknownWorkResolvesToNothing()
    {
        // Not an error: a transcriber may be reading an apocryphal work, and is
        // then asked what it should be called rather than being refused.
        QVERIFY(bookIdFor(QStringLiteral("Tobit")).isEmpty());
        QVERIFY(bookIdFor(QStringLiteral("1 Enoch")).isEmpty());
        QVERIFY(bookIdFor(QString()).isEmpty());
        QVERIFY(bookIdFor(QStringLiteral("   ")).isEmpty());
    }

    void everyBookSurvivesTheRoundTrip()
    {
        // Each id names a book, and that name resolves back to the same id.
        // This is what stops the two columns of the table drifting apart.
        const QStringList ids = bookIds();
        const QStringList names = bookNames();
        for (int index = 0; index < ids.size(); ++index) {
            const QString id = ids.at(index);
            QCOMPARE(bookName(id), names.at(index));
            QCOMPARE(bookIdFor(names.at(index)), id);
            QCOMPARE(bookIdFor(id), id);
        }
    }

    void anUnknownIdStillNamesItsOwnBook()
    {
        // bookName's standing promise, relied on by the transcription toolbar
        // when it fills the Book field for a work it coined an id for.
        QCOMPARE(bookName(QStringLiteral("Tob")), QStringLiteral("Tob"));
    }
};

QTEST_MAIN(BooksTest)
#include "books_test.moc"
