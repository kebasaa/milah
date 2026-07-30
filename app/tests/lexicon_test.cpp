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
    /// Exercises the embedded resource by the same ":/data/..." path the
    /// application uses, so a mis-wired resource fails here rather than
    /// silently leaving every Strong's cell blank at runtime.
    void theBundledIndexLoadsFromTheResource()
    {
        QVERIFY2(
            !m_lexicon.isEmpty(),
            "the bundled index is missing from the Qt resource or failed to parse");
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
        QVERIFY(!entries.first().gloss.isEmpty());
        QVERIFY(!entries.first().lemma.isEmpty());
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
