#include "project_storage.h"

#include "core/alignment.h"
#include "core/osis.h"
#include "core/project.h"
#include "core/serialize.h"
#include "test_data.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace milah;

namespace {

SourceDocument witness(const QString &id, const QString &text)
{
    ParseOptions options;
    options.id = id;
    options.name = id + QStringLiteral(".osis");
    options.role = SourceRole::Manuscript;
    return parseOsis(milah_test::witnessOsis(id, text), options);
}

} // namespace

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

    /// A word settled by hand in the Combined row has to survive the whole
    /// way out to disk and back, and has to be what the exported edition
    /// says. The editing itself lives in AppController, which needs widgets
    /// and cannot be linked here, so the column it writes is built directly.
    void anEditedWordSurvivesSavingAndExport()
    {
        const QString edited = QStringLiteral("ZZTEST");

        QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
            witness(QStringLiteral("b"), QString::fromUtf8("דבר דוד")),
        };
        DocumentRefs sources;
        for (const SourceDocument &document : documents) {
            sources.append(&document);
        }

        const AlignedVerse aligned =
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("a"));
        CombinedDraft draft =
            generateCombined(aligned, sources, QStringLiteral("a"));
        QVERIFY(draft.columns.size() >= 2);
        // The witnesses disagree on the first word and agree on the second, so
        // one column starts unsettled and one starts settled.
        QVERIFY(draft.columns.at(0).needsReview);
        QVERIFY(!draft.columns.at(1).needsReview);

        draft.columns[0].text = edited;
        draft.columns[0].sourceId.reset();
        draft.columns[0].needsReview = false;

        ProjectState state;
        state.sources = documents;
        state.priorityManuscriptId = QStringLiteral("a");
        state.combined.insert(QStringLiteral("Matt.1.1"), draft);

        const QString exported = serializeCombinedOsis(state.combined);
        QVERIFY2(
            exported.contains(edited),
            "the edited word is missing from the exported OSIS");

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("edited.milah"));

        QString error;
        QVERIFY2(
            ProjectStorage::saveToPath(
                path, payloadToJson(projectPayload(state, exported)), &error),
            qPrintable(error));

        const QJsonObject reopened = ProjectStorage::loadFromPath(path, &error);
        QVERIFY2(!reopened.isEmpty(), qPrintable(error));
        const ProjectState restored = restoreProject(payloadFromJson(reopened));

        const CombinedDraft &back = restored.combined.value(QStringLiteral("Matt.1.1"));
        QCOMPARE(back.columns.size(), draft.columns.size());
        QCOMPARE(back.columns.at(0).text.value_or(QString()), edited);
        QCOMPARE(back.columns.at(0).needsReview, false);
        // The untouched column keeps both its reading and its unsettled state.
        QCOMPARE(back.columns.at(1).text, draft.columns.at(1).text);
        QCOMPARE(back.columns.at(1).needsReview, draft.columns.at(1).needsReview);

        QVERIFY(serializeCombinedOsis(restored.combined).contains(edited));
    }
};

QTEST_MAIN(ProjectStorageTest)
#include "project_storage_test.moc"
