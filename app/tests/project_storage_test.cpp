#include "project_storage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class ProjectStorageTest final : public QObject
{
    Q_OBJECT

private slots:
    void savesAndReopensMinimalProject()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("sample.milah"));
        const QJsonObject payload{
            {QStringLiteral("manifest"),
             QJsonObject{
                 {QStringLiteral("format"), QStringLiteral("milah-project")},
                 {QStringLiteral("version"), 1},
             }},
            {QStringLiteral("files"),
             QJsonArray{
                 QJsonObject{
                     {QStringLiteral("path"), QStringLiteral("sources/a.osis")},
                     {QStringLiteral("contentBase64"),
                      QString::fromLatin1(QByteArray("<osis/>").toBase64())},
                 },
             }},
        };

        QString error;
        QVERIFY2(
            milah::ProjectStorage::saveToPath(path, payload, &error),
            qPrintable(error));
        const QJsonObject reopened =
            milah::ProjectStorage::loadFromPath(path, &error);
        QVERIFY2(!reopened.isEmpty(), qPrintable(error));
        QCOMPARE(
            reopened.value(QStringLiteral("manifest"))
                .toObject()
                .value(QStringLiteral("version"))
                .toInt(),
            1);
        QCOMPARE(
            reopened.value(QStringLiteral("files")).toArray().size(),
            1);
    }
};

QTEST_MAIN(ProjectStorageTest)
#include "project_storage_test.moc"
