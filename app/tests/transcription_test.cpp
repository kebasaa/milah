#include "core/project.h"
#include "core/transcription.h"
#include "project_storage.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace milah;

namespace {

TranscribedWord word(const char *hebrew, const char *english)
{
    TranscribedWord made;
    made.hebrew = QString::fromUtf8(hebrew);
    made.english = QString::fromUtf8(english);
    return made;
}

TranscribedVerse verse(const QString &number, bool newChapter = false)
{
    TranscribedVerse made;
    made.number = number;
    made.startsNewChapter = newChapter;
    return made;
}

} // namespace

class TranscriptionTest final : public QObject
{
    Q_OBJECT

private:
    static TranscribedPage samplePage()
    {
        TranscribedPage page;
        page.imageEntry = QStringLiteral("images/001-folio.png");
        page.imageName = QStringLiteral("folio.png");
        page.sourcePath = QStringLiteral("D:/scans/folio.png");
        page.book = QStringLiteral("Gen");
        page.firstChapter = 4;

        TranscribedVerse first = verse(QStringLiteral("1"));
        first.words = {word("בְּרֵאשִׁית", "in-beginning"), word("בָּרָא", "he-created")};

        TranscribedVerse second = verse(QStringLiteral("2"));
        second.words = {word("אֱלֹהִים", "God")};
        second.words.first().englishIsOwn = true;

        TranscribedVerse third = verse(QStringLiteral("1"), true);
        third.words = {word("אֵת", "[obj]")};

        page.verses = {first, second, third};
        return page;
    }

    static TranscriptionDocument sampleDocument()
    {
        TranscriptionDocument document;
        document.metadata.manuscriptName = QStringLiteral("Ebr. 530");
        document.metadata.transcriber = QStringLiteral("J. Mueller");
        document.metadata.origin = QStringLiteral("Northern Italy");
        document.metadata.libraryMark = QStringLiteral("BAV Ebr. 530");
        document.metadata.notes = QStringLiteral("Water damage on the outer margin.");
        document.metadata.extra.insert(QStringLiteral("quire"), QStringLiteral("iv"));
        document.pages = {samplePage()};
        return document;
    }

private slots:
    void chaptersCountOnFromTheBreak()
    {
        const TranscribedPage page = samplePage();
        // The page opens at 4, and the break belongs to the verse carrying it —
        // so the third verse is already in 5, not the one after it.
        QCOMPARE(chapterOfVerse(page, 0), 4);
        QCOMPARE(chapterOfVerse(page, 1), 4);
        QCOMPARE(chapterOfVerse(page, 2), 5);
    }

    void aBreakCarriesEverythingAfterItAlong()
    {
        TranscribedPage page = samplePage();
        page.verses.append(verse(QStringLiteral("2")));
        page.verses.append(verse(QStringLiteral("3")));
        // Moving one verse moves the rest of the folio with it: that is what
        // "a new chapter starts here" means.
        QCOMPARE(chapterOfVerse(page, 3), 5);
        QCOMPARE(chapterOfVerse(page, 4), 5);

        // And a second break counts on again rather than resetting.
        page.verses[4].startsNewChapter = true;
        QCOMPARE(chapterOfVerse(page, 3), 5);
        QCOMPARE(chapterOfVerse(page, 4), 6);
    }

    void verseIdsFollowTheChapters()
    {
        const TranscribedPage page = samplePage();
        QCOMPARE(transcribedVerseId(page, 0), QStringLiteral("Gen.4.1"));
        QCOMPARE(transcribedVerseId(page, 2), QStringLiteral("Gen.5.1"));
    }

    void halfAnIdIsNoId()
    {
        // A verse with no number, or a page with no book, cannot be addressed —
        // and a partial reference would be worse than none, because it would
        // export as though it meant something.
        TranscribedPage page = samplePage();
        page.book.clear();
        QVERIFY(transcribedVerseId(page, 0).isEmpty());

        TranscribedPage unnumbered = samplePage();
        unnumbered.verses[0].number.clear();
        QVERIFY(transcribedVerseId(unnumbered, 0).isEmpty());
        QVERIFY(transcribedVerseId(unnumbered, 99).isEmpty());
    }

    void verseNumbersAreRecognisedAsTyped()
    {
        QVERIFY(looksLikeVerseNumber(QStringLiteral("3")));
        QVERIFY(looksLikeVerseNumber(QStringLiteral("12")));
        // A verse a manuscript divides.
        QVERIFY(looksLikeVerseNumber(QStringLiteral("12a")));
        QVERIFY(looksLikeVerseNumber(QStringLiteral(" 7 ")));

        QVERIFY(!looksLikeVerseNumber(QString()));
        QVERIFY(!looksLikeVerseNumber(QStringLiteral("a")));
        QVERIFY(!looksLikeVerseNumber(QStringLiteral("12ab")));
        QVERIFY(!looksLikeVerseNumber(QString::fromUtf8("בְּרֵאשִׁית")));
    }

