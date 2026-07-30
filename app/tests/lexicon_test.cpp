#include "core/data_paths.h"
#include "core/lexicon.h"
#include "core/tokenize.h"

#include <QtTest>

using namespace milah;

namespace {

QString firstNumber(const HebrewLexicon &lexicon, const QString &word)
{
    const QList<LexiconEntry> entries = lexicon.lookup(word);
    return entries.isEmpty() ? QString() : entries.first().strongs;
}

} // namespace

class LexiconTest final : public QObject
{
    Q_OBJECT

private:
    const HebrewLexicon &m_lexicon = HebrewLexicon::shared();

private slots:
    /// Finds the index the same way an installed copy does — a file beside the
    /// executable — so a data file that fails to ship fails here rather than
    /// silently leaving every Strong's cell blank at runtime.
    void theIndexLoadsFromDisk()
    {
        QVERIFY2(
            !m_lexicon.isEmpty(),
            qPrintable(QStringLiteral("hebrew_lexicon.json was not found. Looked in: %1")
                           .arg(dataSearchPaths().join(QStringLiteral("; ")))));
    }

    void theSearchPathsPreferAReplacementOverTheShippedCopy()
    {
        const QStringList paths = dataSearchPaths();
        QVERIFY(!paths.isEmpty());
        // The copy beside the executable is what ships, so it must be looked
        // at last: anything the editor drops in has to win.
        QVERIFY(paths.constLast().contains(QCoreApplication::applicationDirPath()));
    }

    void anAbsentFileYieldsAnEmptyLexicon()
    {
        const HebrewLexicon missing =
            HebrewLexicon::fromFile(QStringLiteral("no-such-lexicon.json"));
        QVERIFY(missing.isEmpty());
        QVERIFY(!missing.knows(QString::fromUtf8("אֱלֹהִים")));
        QVERIFY(missing.lookup(QString::fromUtf8("אֱלֹהִים")).isEmpty());
    }

    void findsAPlainWord()
    {
        // אֱלֹהִים, God.
        QCOMPARE(
            firstNumber(m_lexicon, QString::fromUtf8("אֱלֹהִים")),
            QStringLiteral("H430"));
    }

    void carriesTheDictionaryEntryAlong()
    {
        const QList<LexiconEntry> entries =
            m_lexicon.lookup(QString::fromUtf8("אֱלֹהִים"));
        QVERIFY(!entries.isEmpty());
        const LexiconEntry &entry = entries.first();

        QVERIFY(!entry.lemma.isEmpty());
        QVERIFY(!entry.transliteration.isEmpty());
        // Strong's own text, no longer clipped to 90 characters.
        QVERIFY(!entry.gloss.isEmpty());
        QVERIFY(!entry.kjvUsage.isEmpty());
        // And the STEPBible side.
        QVERIFY2(!entry.briefGloss.isEmpty(), "TBESH gloss is missing");
        QVERIFY2(!entry.meaning.isEmpty(), "abridged BDB is missing");
        QVERIFY2(!entry.morphology.isEmpty(), "part of speech is missing");
    }

    void theTooltipCarriesBothLexicons()
    {
        const QList<LexiconEntry> entries =
            m_lexicon.lookup(QString::fromUtf8("אֱלֹהִים"));
        QVERIFY(!entries.isEmpty());
        const LexiconEntry &entry = entries.first();
        const QString tooltip = strongsTooltip(entries);

        QVERIFY(tooltip.startsWith(entry.strongs));
        QVERIFY(tooltip.contains(entry.lemma));
        QVERIFY(tooltip.contains(entry.morphology));
        QVERIFY(tooltip.contains(entry.briefGloss));
        QVERIFY(tooltip.contains(entry.meaning));
        QVERIFY(tooltip.contains(QStringLiteral("KJV:")));
        // A single reading is not announced as a choice.
        QVERIFY(!tooltip.contains(QStringLiteral("possible readings")));
    }

