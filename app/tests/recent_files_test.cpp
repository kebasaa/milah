#include "core/recent_files.h"

#include <QDir>
#include <QtTest>

using namespace milah;

namespace {

/// An absolute path under a folder that need not exist. The list remembers
/// files whether or not they are there — that is the point of it — so nothing
/// here has to touch the disk.
QString at(const QString &folder, const QString &name)
{
    return QDir::cleanPath(QDir(QDir::rootPath() + folder).absoluteFilePath(name));
}

} // namespace

/// The Open Recent list. Small enough to hold in the head and easy enough to
/// get subtly wrong: a list that quietly holds one file twice pushes a real one
/// off the bottom, and a menu whose entries all read alike cannot be used.
class RecentFilesTest final : public QObject
{
    Q_OBJECT

private slots:
    void aFileGoesToTheHead()
    {
        const QStringList list = withRecentFile({}, at("work", "Rev.milah"));
        QCOMPARE(list.size(), 1);
        QVERIFY(list.constFirst().endsWith(QStringLiteral("Rev.milah")));
    }

    void theNewestIsFirst()
    {
        QStringList list;
        list = withRecentFile(list, at("work", "one.milah"));
        list = withRecentFile(list, at("work", "two.milah"));
        QVERIFY(list.constFirst().endsWith(QStringLiteral("two.milah")));
        QVERIFY(list.constLast().endsWith(QStringLiteral("one.milah")));
    }

    void openingTheSameFileAgainMovesItRatherThanCopyingIt()
    {
        // The one that matters: a second copy is not merely untidy, it takes a
        // place from a file the reader has not finished with.
        QStringList list;
        list = withRecentFile(list, at("work", "one.milah"));
        list = withRecentFile(list, at("work", "two.milah"));
        list = withRecentFile(list, at("work", "one.milah"));

        QCOMPARE(list.size(), 2);
        QVERIFY(list.constFirst().endsWith(QStringLiteral("one.milah")));
        QVERIFY(list.constLast().endsWith(QStringLiteral("two.milah")));
    }

    void theListStopsAtItsLimitAndTheOldestFallsOff()
    {
        QStringList list;
        for (int number = 1; number <= RecentFileLimit + 2; ++number) {
            list = withRecentFile(list, at("work", QStringLiteral("%1.milah").arg(number)));
        }
        QCOMPARE(list.size(), RecentFileLimit);
        QVERIFY(list.constFirst().endsWith(QStringLiteral("7.milah")));
        // 1 and 2 are gone; 3 is the oldest still held.
        QVERIFY(list.constLast().endsWith(QStringLiteral("3.milah")));
    }

    void oneFileSpeltTwoWaysIsOneEntry()
    {
        // A path from a dialog and a path from this list can differ in their
        // separators or their dots and still name one file. Two entries for it
        // would be two entries for the same thing.
        QStringList list;
        list = withRecentFile(list, at("work", "Rev.milah"));
        list = withRecentFile(list, at("work", "sub/../Rev.milah"));
        QCOMPARE(list.size(), 1);
    }

#ifdef Q_OS_WIN
    void caseDoesNotMakeASecondFile()
    {
        // Windows does not tell these apart, so neither may this.
        QStringList list;
        list = withRecentFile(list, at("work", "Rev.milah"));
        list = withRecentFile(list, at("WORK", "rev.MILAH"));
        QCOMPARE(list.size(), 1);
    }
#endif

    void aRelativePathIsTheSameFileAsItsAbsoluteForm()
    {
        const QString name = QStringLiteral("Rev.milah");
        QStringList list;
        list = withRecentFile(list, QDir::current().absoluteFilePath(name));
        list = withRecentFile(list, name);
        QCOMPARE(list.size(), 1);
        // Stored absolute, because the working directory will not be the same
        // next time Milah starts.
        QVERIFY(QDir::isAbsolutePath(list.constFirst()));
    }

    void nothingIsRememberedForAnEmptyPath()
    {
        const QStringList list{at("work", "Rev.milah")};
        QCOMPARE(withRecentFile(list, QString()), list);
    }

    void distinctNamesAreLeftAlone()
    {
        const QStringList labels =
            recentFileLabels({at("work", "Matthew.milah"), at("work", "Mark.milah")});
        QCOMPARE(labels, QStringList({QStringLiteral("Matthew.milah"),
                                      QStringLiteral("Mark.milah")}));
    }

    void namesThatWouldReadAlikeSayWhichFolderTheyAreIn()
    {
        // Manuscripts are filed by book, so two of them holding a Revelation
        // apiece is the ordinary case rather than the odd one. A menu offering
        // "Revelation.milah" twice is a menu nobody can choose from.
        const QStringList labels = recentFileLabels(
            {at("Cochin", "Revelation.milah"),
             at("Sloane", "Revelation.milah"),
             at("Sloane", "Matthew.milah")});

        QVERIFY(labels.at(0).contains(QStringLiteral("Cochin")));
        QVERIFY(labels.at(1).contains(QStringLiteral("Sloane")));
        QVERIFY(labels.at(0) != labels.at(1));
        // The one that is unambiguous is not dragged into it.
        QCOMPARE(labels.at(2), QStringLiteral("Matthew.milah"));
    }

    void everyPathGetsALabel()
    {
        // The menu reads the two lists off in step, so a short answer here would
        // put the wrong name on a file.
        const QStringList paths{
            at("work", "one.milah"), at("work", "two.milah"), at("other", "two.milah")};
        QCOMPARE(recentFileLabels(paths).size(), paths.size());
        QCOMPARE(recentFileLabels({}).size(), 0);
    }
};

QTEST_MAIN(RecentFilesTest)
#include "recent_files_test.moc"
