#include "core/project.h"
#include "core/transcription.h"
#include "project_storage.h"

#include <QDir>
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

    void aFolioWithNothingReadOffItIsUntouched()
    {
        // The state a folio is left in the moment it is opened: one empty verse
        // holding one empty word, because there has to be somewhere to type.
        // The workspace only offers to say where to start while this holds, so
        // getting it wrong either hides the hint or never lets it go.
        TranscribedPage fresh;
        fresh.verses.append(TranscribedVerse());
        fresh.verses.first().words.append(TranscribedWord());
        QVERIFY(isUntouched(fresh));

        // And a page with no verses at all, which nothing should crash on.
        QVERIFY(isUntouched(TranscribedPage()));
    }

    void oneTypedWordIsEnoughToHaveStarted()
    {
        TranscribedPage started;
        started.verses.append(TranscribedVerse());
        started.verses.first().words.append(word("אֱלֹהִים", ""));
        QVERIFY(!isUntouched(started));
    }

    void aNumberedVerseCountsAsStartedToo()
    {
        // A transcriber who has typed the verse number and nothing else has
        // begun, and does not need telling how to begin.
        TranscribedPage numbered;
        numbered.verses.append(verse(QStringLiteral("1")));
        numbered.verses.first().words.append(TranscribedWord());
        QVERIFY(!isUntouched(numbered));
    }

    void aGlossWithoutItsWordDoesNotCount()
    {
        // Only the Hebrew is the transcriber's reading of the folio; a gloss is
        // the lexicon's, and cannot appear without a word above it anyway.
        TranscribedPage odd;
        odd.verses.append(TranscribedVerse());
        odd.verses.first().words.append(word("", "God"));
        QVERIFY(isUntouched(odd));
    }

    void theSampleFolioHasPlainlyBeenStarted()
    {
        QVERIFY(!isUntouched(samplePage()));
    }

    // Walking a codex must cost nothing. Leaving a folio writes the file, and
    // that used to mean a Save As on the very first arrow press — which, when
    // cancelled, refused the turn and pinned the transcriber to folio one.
    // commitBeforeLeavingPage() needs widgets and cannot be linked here, which
    // is exactly why the question it asks lives in core.

    void aTranscriptionNobodyHasTypedInIsUntouched()
    {
        TranscriptionDocument document;
        for (int folio = 0; folio < 3; ++folio) {
            TranscribedPage page;
            // The state ensureTypingRoom() leaves a fresh folio in: somewhere to
            // type, and nothing typed there.
            page.verses.append(TranscribedVerse());
            page.verses.first().words.append(TranscribedWord());
            document.pages.append(page);
        }
        // Filled in on purpose: the manuscript details are the library's record
        // until the transcriber edits them, and editing them goes through
        // setMetadata(), which marks the transcription changed on its own
        // account. This overload asks about folios only.
        document.metadata.manuscriptName = QStringLiteral("MS Oo.1.32");
        document.metadata.shelfmark = QStringLiteral("MS Oo.1.32");

        QVERIFY(isUntouched(document));
    }

    void oneWordAnywhereMakesTheWholeTranscriptionTouched()
    {
        TranscriptionDocument document;
        document.pages.append(TranscribedPage());
        document.pages.append(TranscribedPage());
        document.pages.last().verses.append(TranscribedVerse());
        document.pages.last().verses.first().words.append(word("בְּרֵאשִׁית", ""));

        QVERIFY(!isUntouched(document));
    }

    void aVerseNumberedOnTheLastFolioCountsToo()
    {
        TranscriptionDocument document;
        document.pages.append(TranscribedPage());
        document.pages.append(TranscribedPage());
        document.pages.last().verses.append(verse(QStringLiteral("12")));

        QVERIFY(!isUntouched(document));
    }

    void aTranscriptionWithNoFoliosAtAllIsUntouched()
    {
        QVERIFY(isUntouched(TranscriptionDocument()));
    }

    // A scan's id is whatever its catalogue said. A catalogue that names no
    // manuscript has only the manifest address to give, and an entry name with
    // "https://" in it is refused by the archive writer — the doubled slash does
    // not survive being cleaned — so the transcription could not be saved, and
    // because a folio is committed on the way off it, the page could not be
    // turned. isSafeEntryPath() is private, so these restate its condition.

    void aLocalFolioIsNamedExactlyAsItAlwaysWas()
    {
        // The name every transcription in the field already uses. It must not
        // move, or a re-save would rename the folios inside an existing file.
        QCOMPARE(
            imageEntryFor(0, QStringLiteral("folio.png")),
            QStringLiteral("images/001-folio.png"));
    }

    void aScanIdThatIsAnAddressStillNamesAnArchiveEntry()
    {
        const QString entry = imageEntryFor(
            0,
            QStringLiteral("https://gallica.bnf.fr/iiif/ark:/12148/"
                           "btv1b10720220s/manifest.json-0001.jpg"));

        QVERIFY(entry.startsWith(QStringLiteral("images/")));
        // Exactly what ProjectStorage checks before it will write the entry.
        QCOMPARE(QDir::cleanPath(entry), entry);
        QVERIFY(!QDir::isAbsolutePath(entry));
        const QString name = entry.mid(QStringLiteral("images/").size());
        QVERIFY(!name.contains(QLatin1Char('/')));
        QVERIFY(!name.contains(QLatin1Char('\\')));
        QVERIFY(!name.contains(QLatin1Char(':')));
    }

    void twoFoliosOfOneScanNeverShareAnEntryName()
    {
        // The entry name is also what the image pane and the grid compare to
        // decide whether to repaint, so a collision would be a page turn with
        // the picture staying where it was.
        const QString address = QStringLiteral("https://example.org/iiif/x/manifest.json");
        QVERIFY(imageEntryFor(0, address) != imageEntryFor(1, address));
    }

    void twoIdsThatDifferOnlyInPunctuationStillGetTheirOwnEntries()
    {
        // The sanitised name cannot tell these apart — both fragments come out
        // "a_b" — which is why the folio's place in the document is what makes
        // an entry unique, and why the sanitiser is not asked to.
        QCOMPARE(archiveNameFragment(QStringLiteral("a:b")), QStringLiteral("a_b"));
        QCOMPARE(archiveNameFragment(QStringLiteral("a/b")), QStringLiteral("a_b"));
        QVERIFY(
            imageEntryFor(0, QStringLiteral("a:b")) != imageEntryFor(1, QStringLiteral("a/b")));
    }

    void anIdThatTriesToClimbOutOfTheArchiveCannot()
    {
        const QString entry = imageEntryFor(0, QStringLiteral("../../etc/passwd"));
        QCOMPARE(QDir::cleanPath(entry), entry);
        QVERIFY(entry.startsWith(QStringLiteral("images/")));
        QVERIFY(!entry.contains(QStringLiteral("..")));
    }

    void anIdWithNothingUsableInItStillLeavesAName()
    {
        const QString hebrewOnly = QString::fromUtf8("חזון יוחנן");
        const QString entry = imageEntryFor(4, hebrewOnly);
        QCOMPARE(entry, QStringLiteral("images/005"));
        QVERIFY(imageEntryFor(4, hebrewOnly) != imageEntryFor(5, hebrewOnly));
    }

    void anIdLongEnoughToBreakAFilesystemIsCutShort()
    {
        // A catalogue is a remote string; MILAH_MANUSCRIPT_URL points Milah at
        // whichever one an institution cares to serve.
        const QString entry =
            imageEntryFor(0, QString(500, QLatin1Char('x')) + QStringLiteral("-0007.jpg"));
        QVERIFY(entry.size() < 128);
        // Cut from the front, because the tail is what tells one folio from the
        // next.
        QVERIFY(entry.endsWith(QStringLiteral("-0007.jpg")));
    }

    void aScanWhoseIdIsAnAddressCanActuallyBeSaved()
    {
        // The regression itself. Before the entry name was sanitised this failed
        // with "Unsafe project entry path", and the transcriber could neither
        // save nor turn the page.
        TranscriptionDocument document;
        TranscribedPage page;
        page.imageEntry = imageEntryFor(
            0,
            QStringLiteral("https://gallica.bnf.fr/iiif/ark:/12148/"
                           "btv1b10720220s/manifest.json-0001.jpg"));
        page.imageName = QStringLiteral("1");
        document.pages.append(page);

        const QByteArray folio = QByteArrayLiteral("\x89PNG\r\n\x1a\n not really a picture");
        QHash<QString, QByteArray> images;
        images.insert(page.imageEntry, folio);

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("du-tillet.trscrpt"));

        QString error;
        QVERIFY2(
            ProjectStorage::saveToPath(
                path,
                payloadToJson(transcriptionPayload(document, images)),
                &error,
                ProjectStorage::ImageEntryLimit),
            qPrintable(error));

        const QJsonObject read =
            ProjectStorage::loadFromPath(path, &error, ProjectStorage::ImageEntryLimit);
        QVERIFY2(!read.isEmpty(), qPrintable(error));
        QCOMPARE(
            transcriptionImages(payloadFromJson(read)).value(page.imageEntry), folio);

        // And the name this replaced is still refused, so the test above is
        // testing the fix rather than testing nothing. A test that only asserts
        // a save succeeds would pass just as happily if nothing had ever been
        // wrong with the name.
        TranscriptionDocument unsanitised = document;
        unsanitised.pages.first().imageEntry =
            QStringLiteral("images/https://gallica.bnf.fr/iiif/ark:/12148/"
                           "btv1b10720220s/manifest.json-0001.jpg");
        QHash<QString, QByteArray> rawImages;
        rawImages.insert(unsanitised.pages.first().imageEntry, folio);

        QVERIFY(!ProjectStorage::saveToPath(
            folder.filePath(QStringLiteral("refused.trscrpt")),
            payloadToJson(transcriptionPayload(unsanitised, rawImages)),
            &error,
            ProjectStorage::ImageEntryLimit));
        QVERIFY(error.contains(QStringLiteral("Unsafe")));
    }

    void pastedTextDividesIntoWordsAndVerses()
    {
        // What a transcriber copies off a catalogue page or another
        // transcription: running text with the verse numbers still in it.
        const QList<TranscribedVerse> verses = parseTranscribedText(
            QString::fromUtf8("1 בְּרֵאשִׁית בָּרָא 2 אֱלֹהִים"));

        QCOMPARE(verses.size(), 2);
        QCOMPARE(verses.at(0).number, QStringLiteral("1"));
        QCOMPARE(verses.at(0).words.size(), 2);
        QCOMPARE(verses.at(0).words.at(0).hebrew, QString::fromUtf8("בְּרֵאשִׁית"));
        QCOMPARE(verses.at(1).number, QStringLiteral("2"));
        QCOMPARE(verses.at(1).words.size(), 1);
    }

    void aFragmentWithNoNumberIsStillOneVerse()
    {
        // A piece cut out of the middle of a chapter begins mid-verse. Refusing
        // it for opening without a number would refuse the commonest paste.
        const QList<TranscribedVerse> verses =
            parseTranscribedText(QString::fromUtf8("אֱלֹהִים אֵת"));
        QCOMPARE(verses.size(), 1);
        QVERIFY(verses.constFirst().number.isEmpty());
        QCOMPARE(verses.constFirst().words.size(), 2);
    }

    void pastedWhitespaceIsWhateverItIs()
    {
        // Tabs, newlines and runs of spaces all divide words: text copied out
        // of a PDF or a table arrives full of them.
        const QList<TranscribedVerse> verses =
            parseTranscribedText(QStringLiteral("  1\tא\n\nב  "));
        QCOMPARE(verses.size(), 1);
        QCOMPARE(verses.constFirst().number, QStringLiteral("1"));
        QCOMPARE(verses.constFirst().words.size(), 2);

        QVERIFY(parseTranscribedText(QStringLiteral("   ")).isEmpty());
        QVERIFY(parseTranscribedText(QString()).isEmpty());
    }

    void aHeadingSaysAsMuchAsItKnows()
    {
        TranscribedPage page = samplePage();
        // Book, chapter and number all known.
        QCOMPARE(verseHeading(page, 0), QStringLiteral("Gen 4:1"));
        // The third verse opens a chapter, and the heading follows it.
        QCOMPARE(verseHeading(page, 2), QStringLiteral("Gen 5:1"));

        // A folio not yet identified still knows its chapter from the toolbar.
        page.book.clear();
        QCOMPARE(verseHeading(page, 0), QStringLiteral("4:1"));

        // And a verse just begun has nothing but the fact that it is one.
        page.verses[0].number.clear();
        QCOMPARE(verseHeading(page, 0), QStringLiteral("Unnumbered"));
    }

    void theLibraryNameFollowsTheOsisId()
    {
        QCOMPARE(
            libraryFileName(QStringLiteral("Rev"), QStringLiteral("Sloane237")),
            QStringLiteral("Rev_Sloane237_hebrew_commented.osis"));
        // The same id addressing already uses, not a separate code: a John
        // transcription is John_.
        QCOMPARE(
            libraryFileName(QStringLiteral("John"), QStringLiteral("Ebr530")),
            QStringLiteral("John_Ebr530_hebrew_commented.osis"));
    }

    void aManuscriptNameIsMadeFitForAFilename()
    {
        // Free text a transcriber typed, and half of what they might type is
        // punctuation Windows refuses outright.
        QCOMPARE(
            libraryFileName(QStringLiteral("Rev"), QStringLiteral("Vat. ebr. 530")),
            QStringLiteral("Rev_Vat.ebr.530_hebrew_commented.osis"));
        QCOMPARE(
            libraryFileName(QStringLiteral("Rev"), QStringLiteral("MS Oo.1/32: A?")),
            QStringLiteral("Rev_MSOo.132A_hebrew_commented.osis"));
        // And something has to name it when they typed nothing at all.
        QCOMPARE(
            libraryFileName(QStringLiteral("Rev"), QString()),
            QStringLiteral("Rev_Transcription_hebrew_commented.osis"));
    }

    void aLibraryNameIsNeverATranslation()
    {
        // The suffix, and nothing in the file, is what tells Milah a library
        // text is a translation rather than a witness — so a manuscript called
        // "Ebr530_translation" must not become one.
        const QString name =
            libraryFileName(QStringLiteral("Rev"), QStringLiteral("Ebr530_translation"));
        QVERIFY2(!name.endsWith(QStringLiteral("_translation.osis")), qPrintable(name));
        QVERIFY(name.endsWith(QStringLiteral("_hebrew_commented.osis")));
    }

    void aBookOutsideTheCanonIsFiledUnderItsOwnId()
    {
        QCOMPARE(
            libraryFileName(QStringLiteral("Tob"), QStringLiteral("Sloane237")),
            QStringLiteral("Tob_Sloane237_hebrew_commented.osis"));
    }

    void aNoteSurvivesTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        document.pages[0].verses[0].words[1].note =
            QStringLiteral("The second letter is doubtful.");

        const TranscriptionDocument restored =
            restoreTranscription(transcriptionPayload(document, {}));

        QCOMPARE(
            restored.pages.at(0).verses.at(0).words.at(1).note,
            QStringLiteral("The second letter is doubtful."));
        // And a word nobody remarked on carries nothing.
        QVERIFY(restored.pages.at(0).verses.at(0).words.at(0).note.isEmpty());
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
