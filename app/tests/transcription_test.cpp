#include "core/project.h"
#include "core/transcription.h"
#include "project_storage.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
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

/// A folio of James ending on `number` with `words` words in it, and the empty
/// verse every folio keeps at the end for typing into — which the resume has to
/// see past rather than answer with.
TranscribedPage filledPage(const QString &number, int words)
{
    TranscribedPage page;
    page.book = QStringLiteral("JAS");
    page.firstChapter = 1;
    page.verses.append(verse(number));
    for (int index = 0; index < words; ++index) {
        page.verses.last().words.append(word("דבר", "word"));
    }
    page.verses.append(verse(QString()));
    return page;
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

    void theSuggestedNameCarriesTheBookAndTheManuscript()
    {
        TranscriptionDocument document = sampleDocument();
        document.metadata.shelfmark = QStringLiteral("Ebr. 530");
        // The book of the folio on screen, and the shelfmark with its
        // punctuation taken out. A folder of transcriptions of Luke is only
        // navigable if the manuscript is in the name.
        QCOMPARE(
            transcriptionFileStem(document, 0), QStringLiteral("Gen_Ebr530"));

        // Spaces go the same way as stops.
        document.metadata.shelfmark = QStringLiteral("Gaster Hebrew MS 1616");
        QCOMPARE(
            transcriptionFileStem(document, 0),
            QStringLiteral("Gen_GasterHebrewMS1616"));
    }

    void theShelfmarkIsPreferredToTheManuscriptName()
    {
        TranscriptionDocument document = sampleDocument();
        document.metadata.manuscriptName = QStringLiteral("Luke");
        document.metadata.shelfmark = QStringLiteral("Vat. ebr. 530");
        QCOMPARE(
            transcriptionFileStem(document, 0), QStringLiteral("Gen_Vatebr530"));

        // The name only where there is no shelfmark — and there it is usually
        // the work rather than the copy, which is why it is second.
        document.metadata.shelfmark.clear();
        QCOMPARE(transcriptionFileStem(document, 0), QStringLiteral("Gen_Luke"));
    }

    void theBookComesFromWhicheverFolioNamesOne()
    {
        TranscriptionDocument document = sampleDocument();
        document.metadata.shelfmark = QStringLiteral("Ebr. 530");

        // A cover or a flyleaf opened before the transcriber has said what they
        // are reading. The book is still known — the folio after it says so.
        TranscribedPage cover;
        cover.imageName = QStringLiteral("cover.png");
        document.pages.prepend(cover);
        QCOMPARE(
            transcriptionFileStem(document, 0), QStringLiteral("Gen_Ebr530"));

        // And an index nothing is open at, which is what the archive writer
        // has to work from.
        QCOMPARE(
            transcriptionFileStem(document, -1), QStringLiteral("Gen_Ebr530"));
        QCOMPARE(
            transcriptionFileStem(document, 99), QStringLiteral("Gen_Ebr530"));
    }

    void halfANameIsBetterThanNone()
    {
        TranscriptionDocument document;
        document.pages = {samplePage()};
        // No manuscript said yet: the book alone, rather than a trailing
        // underscore.
        QCOMPARE(transcriptionFileStem(document, 0), QStringLiteral("Gen"));

        // And no book: the manuscript alone.
        document.pages[0].book.clear();
        document.metadata.shelfmark = QStringLiteral("Ebr. 530");
        QCOMPARE(transcriptionFileStem(document, 0), QStringLiteral("Ebr530"));

        // A transcription that has said nothing about itself still has to be
        // offered something.
        document.metadata.shelfmark.clear();
        QCOMPARE(
            transcriptionFileStem(document, 0), QStringLiteral("Transcription"));
        QCOMPARE(
            transcriptionFileStem(TranscriptionDocument{}, 0),
            QStringLiteral("Transcription"));
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

    /// The two things filling and training write, and the two that a folio
    /// worked on months apart depends on surviving: which line a word came off
    /// and where a hand-marked line ends.
    void aLineAndAMarkedBreakSurviveTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        document.pages[0].verses[0].words[0].line = 4;
        document.pages[0].verses[0].words[0].endsLine = true;

        const TranscriptionDocument restored =
            restoreTranscription(transcriptionPayload(document, {}));

        const TranscribedWord &read = restored.pages.at(0).verses.at(0).words.at(0);
        QCOMPARE(read.line, 4);
        QVERIFY(read.endsLine);
        // A word off no line reads as -1 rather than 0, which would put it on
        // the first line of the folio.
        QCOMPARE(restored.pages.at(0).verses.at(0).words.at(1).line, -1);
        QVERIFY(!restored.pages.at(0).verses.at(0).words.at(1).endsLine);
    }

    /// Where a fill stopped, so the next leaf can carry on from it — a folio
    /// ends mid-verse far more often than not, and a verse alone cannot say how
    /// far in.
    void whereAFillStoppedSurvivesTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        document.pages[0].fillEndVerse = QStringLiteral("Jas.1.14");
        document.pages[0].fillEndWord = 5;

        const TranscribedPage read =
            restoreTranscription(transcriptionPayload(document, {})).pages.at(0);
        QCOMPARE(read.fillEndVerse, QStringLiteral("Jas.1.14"));
        QCOMPARE(read.fillEndWord, 5);

        // Half an answer is worse than none: a verse with no count says where to
        // resume without saying how far in, so neither is written.
        TranscriptionDocument half = sampleDocument();
        half.pages[0].fillEndVerse = QStringLiteral("Jas.1.14");
        const TranscribedPage partial =
            restoreTranscription(transcriptionPayload(half, {})).pages.at(0);
        QVERIFY(partial.fillEndVerse.isEmpty());
        QCOMPARE(partial.fillEndWord, -1);
    }

    /// Which transcription the folios were filled from, so the next leaf does
    /// not send the transcriber back through the file dialog for a file they
    /// already chose.
    void theTranscriptionRemembersWhatItWasFilledFrom()
    {
        TranscriptionDocument document = sampleDocument();
        document.fillSource = QStringLiteral("C:/manuscripts/JAS_Cochin.osis");

        QCOMPARE(
            restoreTranscription(transcriptionPayload(document, {})).fillSource,
            QStringLiteral("C:/manuscripts/JAS_Cochin.osis"));

        // Every file written before this existed has no such key, and must read
        // as "ask once" rather than fail to open.
        QCOMPARE(restoreTranscription(transcriptionPayload(sampleDocument(), {})).fillSource,
                 QString());
    }

    /// The nearest earlier folio that holds text, not the first one.
    ///
    /// A leaf skipped — a blank verso, a plate, a folio left for later — must
    /// not send the next one back two places in the book. The menu and the fill
    /// both ask this, so if it were wrong the menu would promise a place the
    /// fill does not go to.
    void carryingOnLooksAtTheNearestFolioWithText()
    {
        TranscriptionDocument document;
        document.pages = {filledPage(QStringLiteral("14"), 5), TranscribedPage{},
                          filledPage(QStringLiteral("9"), 2), TranscribedPage{}};

        // Folio 4 carries on from folio 3, not from folio 1.
        const ResumePoint fromThird = resumeFill(document, 3);
        QCOMPARE(fromThird.verse, QStringLiteral("JAS.1.9"));
        QCOMPARE(fromThird.word, 2);

        // Folio 3 skips the empty folio 2 and reaches back to folio 1.
        QCOMPARE(resumeFill(document, 2).verse, QStringLiteral("JAS.1.14"));

        // The first folio has nothing behind it, and neither has a document
        // nobody has filled — which is what makes the menu offer one entry
        // rather than two.
        QVERIFY(!resumeFill(document, 0).isValid());
        QVERIFY(!resumeFill(TranscriptionDocument{}, 0).isValid());

        // A page index past the end answers from the last folio rather than
        // reading off the end of the list.
        QCOMPARE(resumeFill(document, 99).verse, QStringLiteral("JAS.1.9"));
    }

    /// **The reason the resume is read rather than remembered.**
    ///
    /// A fill records where it stopped, and then the transcriber walks the folio
    /// and corrects it — which is the whole point of filling. Deleting a word
    /// the recogniser invented changes what the leaf holds and cannot change a
    /// number written before it happened. Counting what is on the page now moves
    /// with the correction; a stored pair silently does not, and the next folio
    /// resumes a word late for the rest of the book.
    void correctingAFolioMovesWhereTheNextOneCarriesOnFrom()
    {
        TranscriptionDocument document;
        document.pages = {filledPage(QStringLiteral("25"), 4), TranscribedPage{}};
        // Written by the fill and deliberately left stale, which is exactly the
        // state a corrected folio is in.
        document.pages[0].fillEndVerse = QStringLiteral("JAS.1.25");
        document.pages[0].fillEndWord = 4;
        QCOMPARE(resumeFill(document, 1).word, 4);

        document.pages[0].verses.first().words.removeLast();
        QCOMPARE(resumeFill(document, 1).word, 3);
        // The stale note is still there, and is still ignored.
        QCOMPARE(document.pages.at(0).fillEndWord, 4);

        // A word added by hand counts too — the leaf is the record.
        document.pages[0].verses.first().words.append(TranscribedWord{});
        document.pages[0].verses.first().words.append(TranscribedWord{});
        QCOMPARE(resumeFill(document, 1).word, 5);
    }

    /// A folio ending on a chapter break resumes inside that chapter, not the
    /// one the leaf opened in — the id is built from where the verse sits, not
    /// from the top of the page.
    void theResumeVerseCarriesTheChapterItIsIn()
    {
        TranscriptionDocument document;
        document.pages = {filledPage(QStringLiteral("3"), 2), TranscribedPage{}};
        document.pages[0].verses.first().startsNewChapter = true;
        QCOMPARE(resumeFill(document, 1).verse, QStringLiteral("JAS.2.3"));
    }

    /// A box held out of the work survives the archive, and a file written
    /// before boxes could be held out reads as all-text, which is what it was.
    void aHeldOutBoxSurvivesTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        document.pages[0].verses[0].words[1].marginal = true;

        const TranscribedPage read =
            restoreTranscription(transcriptionPayload(document, {})).pages.at(0);
        QVERIFY(read.verses.at(0).words.at(1).marginal);
        QVERIFY(!read.verses.at(0).words.at(0).marginal);
    }

    /// What the machine read survives the fill that overwrote it, and the line
    /// the pour began on survives the archive.
    ///
    /// Both exist for the same moment: a box turning out to be a marginal note.
    /// The word of the work poured onto it belongs further down the passage, so
    /// the leaf is laid again from that line — which needs the start line — and
    /// the box goes back to what was read there, which needs the reading.
    void theMachinesReadingAndTheFillsStartSurviveTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        document.pages[0].verses[0].words[0].recognised = QString::fromUtf8("\xd7\x91\xd7\xa8\xd7\x90");
        document.pages[0].fillStartLine = 7;
        document.pages[0].fillStartVerse = QStringLiteral("JAS.1.25");
        document.pages[0].fillStartWord = 4;

        const TranscribedPage read =
            restoreTranscription(transcriptionPayload(document, {})).pages.at(0);
        QCOMPARE(read.verses.at(0).words.at(0).recognised,
                 QString::fromUtf8("\xd7\x91\xd7\xa8\xd7\x90"));
        QCOMPARE(read.fillStartLine, 7);
        QCOMPARE(read.fillStartVerse, QStringLiteral("JAS.1.25"));
        QCOMPARE(read.fillStartWord, 4);

        // A file written before any of it existed: nothing to go back to, and no
        // record of where the pour began. This is the state that has to produce
        // a message rather than a re-flow laid from the wrong place.
        const TranscribedPage old =
            restoreTranscription(transcriptionPayload(sampleDocument(), {})).pages.at(0);
        QVERIFY(old.verses.at(0).words.at(0).recognised.isEmpty());
        QCOMPARE(old.fillStartLine, -1);
        QVERIFY(old.fillStartVerse.isEmpty());
        QCOMPARE(old.fillStartWord, -1);

        // Half a start is no start: a verse with no word count says where the
        // pour began without saying how far in, so neither is written.
        TranscriptionDocument half = sampleDocument();
        half.pages[0].fillStartVerse = QStringLiteral("JAS.1.25");
        const TranscribedPage partial =
            restoreTranscription(transcriptionPayload(half, {})).pages.at(0);
        QVERIFY(partial.fillStartVerse.isEmpty());
        QCOMPARE(partial.fillStartWord, -1);
    }

    /// The line the segmenter drew survives the archive, because training cuts
    /// its strips from it and a folio is trained on months after it was read.
    void theLineTheSegmenterDrewSurvivesTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        TranscribedLine drawn;
        drawn.index = 3;
        drawn.baseline = {QPoint(588, 73), QPoint(658, 72), QPoint(720, 66)};
        drawn.boundary = {QPoint(597, 54), QPoint(660, 57), QPoint(679, 103)};
        document.pages[0].lines = {drawn};

        const TranscribedPage read =
            restoreTranscription(transcriptionPayload(document, {})).pages.at(0);
        QCOMPARE(read.lines.size(), 1);
        QCOMPARE(read.lines.first().index, 3);
        QCOMPARE(read.lines.first().baseline.size(), 3);
        QCOMPARE(read.lines.first().baseline.at(2), QPoint(720, 66));
        QCOMPARE(read.lines.first().boundary.size(), 3);

        // A file written before Milah kept them reads as none, which is what
        // sends the training export back to its own invention.
        QVERIFY(restoreTranscription(transcriptionPayload(sampleDocument(), {}))
                    .pages.at(0)
                    .lines.isEmpty());
    }

    /// A line whose length the transcriber corrected keeps that length. It is a
    /// fact about the leaf, read off the picture — not about which transcription
    /// was poured onto it — so it outlives the fill that prompted it, and the
    /// re-flows that run over it afterwards.
    void aCorrectedLineLengthSurvivesTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        document.pages[0].lineWords.insert(21, 13);
        // Nought is an answer, not an absence: the whole line belongs further
        // down. Written as such, and read back as such.
        document.pages[0].lineWords.insert(22, 0);

        const TranscribedPage read =
            restoreTranscription(transcriptionPayload(document, {})).pages.at(0);
        QCOMPARE(read.lineWords.size(), 2);
        QCOMPARE(read.lineWords.value(21), 13);
        QVERIFY(read.lineWords.contains(22));
        QCOMPARE(read.lineWords.value(22), 0);

        // A file written before this existed reads as nothing said, which sends
        // every line back to its box count.
        QVERIFY(restoreTranscription(transcriptionPayload(sampleDocument(), {}))
                    .pages.at(0)
                    .lineWords.isEmpty());
    }

    /// Marginalia leave the running text and arrive as a note on the line they
    /// stand beside.
    ///
    /// They are on the leaf but not in the work: exporting one as a word of a
    /// verse would put it into the running text of an edition, somewhere the
    /// scribe never wrote it. Losing it would be worse still, so it becomes a
    /// note — which is what a marginal gloss is.
    void marginaliaBecomeNotesOnTheirLine()
    {
        TranscriptionDocument document;
        TranscribedPage page;
        page.book = QStringLiteral("JAS");
        TranscribedVerse verse = ::verse(QStringLiteral("1"));
        verse.words = {word("\xd7\x90", "a"), word("\xd7\x91", "b"), word("\xd7\x92", "c")};
        for (int index = 0; index < 3; ++index) {
            verse.words[index].line = 0;
        }
        verse.words[2].marginal = true;
        page.verses = {verse};
        document.pages = {page};

        const TranscribedVerse &out = withoutMarginalia(document).pages.at(0).verses.at(0);
        QCOMPARE(out.words.size(), 2);
        QCOMPARE(out.words.at(1).note, QString::fromUtf8("\xd7\x92"));
        // And a note the transcriber had already written is kept, not replaced.
        QCOMPARE(out.words.at(0).note, QString());
    }

    /// A note the recogniser gave a line of its own — which is most of them,
    /// since marginalia sit beside the text block rather than inside it —
    /// attaches to the nearest line above, which is the text it stands next to.
    void aNoteOnItsOwnLineAttachesToTheLineAbove()
    {
        TranscriptionDocument document;
        TranscribedPage page;
        page.book = QStringLiteral("JAS");
        TranscribedVerse verse = ::verse(QStringLiteral("1"));
        verse.words = {word("\xd7\x90", "a"), word("\xd7\x91", "b"), word("\xd7\x92", "c")};
        verse.words[0].line = 0;
        verse.words[1].line = 1;
        verse.words[2].line = 2;
        verse.words[2].marginal = true;
        page.verses = {verse};
        document.pages = {page};

        const TranscribedVerse &out = withoutMarginalia(document).pages.at(0).verses.at(0);
        QCOMPARE(out.words.size(), 2);
        QCOMPARE(out.words.at(1).note, QString::fromUtf8("\xd7\x92"));
    }

    /// A folio of nothing but marginalia strands them rather than inventing a
    /// word for them to be a note on — which would put text into an export that
    /// the transcriber never wrote.
    void aFolioOfNothingButMarginaliaInventsNoAnchor()
    {
        TranscriptionDocument document;
        TranscribedPage page;
        page.book = QStringLiteral("JAS");
        TranscribedVerse verse = ::verse(QStringLiteral("1"));
        verse.words = {word("\xd7\x90", "a")};
        verse.words[0].line = 0;
        verse.words[0].marginal = true;
        page.verses = {verse};
        document.pages = {page};

        const TranscribedVerse &out = withoutMarginalia(document).pages.at(0).verses.at(0);
        QVERIFY(out.words.isEmpty());
    }

    /// Text with nothing to place it — no book, or a heading rather than a verse
    /// — is passed over rather than answered with half an id.
    void aFolioThatCannotSayWhereItIsIsPassedOver()
    {
        TranscriptionDocument document;
        document.pages = {filledPage(QStringLiteral("14"), 5),
                          filledPage(QStringLiteral("20"), 3), TranscribedPage{}};
        document.pages[1].book.clear();
        QCOMPARE(resumeFill(document, 2).verse, QStringLiteral("JAS.1.14"));

        // And a folio whose last verse is an incipit rather than a numbered one.
        document.pages[1].book = QStringLiteral("JAS");
        document.pages[1].verses.first().number = QStringLiteral("0");
        QCOMPARE(resumeFill(document, 2).verse, QStringLiteral("JAS.1.14"));
    }

    void aBoxAndAnUncheckedFlagSurviveTheArchive()
    {
        TranscriptionDocument document = sampleDocument();
        document.pages[0].verses[0].words[0].box = QRect(700, 200, 180, 55);
        document.pages[0].verses[0].words[0].unchecked = true;

        const TranscriptionDocument restored =
            restoreTranscription(transcriptionPayload(document, {}));

        const TranscribedWord &read = restored.pages.at(0).verses.at(0).words.at(0);
        QCOMPARE(read.box, QRect(700, 200, 180, 55));
        QVERIFY(read.unchecked);
        // And the word beside it, which nobody's machine has touched.
        QVERIFY(restored.pages.at(0).verses.at(0).words.at(1).box.isNull());
        QVERIFY(!restored.pages.at(0).verses.at(0).words.at(1).unchecked);
    }

    void aTypedWordWritesNeitherOfThem()
    {
        // What lets a .trscrpt made by this version open in one that has never
        // heard of handwriting recognition: an ordinary transcription contains
        // not one mention of it. The whole scheme for adding fields without
        // moving the format version rests on this, so it is asserted rather
        // than assumed.
        const QJsonObject manifest =
            transcriptionPayload(sampleDocument(), {}).manifest;
        const QJsonObject word = manifest.value(QStringLiteral("pages"))
                                     .toArray()
                                     .at(0)
                                     .toObject()
                                     .value(QStringLiteral("verses"))
                                     .toArray()
                                     .at(0)
                                     .toObject()
                                     .value(QStringLiteral("words"))
                                     .toArray()
                                     .at(0)
                                     .toObject();

        QVERIFY(!word.isEmpty());
        QVERIFY(!word.contains(QStringLiteral("box")));
        QVERIFY(!word.contains(QStringLiteral("unchecked")));
    }

    void aBoxWrittenWrongIsNoBoxAtAll()
    {
        // Three numbers where four were meant would otherwise become a
        // rectangle at the origin, and the overlay would draw it there with
        // every appearance of meaning it.
        TranscriptionDocument document = sampleDocument();
        MilahProjectPayload payload = transcriptionPayload(document, {});

        QJsonArray pages = payload.manifest.value(QStringLiteral("pages")).toArray();
        QJsonObject page = pages.at(0).toObject();
        QJsonArray verses = page.value(QStringLiteral("verses")).toArray();
        QJsonObject verse = verses.at(0).toObject();
        QJsonArray words = verse.value(QStringLiteral("words")).toArray();
        QJsonObject word = words.at(0).toObject();
        word.insert(QStringLiteral("box"), QStringLiteral("700 200 180"));
        words.replace(0, word);
        verse.insert(QStringLiteral("words"), words);
        verses.replace(0, verse);
        page.insert(QStringLiteral("verses"), verses);
        pages.replace(0, page);
        payload.manifest.insert(QStringLiteral("pages"), pages);

        const TranscriptionDocument restored = restoreTranscription(payload);
        QVERIFY(restored.pages.at(0).verses.at(0).words.at(0).box.isNull());
        // The text is not the casualty of a bad rectangle.
        QVERIFY(!restored.pages.at(0).verses.at(0).words.at(0).hebrew.isEmpty());
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

    void zeroIsThePreambleAndNothingElseIs()
    {
        QVERIFY(isPreamble(QStringLiteral("0")));
        // Written out longer, and divided the way any verse may be.
        QVERIFY(isPreamble(QStringLiteral("00")));
        QVERIFY(isPreamble(QStringLiteral("0a")));
        QVERIFY(isPreamble(QStringLiteral(" 0 ")));

        QVERIFY(!isPreamble(QStringLiteral("1")));
        // The one that would go wrong if this looked for a zero anywhere in it.
        QVERIFY(!isPreamble(QStringLiteral("10")));
        QVERIFY(!isPreamble(QStringLiteral("100")));
        QVERIFY(!isPreamble(QString()));
        QVERIFY(!isPreamble(QStringLiteral("a")));
    }

    void aPreambleIsHeadedForWhatItIs()
    {
        TranscribedPage page = samplePage();
        page.verses[0].number = QStringLiteral("0");
        // Not "Gen 4:0", which says nothing to a transcriber who has not been
        // told the convention.
        QCOMPARE(verseHeading(page, 0), QStringLiteral("Gen 4 preamble"));

        page.book.clear();
        QCOMPARE(verseHeading(page, 0), QStringLiteral("4 preamble"));
    }

    void aPreambleStillHasAnIdInside()
    {
        // It is written out as a div rather than a verse, but everything inside
        // Milah keys it by an id all the same: its notes hang off this, and the
        // export sorts by it.
        TranscribedPage page = samplePage();
        page.verses[0].number = QStringLiteral("0");
        QCOMPARE(transcribedVerseId(page, 0), QStringLiteral("Gen.4.0"));
    }

    void aPastedZeroIsAVerseNumberLikeAnyOther()
    {
        const QList<TranscribedVerse> verses =
            parseTranscribedText(QString::fromUtf8("0 אֱלֹהִים 1 אֵת"));
        QCOMPARE(verses.size(), 2);
        QCOMPARE(verses.at(0).number, QStringLiteral("0"));
        QCOMPARE(verses.at(1).number, QStringLiteral("1"));
    }

    void theCataloguingDetailsSurviveTheArchive()
    {
        // What the download window shows beside a manuscript's title. A
        // transcription that lost them on being reopened would answer those
        // columns once and never again.
        QTemporaryDir home;
        QVERIFY(home.isValid());

        TranscriptionDocument original = sampleDocument();
        original.metadata.shelfmark = QStringLiteral("Sloane MS 237");
        original.metadata.folios = QStringLiteral("1r–4v");
        original.metadata.material = QStringLiteral("Parchment");
        original.metadata.provenance = QStringLiteral("From the Sloane bequest.");
        original.metadata.translatedFrom = QStringLiteral("Translated from the Greek");
        original.metadata.translatedFromCertainty = QStringLiteral("uncertain");
        original.metadata.exemplar = QStringLiteral("Copied from Cambridge MS Oo.1.32");

        QString error;
        const QString path = home.filePath(QStringLiteral("details.trscrpt"));
        QVERIFY2(
            ProjectStorage::saveToPath(
                path,
                payloadToJson(transcriptionPayload(original, {})),
                &error,
                ProjectStorage::ImageEntryLimit),
            qPrintable(error));
        const TranscriptionDocument restored = restoreTranscription(payloadFromJson(
            ProjectStorage::loadFromPath(path, &error, ProjectStorage::ImageEntryLimit)));

        QCOMPARE(restored.metadata.shelfmark, original.metadata.shelfmark);
        QCOMPARE(restored.metadata.folios, original.metadata.folios);
        QCOMPARE(restored.metadata.material, original.metadata.material);
        QCOMPARE(restored.metadata.provenance, original.metadata.provenance);
        QCOMPARE(restored.metadata.translatedFrom, original.metadata.translatedFrom);
        // The verdict travels with the answer. Losing it would turn "probably
        // Greek" back into "Greek" on the next open.
        QCOMPARE(
            restored.metadata.translatedFromCertainty,
            original.metadata.translatedFromCertainty);
        QCOMPARE(restored.metadata.exemplar, original.metadata.exemplar);
    }

    void aTranscriptionWrittenBeforeTheseFieldsStillOpens()
    {
        // They are written only when they say something, so a file made by an
        // earlier Milah simply has no such keys — and must read back as
        // unanswered rather than as unreadable.
        QTemporaryDir home;
        QVERIFY(home.isValid());

        const TranscriptionDocument original = sampleDocument();
        QString error;
        const QString path = home.filePath(QStringLiteral("older.trscrpt"));
        QVERIFY2(
            ProjectStorage::saveToPath(
                path,
                payloadToJson(transcriptionPayload(original, {})),
                &error,
                ProjectStorage::ImageEntryLimit),
            qPrintable(error));
        const TranscriptionDocument restored = restoreTranscription(payloadFromJson(
            ProjectStorage::loadFromPath(path, &error, ProjectStorage::ImageEntryLimit)));

        QVERIFY(restored.metadata.folios.isEmpty());
        QVERIFY(restored.metadata.exemplar.isEmpty());
        // And everything that was written is still there.
        QCOMPARE(restored.metadata.manuscriptName, original.metadata.manuscriptName);
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
