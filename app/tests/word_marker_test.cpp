#include "core/lexicon.h"
#include "core/suggestions.h"
#include "core/word_marker.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace milah;

/// The marker row is the only place a transcriber is told whether the word they
/// have just read off a folio is a word anyone else has met. Each rung of the
/// ladder says something different, so each is pinned separately: a wrong rung
/// is worse than a blank cell, because it reads as an answer.
class WordMarkerTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_home;
    /// Its own file in a temporary directory, never the editor's real one: a
    /// test must not be able to write into the dictionary someone is using.
    UserDictionary dictionaryAt(const QString &name)
    {
        return UserDictionary(m_home.filePath(name));
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(m_home.isValid(), "could not make a temporary directory");
        QVERIFY2(
            !HebrewLexicon::shared().isEmpty(),
            "hebrew_lexicon.json was not found beside the test executable");
    }

    void aBiblicalWordCarriesItsNumber()
    {
        // אֱלֹהִים, God.
        const WordMarker marker =
            markerFor(QString::fromUtf8("אֱלֹהִים"), UserDictionary());
        QCOMPARE(marker.text, QStringLiteral("H430"));
        QVERIFY(marker.tooltip.contains(QStringLiteral("H430")));
    }

    void anUnpointedFormResolvesToo()
    {
        // Nothing is stripped before the lookup: the lexicon normalises for
        // itself, which is what lets a transcriber type what they see.
        QCOMPARE(
            markerFor(QString::fromUtf8("אלהים"), UserDictionary()).text,
            QStringLiteral("H430"));
    }

    void anAgglutinatedPrefixIsPeeled()
    {
        // שֶׁנָּתְנוּ is the relative shin on נתן.
        QCOMPARE(
            markerFor(QString::fromUtf8("שֶׁנָּתְנוּ"), UserDictionary()).text,
            QStringLiteral("H5414"));
    }

    void anAmbiguousFormIsMarkedAsAChoice()
    {
        // בְּיַד is ambiguous on its consonants. The likeliest reading is shown
        // with a question mark rather than being passed off as the answer.
        const WordMarker marker =
            markerFor(QString::fromUtf8("בְּיַד"), UserDictionary());
        QVERIFY2(marker.text.endsWith(QLatin1Char('?')), qPrintable(marker.text));
        QVERIFY(marker.tooltip.contains(QStringLiteral("possible readings")));
    }

    void aRabbinicWordIsMarkedM()
    {
        // Strong's covers the Hebrew Bible only, so a word the Mishnah or the
        // Tosefta has and the Bible does not gets M, never a number.
        const AttestedForms &attested = AttestedForms::shared();
        QVERIFY2(!attested.keys().isEmpty(), "rabbinic.words.txt was not found");

        QString rabbinicOnly;
        for (const QString &key : attested.keys()) {
            if (!key.isEmpty() && HebrewLexicon::shared().lookup(key).isEmpty()) {
                rabbinicOnly = key;
                break;
            }
        }
        QVERIFY2(!rabbinicOnly.isEmpty(), "no word is attested rabbinically alone");

        const WordMarker marker = markerFor(rabbinicOnly, UserDictionary());
        QCOMPARE(marker.text, QStringLiteral("M"));
        QVERIFY(marker.tooltip.contains(QStringLiteral("Mishnah")));
    }

    void anUnattestedWordIsADash()
    {
        const WordMarker marker =
            markerFor(QString::fromUtf8("זזזזזז"), UserDictionary());
        QCOMPARE(marker.text, QString::fromUtf8("—"));
        QVERIFY(marker.tooltip.contains(QStringLiteral("not attested")));
    }

    void anEmptyWordSaysNothingAtAll()
    {
        // A column with no reading in it is not a word nobody knows, so it must
        // not be marked as one.
        const WordMarker marker = markerFor(QString(), UserDictionary());
        QVERIFY(marker.text.isEmpty());
        QVERIFY(marker.tooltip.isEmpty());
    }

    void anOwnDefinitionAloneStandsAsD()
    {
        // A dash says nothing knows this word, which stops being true the
        // moment the editor defines it — so D replaces the dash rather than
        // riding on it.
        UserDictionary dictionary = dictionaryAt(QStringLiteral("alone.json"));
        const QString nonsense = QString::fromUtf8("זזזזזז");
        QVERIFY(dictionary.save(nonsense, {QStringLiteral("A scribal flourish.")}));

        const WordMarker marker = markerFor(nonsense, dictionary);
        QCOMPARE(marker.text, QStringLiteral("D"));
        QVERIFY(marker.tooltip.contains(QStringLiteral("A scribal flourish.")));
    }

    void anOwnDefinitionRidesBesideTheHarderFact()
    {
        UserDictionary dictionary = dictionaryAt(QStringLiteral("beside.json"));
        const QString word = QString::fromUtf8("אֱלֹהִים");
        QVERIFY(dictionary.save(word, {QStringLiteral("Rendered plural here.")}));

        const WordMarker marker = markerFor(word, dictionary);
        QCOMPARE(marker.text, QString::fromUtf8("H430·D"));
        QVERIFY(marker.tooltip.contains(QStringLiteral("Your own definition:")));
        QVERIFY(marker.tooltip.contains(QStringLiteral("Rendered plural here.")));
        // The lexicon's own reading is still there underneath it.
        QVERIFY(marker.tooltip.contains(QStringLiteral("H430")));
    }

    void aDefinitionIsFoundHoweverTheWordIsPointed()
    {
        // The dictionary is keyed by comparison key, so a word defined pointed
        // is still found when it is typed bare.
        UserDictionary dictionary = dictionaryAt(QStringLiteral("pointing.json"));
        QVERIFY(dictionary.save(
            QString::fromUtf8("אֱלֹהִים"), {QStringLiteral("Noted once.")}));

        QCOMPARE(
            markerFor(QString::fromUtf8("אלהים"), dictionary).text,
            QString::fromUtf8("H430·D"));
    }

    void theRowExplainsItself()
    {
        const QString tooltip = markerRowTooltip();
        QVERIFY(tooltip.contains(QStringLiteral("Mishnah")));
        QVERIFY(tooltip.contains(QStringLiteral("D")));
    }
};

QTEST_MAIN(WordMarkerTest)
#include "word_marker_test.moc"