    void theTooltipListsEveryCandidate()
    {
        // בְּיַד is ambiguous on its consonants; the row shows the likeliest
        // number with a mark, so the tooltip has to account for the rest.
        const QList<LexiconEntry> entries =
            m_lexicon.lookup(QString::fromUtf8("בְּיַד"));
        QVERIFY2(entries.size() > 1, "expected an ambiguous form");

        const QString tooltip = strongsTooltip(entries);
        QVERIFY(tooltip.startsWith(
            QStringLiteral("%1 possible readings:").arg(entries.size())));
        for (const LexiconEntry &entry : entries) {
            QVERIFY2(
                tooltip.contains(entry.strongs),
                qPrintable(QStringLiteral("%1 is missing").arg(entry.strongs)));
        }
    }

    void anEmptyLookupHasNoTooltip()
    {
        QVERIFY(strongsTooltip({}).isEmpty());
    }

    void anUnpointedFormMatchesThePointedIndex()
    {
        // The manuscripts are not consistently pointed, so the consonantal
        // skeleton has to resolve on its own.
        QCOMPARE(
            firstNumber(m_lexicon, QString::fromUtf8("אלהים")),
            QStringLiteral("H430"));
    }

    void cantillationDoesNotPreventAMatch()
    {
        // The index is built from a text carrying accents; verse text is not.
        QCOMPARE(
            firstNumber(m_lexicon, QString::fromUtf8("אֱלֹהִ֑ים")),
            QStringLiteral("H430"));
    }

    void peelsAnAgglutinatedPrefix()
    {
        // שֶׁנָּתְנוּ is the relative shin on נתן; the index holds the bare verb.
        QCOMPARE(
            firstNumber(m_lexicon, QString::fromUtf8("שֶׁנָּתְנוּ")),
            QStringLiteral("H5414"));
        // הַמַּלְאָכוֹ likewise, once the article is taken off.
        QCOMPARE(
            firstNumber(m_lexicon, QString::fromUtf8("הַמַּלְאָכוֹ")),
            QStringLiteral("H4397"));
    }

    void doesNotInventAReadingForNonsense()
    {
        const QString nonsense = QString::fromUtf8("זזזזזז");
        QVERIFY(!m_lexicon.knows(nonsense));
        QVERIFY(m_lexicon.lookup(nonsense).isEmpty());
    }

    void peelingNeverManufacturesAMatch()
    {
        // Starts with a prefix letter, but nothing is left that the index
        // knows, so it must stay unresolved rather than matching a fragment.
        QVERIFY(!m_lexicon.knows(QString::fromUtf8("בזזזזז")));
    }

    void anEmptyWordIsUnknown()
    {
        QVERIFY(!m_lexicon.knows(QString()));
        QVERIFY(!m_lexicon.knows(QStringLiteral("   ")));
    }

    void theIndexAgreesWithTheAppsComparisonKey()
    {
        // The generator reimplements comparisonKey() in Python. If the two ever
        // drift, every lookup silently misses, so pin a couple of cases.
        QCOMPARE(
            comparisonKey(QString::fromUtf8("אֱלֹהִ֑ים")),
            QString::fromUtf8("אלהים"));
        QCOMPARE(
            comparisonKey(QString::fromUtf8("בְּרֵאשִׁית")),
            QString::fromUtf8("בראשית"));
    }

    void resolvesMostOfARealVerse()
    {
        // Revelation 1.1 as the Combined row currently reads it. Coverage is
        // not total — a Hebrew New Testament is not the Tanakh — so this pins
        // the level rather than demanding every word.
        const QStringList words = QString::fromUtf8(
                                      "חֲזוֹן יְהוֹשֻׁעַ מָשִׁיחַ שֶׁנָּתְנוּ הָאֱלֹהִים "
                                      "לְהַרְאוֹת עֲבָדָיו אֲשֶׁר לִהְיוֹת שׁוֹלֵחַ "
                                      "בְּיַד הַמַּלְאָכוֹ לְעַבְדּוֹ יוֹחָנָן")
                                      .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int resolved = 0;
        for (const QString &word : words) {
            if (m_lexicon.knows(word)) {
                ++resolved;
            }
        }
        QVERIFY2(
            resolved >= words.size() - 1,
            qPrintable(QStringLiteral("only %1 of %2 words resolved")
                           .arg(resolved)
                           .arg(words.size())));
    }
};

QTEST_MAIN(LexiconTest)
#include "lexicon_test.moc"
