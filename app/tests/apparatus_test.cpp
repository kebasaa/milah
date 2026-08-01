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
    return QStringLiteral("%1/tools/data/01_osis/REV_Sloane237_%2.osis")
        .arg(QStringLiteral(MILAH_REPO_ROOT), variant);
}

const QStringList &corpusVariants()
{
    static const QStringList variants = {
        QStringLiteral("hebrew"),
        QStringLiteral("hebrew_commented"),
        QStringLiteral("translation"),
        QStringLiteral("hebrew_consonantal"),
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
};

QTEST_MAIN(ApparatusTest)
#include "apparatus_test.moc"
