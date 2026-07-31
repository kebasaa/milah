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

    void aVerseReferenceAndANoteSurviveSaving()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
            witness(QStringLiteral("b"), QString::fromUtf8("דבר דוד")),
        };
        DocumentRefs sources;
        for (const SourceDocument &document : documents) {
            sources.append(&document);
        }

        ProjectState state;
        state.sources = documents;
        state.priorityManuscriptId = QStringLiteral("a");
        state.combined.insert(
            QStringLiteral("Matt.1.1"),
            generateCombined(
                alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("a")),
                sources,
                QStringLiteral("a")));
        // This verse departs from the chapter's reference, and one of its words
        // carries the editor's own remark.
        state.verseReferences.insert(QStringLiteral("Matt.1.1"), QStringLiteral("b"));
        state.combinedNotes.insert(
            QStringLiteral("Matt.1.1:1"), QStringLiteral("Read with the Cochin witness."));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("annotated.milah"));

        QString error;
        QVERIFY2(
            ProjectStorage::saveToPath(
                path,
                payloadToJson(projectPayload(state, serializeCombinedOsis(state.combined))),
                &error),
            qPrintable(error));

        const QJsonObject reopened = ProjectStorage::loadFromPath(path, &error);
        QVERIFY2(!reopened.isEmpty(), qPrintable(error));
        const ProjectState restored = restoreProject(payloadFromJson(reopened));

        QCOMPARE(
            restored.verseReferences.value(QStringLiteral("Matt.1.1")),
            QStringLiteral("b"));
        QCOMPARE(
            restored.combinedNotes.value(QStringLiteral("Matt.1.1:1")),
            QStringLiteral("Read with the Cochin witness."));
    }

    void aChapterReferenceAndCorrectedSpansSurviveSaving()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
            witness(QStringLiteral("b"), QString::fromUtf8("דבר דוד")),
        };

        TranslationSpan kept;
        kept.id = QStringLiteral("t:Matt.1.1:0");
        kept.translationId = QStringLiteral("t");
        kept.verseId = QStringLiteral("Matt.1.1");
        kept.columnStart = 0;
        kept.columnEnd = 1;
        kept.tokenStart = 0;
        kept.tokenEnd = 2;

        TranslationSpan dropped = kept;
        dropped.id = QStringLiteral("t:Matt.1.1:1");
        dropped.columnStart = 1;
        dropped.columnEnd = 2;
        dropped.removed = true;

        ProjectState state;
        state.sources = documents;
        state.priorityManuscriptId = QStringLiteral("a");
        state.chapterReferences.insert(QStringLiteral("Matt.1"), QStringLiteral("b"));
        state.translationSpans = {kept, dropped};
        // The wording the editor typed into the Interlinear row, which is a
        // line of the edition rather than anything the translation says.
        state.interlinearWords.insert(
            QStringLiteral("Matt.1.1:0"), QStringLiteral("to-be"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("spans.milah"));

        QString error;
        QVERIFY2(
            ProjectStorage::saveToPath(
                path, payloadToJson(projectPayload(state, QString())), &error),
            qPrintable(error));

        const QJsonObject reopened = ProjectStorage::loadFromPath(path, &error);
        QVERIFY2(!reopened.isEmpty(), qPrintable(error));
        const ProjectState restored = restoreProject(payloadFromJson(reopened));

        QCOMPARE(
            restored.chapterReferences.value(QStringLiteral("Matt.1")),
            QStringLiteral("b"));
        QCOMPARE(
            restored.interlinearWords.value(QStringLiteral("Matt.1.1:0")),
            QStringLiteral("to-be"));
        QCOMPARE(restored.translationSpans.size(), 2);
        QCOMPARE(restored.translationSpans.at(0).removed, false);
        // The removed one is kept rather than dropped, which is what stops the
        // verse being regenerated with it back in.
        QCOMPARE(restored.translationSpans.at(1).removed, true);
    }

    void aProjectWithoutInterlinearWordsReadsAsHavingNone()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
        };

        ProjectState state;
        state.sources = documents;
        state.priorityManuscriptId = QStringLiteral("a");

        QJsonObject json = payloadToJson(projectPayload(state, QString()));
        QJsonObject manifest = json.value(QStringLiteral("manifest")).toObject();
        manifest.remove(QStringLiteral("interlinearWords"));
        json.insert(QStringLiteral("manifest"), manifest);

        QVERIFY(restoreProject(payloadFromJson(json)).interlinearWords.isEmpty());
    }

    void anInterlinearCarriesDashedGlosses()
    {
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Matt.1.1");
        draft.reference.book = QStringLiteral("Matt");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        for (const QString &word : {QStringLiteral("AAA"),
                                    QStringLiteral("BBB"),
                                    QStringLiteral("CCC")}) {
            ConsensusColumn column;
            column.text = word;
            draft.columns.append(column);
        }

        InterlinearGlosses glosses;
        glosses[QStringLiteral("Matt.1.1")].insert(0, QStringLiteral("book"));
        // Several translation words standing for one Hebrew word.
        glosses[QStringLiteral("Matt.1.1")].insert(1, QStringLiteral("to-be"));

        QMap<QString, CombinedDraft> drafts;
        drafts.insert(QStringLiteral("Matt.1.1"), draft);
        const QString osis = serializeInterlinearOsis(drafts, glosses);

        QVERIFY2(osis.contains(QStringLiteral("<w gloss=\"book\">AAA</w>")),
                 qPrintable(osis));
        QVERIFY2(osis.contains(QStringLiteral("<w gloss=\"to-be\">BBB</w>")),
                 qPrintable(osis));
        // A word nothing is aligned to is still written, but carries no gloss.
        QVERIFY2(osis.contains(QStringLiteral("<w>CCC</w>")), qPrintable(osis));

        // And the plain edition is unchanged by any of it.
        QVERIFY(!serializeCombinedOsis(drafts).contains(QStringLiteral("<w ")));
    }

    void anEditorsNoteIsAnchoredToItsOwnWord()
    {
        // What the annotated export is made of: a note carried at the offset
        // columnCharOffset() gives, which has to land on the word it was
        // written about rather than somewhere else in the verse.
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Matt.1.1");
        draft.reference.book = QStringLiteral("Matt");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        for (const QString &word : {QStringLiteral("AAA"),
                                    QStringLiteral("BBB"),
                                    QStringLiteral("CCC")}) {
            ConsensusColumn column;
            column.text = word;
            draft.columns.append(column);
        }

        SourceNote note;
        note.number = QStringLiteral("1");
        note.text = QStringLiteral("ZZNOTE");
        note.charOffset = columnCharOffset(draft, 1);

        CombinedApparatus apparatus;
        apparatus.notes[QStringLiteral("Matt.1.1")].append(note);

        QMap<QString, CombinedDraft> drafts;
        drafts.insert(QStringLiteral("Matt.1.1"), draft);
        const QString osis = serializeCombinedOsis(drafts, WorkMetadata(), apparatus);

        QVERIFY2(osis.contains(QStringLiteral("ZZNOTE")), qPrintable(osis));
        // Between the first word and the second, which is the word it is about.
        const int first = int(osis.indexOf(QStringLiteral("AAA")));
        const int marker = int(osis.indexOf(QStringLiteral("ZZNOTE")));
        const int second = int(osis.indexOf(QStringLiteral("BBB")));
        QVERIFY(first >= 0 && marker > first && second > marker);

        // And the plain export, given no apparatus, carries none of it.
        QVERIFY(!serializeCombinedOsis(drafts).contains(QStringLiteral("ZZNOTE")));
    }

    void aProjectWithoutThemReadsAsHavingNone()
    {
        // Projects written before either existed must still open, meaning no
        // verse departs from its chapter and no word carries a remark.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
        };

        ProjectState state;
        state.sources = documents;
        state.priorityManuscriptId = QStringLiteral("a");

        QJsonObject json = payloadToJson(projectPayload(state, QString()));
        QJsonObject manifest = json.value(QStringLiteral("manifest")).toObject();
        manifest.remove(QStringLiteral("verseReferences"));
        manifest.remove(QStringLiteral("combinedNotes"));
        json.insert(QStringLiteral("manifest"), manifest);

        const ProjectState restored = restoreProject(payloadFromJson(json));
        QVERIFY(restored.verseReferences.isEmpty());
        QVERIFY(restored.combinedNotes.isEmpty());
    }
};

QTEST_MAIN(ProjectStorageTest)
#include "project_storage_test.moc"
