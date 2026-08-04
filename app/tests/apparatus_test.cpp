#include "core/alignment.h"
#include "core/osis.h"
#include "core/serialize.h"
#include "test_data.h"

#include <QFile>
#include <QFileInfo>
#include <QtTest>

using namespace milah;

namespace {

SourceDocument parseSample()
{
    ParseOptions options;
    options.id = QStringLiteral("sloane");
    options.name = QStringLiteral("sloane.osis");
    options.role = SourceRole::Manuscript;
    return parseOsis(QString::fromUtf8(milah_test::kApparatusManuscript), options);
}

QString corpusPath(const QString &variant)
{
    return QStringLiteral("%1/tools/data/01_osis/Rev_Sloane237_%2.osis")
        .arg(QStringLiteral(MILAH_REPO_ROOT), variant);
}

const QStringList &corpusVariants()
{
    static const QStringList variants = {
        QStringLiteral("hebrew"),
        QStringLiteral("hebrew_commented"),
        QStringLiteral("translation"),
    };
    return variants;
}

bool corpusAvailable()
{
    for (const QString &variant : corpusVariants()) {
        if (!QFileInfo::exists(corpusPath(variant))) {
            return false;
        }
    }
    return true;
}

QString readCorpus(const QString &variant)
{
    QFile file(corpusPath(variant));
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

class ApparatusTest final : public QObject
{
    Q_OBJECT

private slots:
    void keepsVersesAndTheirInlineNotes()
    {
        const SourceDocument parsed = parseSample();

        const SourceVerse *verse = parsed.verse(QStringLiteral("Rev.1.1"));
        QVERIFY(verse != nullptr);
        QCOMPARE(verse->text, QString::fromUtf8("ספר הולדת"));
        QVERIFY(!verse->tokens.isEmpty());
        QVERIFY(!verse->tokens.at(0).notes.isEmpty());
        QCOMPARE(verse->tokens.at(0).notes.at(0).text, QStringLiteral("A comment"));

        QStringList verseIds;
        for (const SourceVerse &item : parsed.verses) {
            verseIds.append(item.reference.id);
        }
        QCOMPARE(verseIds, (QStringList{QStringLiteral("Rev.1.1"), QStringLiteral("Rev.2.1")}));
    }

    void capturesTitlesThatBelongToNoVerse()
    {
        const SourceDocument parsed = parseSample();

        QStringList titleTexts;
        for (const SourceTitle &title : parsed.titles) {
            titleTexts.append(title.text);
        }
        QCOMPARE(
            titleTexts,
            (QStringList{QString::fromUtf8("חזון יוחנן"), QString::fromUtf8("השער שני")}));

        const SourceTitle &incipit = parsed.titles.at(0);
        QCOMPARE(incipit.type, QStringLiteral("main"));
        QCOMPARE(incipit.canonical, true);
        QCOMPARE(incipit.book.value_or(QString()), QStringLiteral("Rev"));
        QVERIFY(!incipit.chapter.has_value());
        // A note attached to non-verse text used to be discarded entirely.
        QCOMPARE(incipit.notes.size(), 1);
        QCOMPARE(incipit.notes.at(0).text, QStringLiteral("An incipit note"));

        const SourceTitle &gate = parsed.titles.at(1);
        QCOMPARE(gate.type, QStringLiteral("chapter"));
        QCOMPARE(gate.chapter.value_or(0), 2);
    }

    void capturesMilestonesWithTheirPosition()
    {
        const SourceDocument parsed = parseSample();

        QStringList summary;
        for (const SourceMilestone &milestone : parsed.milestones) {
            summary.append(QStringLiteral("%1|%2|%3")
                               .arg(milestone.type, milestone.n,
                                    milestone.verseId.value_or(QStringLiteral("null"))));
        }
        QCOMPARE(
            summary,
            (QStringList{
                QStringLiteral("pb|1v|null"),
                QStringLiteral("pb|2r|Rev.1.1"),
                QStringLiteral("x-ms-verse|8|Rev.2.1"),
            }));

        QCOMPARE(
            parsed.milestones.at(1).charOffset,
            int(QString::fromUtf8("ספר הולדת").size()));
    }

    void readsTheManuscriptsOwnVerseNumbering()
    {
        const SourceDocument parsed = parseSample();
        QCOMPARE(
            parsed.verse(QStringLiteral("Rev.1.1"))->altNumber.value_or(QString()),
            QStringLiteral("1"));
        QCOMPARE(
            parsed.verse(QStringLiteral("Rev.2.1"))->altNumber.value_or(QString()),
            QStringLiteral("9"));
    }

    void roundTripsNotesTitlesAndMilestones()
    {
        const SourceDocument parsed = parseSample();

        CombinedDraft draft;
        draft.reference =
            VerseReference{QStringLiteral("Rev.1.1"), QStringLiteral("Rev"), 1,
                           QStringLiteral("1")};
        draft.manualText = QString::fromUtf8("ספר הולדת");

        QMap<QString, CombinedDraft> drafts;
        drafts.insert(QStringLiteral("Rev.1.1"), draft);

        WorkMetadata metadata;
        metadata.workId = QStringLiteral("Combined");

        CombinedApparatus apparatus;
        apparatus.titles = parsed.titles;
        for (const SourceMilestone &milestone : parsed.milestones) {
            if (milestone.verseId.value_or(QString()) == QLatin1String("Rev.1.1")) {
                apparatus.milestones.append(milestone);
            }
        }
        apparatus.notes.insert(
            QStringLiteral("Rev.1.1"),
            parsed.verse(QStringLiteral("Rev.1.1"))->tokens.at(0).notes);

        const QString xml = serializeCombinedOsis(drafts, metadata, apparatus);

        // The exporter previously emitted no <note> at all, silently dropping
        // every footnote it had read.
        QVERIFY(xml.contains(QStringLiteral("<note")));
        QVERIFY(xml.contains(QStringLiteral("A comment")));
        QVERIFY(xml.contains(QStringLiteral(R"(<milestone type="pb" n="2r"/>)")));
        QVERIFY(xml.contains(QString::fromUtf8("חזון יוחנן")));

        ParseOptions options;
        options.id = QStringLiteral("round");
        options.name = QStringLiteral("round.osis");
        options.role = SourceRole::Combined;
        const SourceDocument round = parseOsis(xml, options);

        const SourceVerse *verse = round.verse(QStringLiteral("Rev.1.1"));
        QVERIFY(verse != nullptr);
        QCOMPARE(verse->text, QString::fromUtf8("ספר הולדת"));
        QCOMPARE(verse->tokens.at(0).notes.at(0).text, QStringLiteral("A comment"));

        QStringList roundTitles;
        for (const SourceTitle &title : round.titles) {
            roundTitles.append(title.text);
        }
        QVERIFY(roundTitles.contains(QString::fromUtf8("חזון יוחנן")));

        QStringList roundMilestones;
        for (const SourceMilestone &milestone : round.milestones) {
            roundMilestones.append(milestone.n);
        }
        QVERIFY(roundMilestones.contains(QStringLiteral("2r")));
    }

    void escapesApostrophesInText()
    {
        CombinedDraft draft;
        draft.reference =
            VerseReference{QStringLiteral("Rev.1.1"), QStringLiteral("Rev"), 1,
                           QStringLiteral("1")};
        draft.manualText = QStringLiteral("She'ol & <hope>");

        QMap<QString, CombinedDraft> drafts;
        drafts.insert(QStringLiteral("Rev.1.1"), draft);

        const QString xml = serializeCombinedOsis(drafts);
        QVERIFY(xml.contains(QStringLiteral("She&apos;ol &amp; &lt;hope&gt;")));
    }

    // The converter's own output, so the two sides of the pipeline are checked
    // against each other rather than only against a hand-written sample.

    void loadsEveryGeneratedVariantWithoutWarnings()
    {
        if (!corpusAvailable()) {
            QSKIP("The tools/data/01_osis corpus is not available next to this build.");
        }

        for (const QString &variant : corpusVariants()) {
            ParseOptions options;
            options.id = variant;
            options.name = variant;
            options.role = SourceRole::Manuscript;

            const SourceDocument document = parseOsis(readCorpus(variant), options);
            QCOMPARE(document.verses.size(), 33);
            QCOMPARE(document.warnings, QStringList());
        }
    }

    void keepsTheManuscriptsNonVerseText()
    {
        if (!corpusAvailable()) {
            QSKIP("The tools/data/01_osis corpus is not available next to this build.");
        }

        ParseOptions options;
        options.id = QStringLiteral("hebrew");
        options.name = QStringLiteral("hebrew");
        options.role = SourceRole::Manuscript;
        const SourceDocument document =
            parseOsis(readCorpus(QStringLiteral("hebrew")), options);

        const SourceTitle *incipit = nullptr;
        const SourceTitle *chapterTitle = nullptr;
        for (const SourceTitle &title : document.titles) {
            if (!incipit && title.canonical && title.type == QLatin1String("main")) {
                incipit = &title;
            }
            if (!chapterTitle && title.type == QLatin1String("chapter")) {
                chapterTitle = &title;
            }
        }

        QVERIFY(incipit != nullptr);
        QVERIFY(incipit->text.contains(QString::fromUtf8("חֲזוֹן יוֹחָנָן הַקֹּדֶשׁ")));
        QVERIFY(chapterTitle != nullptr);
        QCOMPARE(chapterTitle->text, QString::fromUtf8("הַשַּׁעַר שֵׁנִי"));

        QStringList folios;
        for (const SourceMilestone &milestone : document.milestones) {
            if (milestone.type == QLatin1String("pb")) {
                folios.append(milestone.n);
            }
        }
        QCOMPARE(
            folios,
            (QStringList{
                QStringLiteral("1r"), QStringLiteral("1v"), QStringLiteral("2r"),
                QStringLiteral("2v"), QStringLiteral("3r"), QStringLiteral("3v"),
                QStringLiteral("4r"), QStringLiteral("4v"),
            }));

        // Revelation 1:18 was dropped entirely by the previous extraction.
        const SourceVerse *eighteen = document.verse(QStringLiteral("Rev.1.18"));
        QVERIFY(eighteen != nullptr);
        QVERIFY(eighteen->text.contains(QString::fromUtf8("וְהָחָי וְהָיִיתִי מֵת")));

        const SourceVerse *fifteen = document.verse(QStringLiteral("Rev.1.15"));
        QVERIFY(fifteen != nullptr);
        QCOMPARE(fifteen->altNumber.value_or(QString()), QStringLiteral("14"));
    }

    /// The interlinear edition can carry notes too, and has to write them the
    /// way the plain one does — the transcription tab exports through it, and a
    /// remark made while transcribing is the same kind of thing as a remark
    /// made while comparing.
    void theInterlinearWriterAnchorsANoteToItsWord()
    {
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        for (const QString &word : {QStringLiteral("alpha"),
                                    QStringLiteral("beta"),
                                    QStringLiteral("gamma")}) {
            ConsensusColumn column;
            column.text = word;
            draft.columns.append(column);
        }
        const QMap<QString, CombinedDraft> drafts{{draft.reference.id, draft}};

        SourceNote note;
        note.text = QStringLiteral("Doubtful");
        note.number = QStringLiteral("1");
        // Which word, not how many characters in: the interlinear body writes
        // each word separately, so an offset into running text means nothing.
        note.tokenIndex = 1;

        CombinedApparatus apparatus;
        apparatus.notes.insert(draft.reference.id, {note});

        const QString xml = serializeInterlinearOsis(
            drafts, InterlinearGlosses(), WorkMetadata(), apparatus);

        // Immediately before the word it belongs to, and not before any other.
        QVERIFY(xml.contains(
            QStringLiteral("<note type=\"explanation\" placement=\"foot\" n=\"1\" "
                           "osisRef=\"Rev.1.1\" osisID=\"Rev.1.1!note.1\">Doubtful"
                           "</note><w>beta</w>")));
        QVERIFY(!xml.contains(QStringLiteral("</note><w>alpha</w>")));
    }

    /// The plain writer anchors by character offset, so a note whose offset was
    /// never worked out lands at the start of the verse. Two notes then arrive
    /// on top of one another, on a word neither belongs to — which is what
    /// happens if a caller sets only tokenIndex.
    void plainNotesLandOnTheirOwnWords()
    {
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        for (const QString &word : {QStringLiteral("alpha"),
                                    QStringLiteral("beta"),
                                    QStringLiteral("gamma")}) {
            ConsensusColumn column;
            column.text = word;
            draft.columns.append(column);
        }
        const QMap<QString, CombinedDraft> drafts{{draft.reference.id, draft}};

        CombinedApparatus apparatus;
        SourceNote second;
        second.text = QStringLiteral("On beta");
        second.number = QStringLiteral("1");
        second.charOffset = columnCharOffset(draft, 1);
        SourceNote third;
        third.text = QStringLiteral("On gamma");
        third.number = QStringLiteral("2");
        third.charOffset = columnCharOffset(draft, 2);
        apparatus.notes.insert(draft.reference.id, {second, third});

        QVERIFY2(second.charOffset > 0, "the second word does not start the verse");
        QVERIFY(third.charOffset > second.charOffset);

        const QString xml = serializeCombinedOsis(drafts, WorkMetadata(), apparatus);
        QVERIFY(xml.contains(QStringLiteral("alpha <note")));
        QVERIFY(xml.contains(QStringLiteral("On beta</note>beta")));
        QVERIFY(xml.contains(QStringLiteral("On gamma</note>gamma")));
    }

    void theHeaderKeepsWhatItIsGiven()
    {
        // The shelfmark is how a manuscript is identified at all, and it used
        // to be assembled by the caller and then dropped here.
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        ConsensusColumn column;
        column.text = QStringLiteral("alpha");
        draft.columns.append(column);

        WorkMetadata metadata;
        metadata.title = QStringLiteral("A transcription");
        metadata.scope = QStringLiteral("REV");
        metadata.identifiers.insert(
            QStringLiteral("x-shelfmark"), QStringLiteral("British Library, Sloane MS 237"));

        const QString xml =
            serializeCombinedOsis({{draft.reference.id, draft}}, metadata);
        QVERIFY(xml.contains(QStringLiteral(
            "<identifier type=\"x-shelfmark\">British Library, Sloane MS 237</identifier>")));
        QVERIFY(xml.contains(QStringLiteral("<scope>REV</scope>")));
    }

    void aPreambleIsNotWrittenAsAVerse()
    {
        // Matter standing before verse 1. OSIS has an element for it, and
        // numbering it 0 would address a verse no versification has.
        CombinedDraft preamble;
        preamble.reference.id = QStringLiteral("Rev.4.0");
        preamble.reference.book = QStringLiteral("Rev");
        preamble.reference.chapter = 4;
        preamble.reference.verse = QStringLiteral("0");
        ConsensusColumn incipit;
        incipit.text = QStringLiteral("incipit");
        preamble.columns.append(incipit);

        CombinedDraft first;
        first.reference.id = QStringLiteral("Rev.4.1");
        first.reference.book = QStringLiteral("Rev");
        first.reference.chapter = 4;
        first.reference.verse = QStringLiteral("1");
        ConsensusColumn opening;
        opening.text = QStringLiteral("alpha");
        first.columns.append(opening);

        const QString xml = serializeCombinedOsis(
            {{preamble.reference.id, preamble}, {first.reference.id, first}});

        QVERIFY(xml.contains(
            QStringLiteral("<div type=\"introduction\" osisRef=\"Rev.4\">incipit</div>")));
        QVERIFY(!xml.contains(QStringLiteral("osisID=\"Rev.4.0\"")));
        // And it stands before verse 1, which is the whole of what a preamble
        // is. The drafts sort it there; this is the check that they still do.
        QVERIFY(
            xml.indexOf(QStringLiteral("type=\"introduction\""))
            < xml.indexOf(QStringLiteral("osisID=\"Rev.4.1\"")));
        // Inside the chapter it introduces, not before it.
        QVERIFY(
            xml.indexOf(QStringLiteral("<chapter sID=\"Rev.4\""))
            < xml.indexOf(QStringLiteral("type=\"introduction\"")));
    }

    void aNoteOnAPreamblePointsAtTheChapter()
    {
        // Its notes are held under Rev.4.0 like any other verse's, but that
        // verse is not in the file — so a note pointing at it would point at
        // nothing.
        CombinedDraft preamble;
        preamble.reference.id = QStringLiteral("Rev.4.0");
        preamble.reference.book = QStringLiteral("Rev");
        preamble.reference.chapter = 4;
        preamble.reference.verse = QStringLiteral("0");
        ConsensusColumn incipit;
        incipit.text = QStringLiteral("incipit");
        preamble.columns.append(incipit);

        CombinedApparatus apparatus;
        SourceNote note;
        note.number = QStringLiteral("1");
        note.text = QStringLiteral("In a later hand.");
        note.charOffset = 0;
        apparatus.notes[preamble.reference.id].append(note);

        const QString xml = serializeCombinedOsis(
            {{preamble.reference.id, preamble}}, WorkMetadata(), apparatus);
        QVERIFY(xml.contains(QStringLiteral("osisRef=\"Rev.4\"")));
        QVERIFY(!xml.contains(QStringLiteral("osisRef=\"Rev.4.0\"")));
        QVERIFY(xml.contains(QStringLiteral("In a later hand.")));
    }

    void aChapterWithNoPreambleIsWrittenExactlyAsItWas()
    {
        // The guard on everything above: an ordinary edition must not have
        // moved by a character.
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        ConsensusColumn column;
        column.text = QStringLiteral("alpha");
        draft.columns.append(column);

        const QString xml = serializeCombinedOsis({{draft.reference.id, draft}});
        QVERIFY(!xml.contains(QStringLiteral("introduction")));
        QVERIFY(xml.contains(
            QStringLiteral("<verse sID=\"Rev.1.1\" osisID=\"Rev.1.1\" n=\"1\"/>alpha")));
    }

    void aWitnessIsNotLabelledAnEdition()
    {
        // What the manuscript library's own files say, and what tells a reader
        // opening the XML that they have a witness rather than a collation.
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        ConsensusColumn column;
        column.text = QStringLiteral("alpha");
        draft.columns.append(column);
        const QMap<QString, CombinedDraft> drafts{{draft.reference.id, draft}};

        WorkMetadata manuscript;
        manuscript.workType = QStringLiteral("x-manuscript");
        const QString witness = serializeCombinedOsis(drafts, manuscript);
        QVERIFY(witness.contains(
            QStringLiteral("<type type=\"x-manuscript\">Manuscript</type>")));
        QVERIFY(!witness.contains(QStringLiteral("x-bible")));

        // And an edition, which says nothing, still reads as it always did.
        const QString edition = serializeCombinedOsis(drafts);
        QVERIFY(edition.contains(QStringLiteral("<type type=\"x-bible\">Edition</type>")));
        QVERIFY(!edition.contains(QStringLiteral("x-manuscript")));
    }

    void textThatLooksLikeAPlaceholderIsLeftAlone()
    {
        // The header is assembled by QString::arg, and the verse text is one of
        // its arguments. A note reading "%1" must stay a note rather than being
        // filled in with the title on a later pass.
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        ConsensusColumn column;
        column.text = QStringLiteral("%1 %5 %7");
        draft.columns.append(column);

        WorkMetadata metadata;
        metadata.title = QStringLiteral("A transcription");
        metadata.scope = QStringLiteral("REV");

        const QString xml =
            serializeCombinedOsis({{draft.reference.id, draft}}, metadata);
        QVERIFY(xml.contains(QStringLiteral("%1 %5 %7")));
        QCOMPARE(xml.count(QStringLiteral("<scope>REV</scope>")), 1);
    }

    void aHeaderWithNothingExtraIsUnchanged()
    {
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        ConsensusColumn column;
        column.text = QStringLiteral("alpha");
        draft.columns.append(column);

        // What the comparison passes today: no identifiers, no scope. Its
        // output must not have moved.
        const QString xml = serializeCombinedOsis({{draft.reference.id, draft}});
        QVERIFY(!xml.contains(QStringLiteral("<scope>")));
        QVERIFY(!xml.contains(QStringLiteral("x-shelfmark")));
    }

    void anInterlinearWithNoNotesIsUnchanged()
    {
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        ConsensusColumn column;
        column.text = QStringLiteral("alpha");
        draft.columns.append(column);
        const QMap<QString, CombinedDraft> drafts{{draft.reference.id, draft}};

        const QString xml = serializeInterlinearOsis(drafts, InterlinearGlosses());
        QVERIFY(xml.contains(QStringLiteral("<w>alpha</w>")));
        QVERIFY(!xml.contains(QStringLiteral("<note")));
    }

    /// Every note in a verse used to export as n="" and osisID="…!note.", so
    /// two notes on one verse carried one identifier between them.
    void notesInAVerseAreNumberedApart()
    {
        CombinedDraft draft;
        draft.reference.id = QStringLiteral("Rev.1.1");
        draft.reference.book = QStringLiteral("Rev");
        draft.reference.chapter = 1;
        draft.reference.verse = QStringLiteral("1");
        for (const QString &word : {QStringLiteral("alpha"), QStringLiteral("beta")}) {
            ConsensusColumn column;
            column.text = word;
            draft.columns.append(column);
        }
        const QMap<QString, CombinedDraft> drafts{{draft.reference.id, draft}};

        CombinedApparatus apparatus;
        SourceNote first;
        first.text = QStringLiteral("On the first");
        first.number = QStringLiteral("1");
        first.tokenIndex = 0;
        SourceNote second;
        second.text = QStringLiteral("On the second");
        second.number = QStringLiteral("2");
        second.tokenIndex = 1;
        apparatus.notes.insert(draft.reference.id, {first, second});

        const QString xml = serializeInterlinearOsis(
            drafts, InterlinearGlosses(), WorkMetadata(), apparatus);

        QVERIFY(xml.contains(QStringLiteral("osisID=\"Rev.1.1!note.1\"")));
        QVERIFY(xml.contains(QStringLiteral("osisID=\"Rev.1.1!note.2\"")));
        QVERIFY(!xml.contains(QStringLiteral("osisID=\"Rev.1.1!note.\"")));
    }
};

QTEST_MAIN(ApparatusTest)
#include "apparatus_test.moc"
