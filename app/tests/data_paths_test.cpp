#include "core/data_paths.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace milah;

/// Where Milah looks for its data files. A copy that cannot find them silently
/// loses its Strong's numbers and abbreviation table, and nothing on screen
/// says why, so each installed layout has to be on the list.
class DataPathsTest final : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { qunsetenv("MILAH_DATA_DIR"); }

    void anOverrideIsTriedFirst()
    {
        qputenv("MILAH_DATA_DIR", QByteArrayLiteral("/somewhere/else"));
        QCOMPARE(dataSearchPaths().constFirst(), QStringLiteral("/somewhere/else"));
    }

    void everyInstalledLayoutIsSearched()
    {
        const QString beside = QCoreApplication::applicationDirPath();
        const QStringList paths = dataSearchPaths();

        // The portable folder, a bin/ layout, a macOS bundle and a Linux
        // prefix, in that order and after the personal copy.
        const QStringList installed = {
            beside + QStringLiteral("/data"),
            beside + QStringLiteral("/../data"),
            beside + QStringLiteral("/../Resources/data"),
            beside + QStringLiteral("/../share/milah/data"),
        };
        const qsizetype first = paths.indexOf(installed.constFirst());
        QVERIFY(first >= 0);
        QCOMPARE(paths.mid(first), installed);
    }

    void theFirstReadableCopyWins()
    {
        QTemporaryDir replacement;
        QVERIFY(replacement.isValid());
        QFile file(replacement.filePath(QStringLiteral("hebrew_lexicon.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}");
        file.close();

        qputenv("MILAH_DATA_DIR", replacement.path().toUtf8());
        QCOMPARE(QDir::cleanPath(locateDataFile(QStringLiteral("hebrew_lexicon.json"))),
                 QDir::cleanPath(file.fileName()));
    }

    void aMissingFileGivesNothing()
    {
        QCOMPARE(locateDataFile(QStringLiteral("no_such_file.json")), QString());
    }
};

QTEST_MAIN(DataPathsTest)
#include "data_paths_test.moc"
