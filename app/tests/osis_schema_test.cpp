#include "core/serialize.h"
#include "core/transcription_osis.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace milah;

namespace {

CombinedDraft draft(const QString &book, int chapter, const QString &verse,
                    const QStringList &words)
{
    CombinedDraft result;
    result.reference.book = book;
    result.reference.chapter = chapter;
    result.reference.verse = verse;
    result.reference.id = QStringLiteral("%1.%2.%3").arg(book).arg(chapter).arg(verse);
    for (const QString &word : words) {
        ConsensusColumn column;
        column.text = word;
        result.columns.append(column);
    }
    return result;
}

/// Everything a header can carry, so a field the schema puts out of order or
/// does not allow shows up here rather than in somebody else's validator.
WorkMetadata fullMetadata(const QString &workType)
{
    WorkMetadata metadata;
    metadata.workId = QStringLiteral("Sloane237");
    metadata.title = QStringLiteral("A <Hebrew> \"Revelation\" & more");
    metadata.language = QStringLiteral("he");
    metadata.scope = QStringLiteral("Rev");
    metadata.rights = QStringLiteral("© 2017 by somebody");
    metadata.license = QStringLiteral("CC BY-NC-SA 4.0");
    metadata.workType = workType;
    metadata.identifiers.insert(QStringLiteral("x-shelfmark"), QStringLiteral("Sloane MS 237"));
    metadata.identifiers.insert(QStringLiteral("x-transcriber"), QStringLiteral("A. Person"));
    metadata.descriptions.insert(
        QStringLiteral("x-translated-from"),
        WorkDescription{QStringLiteral("Translated from the Greek"), QStringLiteral("x-uncertain")});
    metadata.descriptions.insert(
        QStringLiteral("x-folios"), WorkDescription{QStringLiteral("1r-4v"), {}});
    return metadata;
}

/// Two chapters, a preamble, a verse split by a folio boundary, notes in
/// running text and on a preamble, a book title and a chapter title.
QMap<QString, CombinedDraft> sampleDrafts()
{
    QMap<QString, CombinedDraft> drafts;
    for (const CombinedDraft &one :
         {draft(QStringLiteral("Rev"), 1, QStringLiteral("0"), {QStringLiteral("חזון")}),
          draft(QStringLiteral("Rev"), 1, QStringLiteral("1"),
                {QStringLiteral("חזון"), QStringLiteral("יוחנן")}),
          draft(QStringLiteral("Rev"), 1, QStringLiteral("2"), {QStringLiteral("אשר")}),
          draft(QStringLiteral("Rev"), 2, QStringLiteral("1"), {QStringLiteral("כתב")})}) {
        drafts.insert(one.reference.id, one);
    }
    return drafts;
}

CombinedApparatus sampleApparatus()
{
    CombinedApparatus apparatus;
    SourceNote note;
    note.number = QStringLiteral("1");
    note.text = QStringLiteral("a doubtful & <odd> letter");
    note.charOffset = 4;
    note.tokenIndex = 1;
    apparatus.notes.insert(QStringLiteral("Rev.1.1"), {note});
    apparatus.notes.insert(QStringLiteral("Rev.1.0"), {note});

    SourceMilestone folio;
    folio.type = QStringLiteral("pb");
    folio.n = QStringLiteral("2r");
    folio.verseId = QStringLiteral("Rev.1.1");
    folio.charOffset = 4;
    apparatus.milestones.append(folio);

    SourceTitle bookTitle;
    bookTitle.type = QStringLiteral("main");
    bookTitle.canonical = true;
    bookTitle.text = QStringLiteral("ספר חזיונות");
    bookTitle.book = QStringLiteral("Rev");
    apparatus.titles.append(bookTitle);

    SourceTitle chapterTitle;
    chapterTitle.type = QStringLiteral("chapter");
    chapterTitle.text = QStringLiteral("פרק ב");
    chapterTitle.book = QStringLiteral("Rev");
    chapterTitle.chapter = 2;
    apparatus.titles.append(chapterTitle);
    return apparatus;
}

/// A transcription with a verse running over a folio break and words carrying
/// glosses and notes, through the same path File ▸ Export takes.
TranscriptionDrafts sampleTranscription()
{
    TranscriptionDocument document;
    for (const QString &hebrew : {QStringLiteral("ספר"), QStringLiteral("תולדות")}) {
        TranscribedPage page;
        page.book = QStringLiteral("Matt");
        TranscribedVerse verse;
        verse.number = QStringLiteral("1");
        TranscribedWord word;
        word.hebrew = hebrew;
        word.english = QStringLiteral("gloss");
        word.note = QStringLiteral("note");
        verse.words.append(word);
        page.verses.append(verse);
        document.pages.append(page);
    }
    return transcriptionDrafts(document);
}

QString schemaPath()
{
    return QStringLiteral(MILAH_TEST_FIXTURES "/osis/osisCore.2.1.1.xsd");
}

} // namespace

