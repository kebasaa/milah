#include "core/lexicon.h"
#include "core/suggestions.h"
#include "core/tokenize.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace milah;

namespace {

QList<std::optional<QString>> verse(const QStringList &words)
{
    QList<std::optional<QString>> result;
    for (const QString &word : words) {
        result.append(word.isEmpty() ? std::nullopt : std::optional<QString>(word));
    }
    return result;
}

QList<Suggestion> of(const QList<Suggestion> &found, SuggestionKind kind)
{
    QList<Suggestion> result;
    for (const Suggestion &suggestion : found) {
        if (suggestion.kind == kind) {
            result.append(suggestion);
        }
    }
    return result;
}

PhraseRules rulesFrom(const QByteArray &json)
{
    return PhraseRules::fromJson(QJsonDocument::fromJson(json).object());
}

} // namespace

class SuggestionsTest final : public QObject
{
    Q_OBJECT

private:
    const HebrewLexicon &m_lexicon = HebrewLexicon::shared();

private slots:
    // --- orthography ------------------------------------------------------

    void flagsANonFinalLetterAtTheEndOfAWord()
    {
        // מלך written with a medial kaf.
        const QList<Suggestion> found = of(
            reviewVerse(verse({QString::fromUtf8("מלכ")}), m_lexicon, PhraseRules()),
            SuggestionKind::Orthography);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().column, 0);
        QCOMPARE(found.first().replacement, QString::fromUtf8("מלך"));
    }

    void flagsAFinalLetterInTheMiddleOfAWord()
    {
        const QList<Suggestion> found = of(
            reviewVerse(verse({QString::fromUtf8("םלך")}), m_lexicon, PhraseRules()),
            SuggestionKind::Orthography);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("מלך"));
    }

    void leavesACorrectlyWrittenWordAlone()
    {
        QVERIFY(of(reviewVerse(
                       verse({QString::fromUtf8("מלך"), QString::fromUtf8("ארץ")}),
                       m_lexicon,
                       PhraseRules()),
                   SuggestionKind::Orthography)
                    .isEmpty());
    }

    void flagsAWordOpeningWithAVowelPoint()
    {
        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("ַמלך")}), m_lexicon, PhraseRules()),
            SuggestionKind::Orthography);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("מלך"));
    }

    void flagsADoubledVowelPoint()
    {
        // Patach written twice on the same letter.
        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("מַַלך")}),
                m_lexicon,
                PhraseRules()),
            SuggestionKind::Orthography);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("מַלך"));
    }

    // --- unknown forms ----------------------------------------------------

    void flagsAWordTheLexiconDoesNotKnow()
    {
        const QList<Suggestion> found = of(
            reviewVerse(verse({QString::fromUtf8("זזזזזז")}), m_lexicon, PhraseRules()),
            SuggestionKind::UnknownForm);

        QCOMPARE(found.size(), 1);
        // Nothing is proposed: the checker has no idea what was meant.
        QVERIFY(found.first().replacement.isEmpty());
    }

    void doesNotFlagAWordTheLexiconKnows()
    {
        QVERIFY(of(reviewVerse(
                       verse({QString::fromUtf8("אֱלֹהִים")}), m_lexicon, PhraseRules()),
                   SuggestionKind::UnknownForm)
                    .isEmpty());
    }

    void withoutALexiconNothingIsCalledUnknown()
    {
        // The lexicon is a file on disk now, so it can be absent. Judging every
        // word "not attested" would mark the whole verse and say nothing, so
        // the check stands down — while the checks that need no lexicon stay.
        const HebrewLexicon none;
        QVERIFY(none.isEmpty());

        const QList<Suggestion> found = reviewVerse(
            verse({QString::fromUtf8("זזזזזז"), QString::fromUtf8("מלכ")}),
            none,
            PhraseRules());

        QVERIFY(of(found, SuggestionKind::UnknownForm).isEmpty());
        QCOMPARE(of(found, SuggestionKind::Orthography).size(), 1);
    }

    // --- attested forms ---------------------------------------------------

    void theShippedCorpusListLoads()
    {
        QVERIFY2(
            !AttestedForms::shared().isEmpty(),
            "rabbinic.words.txt was not found beside the executable");
    }

    void aPostBiblicalWordThatTheLexiconRejectsIsNotFlagged()
    {
        // The whole point. סנהדרין is a Greek loanword the Hebrew Bible does
        // not have, so the lexicon rightly knows nothing of it, but it is
        // ordinary Mishnaic Hebrew and must not be called a misspelling.
        const QString word = QString::fromUtf8("סַנְהֶדְרִין");
        QVERIFY2(!m_lexicon.knows(word), "expected the lexicon not to know it");
        QVERIFY2(AttestedForms::shared().contains(word), "expected the corpus to attest it");

        QVERIFY(of(reviewVerse(
                       verse({word}), m_lexicon, PhraseRules(),
                       AttestedForms::shared().keys()),
                   SuggestionKind::UnknownForm)
                    .isEmpty());
    }

    void nonsenseIsStillFlaggedWithTheCorpusLoaded()
    {
        // A list this large could smother the check; it must not.
        const QString word = QString::fromUtf8("זזזזזז");
        QVERIFY(!AttestedForms::shared().contains(word));

        QCOMPARE(
            of(reviewVerse(
                   verse({word}), m_lexicon, PhraseRules(),
                   AttestedForms::shared().keys()),
               SuggestionKind::UnknownForm)
                .size(),
            1);
    }

    void aWordListIgnoresItsHeaderAndBlankLines()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("sample.words.txt"));

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&file);
        stream << "# a header line\n\n"
               << QString::fromUtf8("שלום") << "\n"
               << "   \n"
               << "#another comment\n"
               << QString::fromUtf8("תלמיד") << "\n";
        file.close();

        const AttestedForms forms = AttestedForms::fromFile(path);
        QCOMPARE(forms.keys().size(), 2);
        QVERIFY(forms.contains(QString::fromUtf8("שָׁלוֹם")));
        QVERIFY(!forms.contains(QStringLiteral("#")));
    }

    void anAcceptedWordIsNotFlagged()
    {
        const QString word = QString::fromUtf8("זזזזזז");
        QSet<QString> accepted;
        accepted.insert(comparisonKey(word));

        QVERIFY(of(reviewVerse(verse({word}), m_lexicon, PhraseRules(), accepted),
                   SuggestionKind::UnknownForm)
                    .isEmpty());
    }

    // --- phrase rules -----------------------------------------------------

    void appliesASeededPhraseRuleInSequence()
    {
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["יהושע", "משיח"],
            "replace": ["", "הַמָּשִׁיחַ"],
            "reason":  "takes the article"
        }]})");
        QCOMPARE(rules.rules().size(), 1);

        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("יְהוֹשֻׁעַ"), QString::fromUtf8("מָשִׁיחַ")}),
                m_lexicon,
                rules),
            SuggestionKind::PhraseRule);

        QCOMPARE(found.size(), 1);
        // Only the word that actually changes is offered.
        QCOMPARE(found.first().column, 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("הַמָּשִׁיחַ"));
    }

    void expandsTheTruncatedYeshu()
    {
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["ישו"],
            "replace": ["יֵשׁוּעַ"],
            "reason":  "written out as Yeshua"
        }]})");

        // The input carries a cantillation mark (U+059E on the shin), proving
        // the single-word match ignores pointing.
        const QList<Suggestion> found = of(
            reviewVerse(verse({QString::fromUtf8("יש֞ו")}), m_lexicon, rules),
            SuggestionKind::PhraseRule);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().column, 0);
        QCOMPARE(found.first().replacement, QString::fromUtf8("יֵשׁוּעַ"));
    }

    void expandsAPrefixedYeshu()
    {
        // "to Yeshu": the inseparable lamed keeps its place and its pointing,
        // and the name is written out. The input carries a cantillation mark,
        // so this also proves the prefixed match ignores pointing.
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["לישו"],
            "replace": ["לְיֵשׁוּעַ"],
            "reason":  "written out as Yeshua"
        }]})");

        const QList<Suggestion> found = of(
            reviewVerse(verse({QString::fromUtf8("ליש֞ו")}), m_lexicon, rules),
            SuggestionKind::PhraseRule);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().column, 0);
        QCOMPARE(found.first().replacement, QString::fromUtf8("לְיֵשׁוּעַ"));
    }

    void matchesAcrossAColumnNoWitnessFilled()
    {
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["יהושע", "משיח"],
            "replace": ["", "הַמָּשִׁיחַ"],
            "reason":  "takes the article"
        }]})");

        // Rev 1.1 reads this way: the alignment leaves an empty Combined
        // column between the two words, which is not a word in the verse.
        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("יְהוֹשֻׁעַ"),
                       QString(),
                       QString::fromUtf8("מָשִׁיחַ")}),
                m_lexicon,
                rules),
            SuggestionKind::PhraseRule);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().column, 2);
        QCOMPARE(found.first().replacement, QString::fromUtf8("הַמָּשִׁיחַ"));
    }

    void doesNotApplyAPhraseRuleOutOfSequence()
    {
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["יהושע", "משיח"],
            "replace": ["", "הַמָּשִׁיחַ"],
            "reason":  "takes the article"
        }]})");

        // The same two words, the other way round.
        QVERIFY(of(reviewVerse(
                       verse({QString::fromUtf8("מָשִׁיחַ"), QString::fromUtf8("יְהוֹשֻׁעַ")}),
                       m_lexicon,
                       rules),
                   SuggestionKind::PhraseRule)
                    .isEmpty());
    }

    void doesNotRepeatARuleAlreadyFollowed()
    {
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["יהושע", "משיח"],
            "replace": ["", "הַמָּשִׁיחַ"],
            "reason":  "takes the article"
        }]})");

        // הַמָּשִׁיחַ still matches on consonants, but it is already what the rule
        // asks for, so proposing it again would be noise.
        QVERIFY(of(reviewVerse(
                       verse({QString::fromUtf8("יְהוֹשֻׁעַ"),
                              QString::fromUtf8("הַמָּשִׁיחַ")}),
                       m_lexicon,
                       rules),
                   SuggestionKind::PhraseRule)
                    .isEmpty());
    }

    void ignoresARuleWhoseHalvesDoNotLineUp()
    {
        // Two words to match but only one replacement: applying it would put
        // the wrong word in the wrong column, so the rule is dropped.
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["א", "ב"],
            "replace": ["ג"],
            "reason":  "malformed"
        }]})");

        QVERIFY(rules.isEmpty());
    }

    void theBundledRulesLoadFromTheResource()
    {
        QVERIFY2(
            !PhraseRules::shared().isEmpty(),
            "hebrew_phrase_rules.json is missing from the Qt resource");
    }

    // --- user dictionary --------------------------------------------------

    void theDictionaryRemembersAWordAcrossReopening()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("words.txt"));

        UserDictionary dictionary(path);
        QVERIFY(dictionary.add(QString::fromUtf8("יֵשׁוּעַ")));
        QVERIFY(dictionary.contains(QString::fromUtf8("יֵשׁוּעַ")));

        // Held by comparison key, so the pointing does not have to match.
        QVERIFY(dictionary.contains(QString::fromUtf8("ישוע")));

        const UserDictionary reopened(path);
        QVERIFY(reopened.contains(QString::fromUtf8("יֵשׁוּעַ")));
        QVERIFY(!reopened.contains(QString::fromUtf8("זזזזזז")));
    }
};

QTEST_MAIN(SuggestionsTest)
#include "suggestions_test.moc"