    void aDocumentSurvivesTheArchive()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());

        const TranscriptionDocument original = sampleDocument();
        QHash<QString, QByteArray> images;
        images.insert(
            QStringLiteral("images/001-folio.png"), QByteArray("\x89PNG not really", 15));

        const MilahProjectPayload payload = transcriptionPayload(original, images);
        QCOMPARE(payload.files.size(), 1);
        QVERIFY(payload.suggestedName.endsWith(QStringLiteral(".trscrpt")));

        const QString path = home.filePath(QStringLiteral("folio.trscrpt"));
        QString error;
        QVERIFY2(
            ProjectStorage::saveToPath(
                path, payloadToJson(payload), &error, ProjectStorage::ImageEntryLimit),
            qPrintable(error));

        const QJsonObject read =
            ProjectStorage::loadFromPath(path, &error, ProjectStorage::ImageEntryLimit);
        QVERIFY2(!read.isEmpty(), qPrintable(error));

        const MilahProjectPayload reloaded = payloadFromJson(read);
        const TranscriptionDocument restored = restoreTranscription(reloaded);

        QCOMPARE(restored.metadata.manuscriptName, original.metadata.manuscriptName);
        QCOMPARE(restored.metadata.transcriber, original.metadata.transcriber);
        QCOMPARE(restored.metadata.libraryMark, original.metadata.libraryMark);
        QCOMPARE(restored.metadata.notes, original.metadata.notes);
        QCOMPARE(restored.metadata.extra.value(QStringLiteral("quire")), QStringLiteral("iv"));

        QCOMPARE(restored.pages.size(), 1);
        const TranscribedPage &page = restored.pages.first();
        QCOMPARE(page.book, QStringLiteral("Gen"));
        QCOMPARE(page.firstChapter, 4);
        QCOMPARE(page.imageName, QStringLiteral("folio.png"));
        QCOMPARE(page.verses.size(), 3);
        QCOMPARE(page.verses.at(0).words.size(), 2);
        QCOMPARE(page.verses.at(0).words.at(0).hebrew, QString::fromUtf8("בְּרֵאשִׁית"));
        QCOMPARE(page.verses.at(0).words.at(0).english, QStringLiteral("in-beginning"));

        // The one bit that decides whether re-reading a word may overwrite the
        // gloss: if it does not survive, a reload silently erases the
        // transcriber's own translations the next time they touch the Hebrew.
        QVERIFY(!page.verses.at(0).words.at(0).englishIsOwn);
        QVERIFY(page.verses.at(1).words.at(0).englishIsOwn);

        QVERIFY(page.verses.at(2).startsNewChapter);
        QCOMPARE(chapterOfVerse(page, 2), 5);

        // And the folio itself came back with it.
        const QHash<QString, QByteArray> restoredImages = transcriptionImages(reloaded);
        QCOMPARE(
            restoredImages.value(QStringLiteral("images/001-folio.png")),
            images.value(QStringLiteral("images/001-folio.png")));
    }

    void anEmptyDocumentRoundTrips()
    {
        const MilahProjectPayload payload = transcriptionPayload(TranscriptionDocument(), {});
        const TranscriptionDocument restored = restoreTranscription(payload);
        QVERIFY(restored.isEmpty());
        QVERIFY(restored.metadata.manuscriptName.isEmpty());
    }

    void anEditionIsNotOpenedAsATranscription()
    {
        // Both are zip archives with a manifest, so the mistake is easy to make
        // and has to be named rather than failing further down.
        MilahProjectPayload edition;
        edition.manifest = QJsonObject{
            {QStringLiteral("format"), QStringLiteral("milah-project")},
            {QStringLiteral("version"), 1},
        };
        bool threw = false;
        try {
            restoreTranscription(edition);
        } catch (const ProjectError &error) {
            threw = true;
            QVERIFY(error.message().contains(QStringLiteral("edition")));
        }
        QVERIFY(threw);
    }

    void anUnknownFormatIsRefused()
    {
        MilahProjectPayload strange;
        strange.manifest = QJsonObject{
            {QStringLiteral("format"), QStringLiteral("milah-transcription")},
            {QStringLiteral("version"), 99},
        };
        QVERIFY_THROWS_EXCEPTION(ProjectError, restoreTranscription(strange));
    }

    void aPageWithNoImageBytesKeepsItsText()
    {
        // The text is the work; the picture is a copy of something that exists
        // elsewhere. Losing the second must not lose the first.
        const MilahProjectPayload payload = transcriptionPayload(sampleDocument(), {});
        QVERIFY(payload.files.isEmpty());

        const TranscriptionDocument restored = restoreTranscription(payload);
        QCOMPARE(restored.pages.size(), 1);
        QCOMPARE(restored.pages.first().verses.size(), 3);
    }
};

QTEST_MAIN(TranscriptionTest)
#include "transcription_test.moc"