/// Every kind of OSIS Milah writes, checked against the OSIS 2.1.1 schema.
///
/// Validation needs xmllint, which every CI runner has and a Windows desktop
/// usually does not; without it the files are still written and parsed, and
/// the schema step is skipped. $MILAH_OSIS_DUMP keeps the files in a folder of
/// one's choosing, for validating them with something else.
class OsisSchemaTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_scratch;
    QStringList m_written;

    QString write(const QString &name, const QString &osis)
    {
        const QString dump = qEnvironmentVariable("MILAH_OSIS_DUMP");
        const QString directory = dump.isEmpty() ? m_scratch.path() : dump;
        QDir().mkpath(directory);
        const QString path = QDir(directory).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QString();
        }
        file.write(osis.toUtf8());
        return path;
    }

    void validate(const QString &path)
    {
        QVERIFY2(!path.isEmpty(), "The OSIS file could not be written.");
        const QString xmllint = QStandardPaths::findExecutable(QStringLiteral("xmllint"));
        if (xmllint.isEmpty()) {
            QSKIP("xmllint is not on PATH; the schema check runs in CI.");
        }
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(xmllint,
                      {QStringLiteral("--noout"), QStringLiteral("--nonet"),
                       QStringLiteral("--schema"), schemaPath(), path});
        QVERIFY(process.waitForFinished(30000));
        QVERIFY2(process.exitCode() == 0, process.readAll().constData());
    }

private slots:
    void theSchemaIsThere() { QVERIFY(QFile::exists(schemaPath())); }

    void theCombinedEditionIsValid()
    {
        validate(write(QStringLiteral("combined.osis"),
                       serializeCombinedOsis(sampleDrafts(), fullMetadata(QString()),
                                             sampleApparatus())));
    }

    void theInterlinearEditionIsValid()
    {
        InterlinearGlosses glosses;
        glosses[QStringLiteral("Rev.1.1")].insert(0, QStringLiteral("vision"));
        glosses[QStringLiteral("Rev.1.1")].insert(1, QStringLiteral("John \"the\" seer"));
        validate(write(QStringLiteral("interlinear.osis"),
                       serializeInterlinearOsis(sampleDrafts(), glosses,
                                                fullMetadata(QString()), sampleApparatus())));
    }

    void aTranscriptionIsValid()
    {
        const TranscriptionDrafts work = sampleTranscription();
        validate(write(QStringLiteral("transcription.osis"),
                       serializeCombinedOsis(work.drafts,
                                             fullMetadata(QStringLiteral("x-manuscript")),
                                             work.apparatus)));
    }

    void anInterlinearTranscriptionIsValid()
    {
        const TranscriptionDrafts work = sampleTranscription();
        validate(write(QStringLiteral("transcription-interlinear.osis"),
                       serializeInterlinearOsis(work.drafts, work.glosses,
                                                fullMetadata(QStringLiteral("x-manuscript")),
                                                work.apparatus)));
    }

    void anEmptyEditionIsValid()
    {
        validate(write(QStringLiteral("empty.osis"),
                       serializeCombinedOsis({}, WorkMetadata(), CombinedApparatus())));
    }
};

QTEST_MAIN(OsisSchemaTest)
#include "osis_schema_test.moc"
