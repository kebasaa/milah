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

AbbreviationTable abbreviationsFrom(const QByteArray &json)
{
    return AbbreviationTable::fromJson(QJsonDocument::fromJson(json).object());
}

} // namespace

class SuggestionsTest final : public QObject
{
    Q_OBJECT

private:
    const HebrewLexicon &m_lexicon = HebrewLexicon::shared();

private slots:
    // --- scribal abbreviations ---------------------------------------------

    void offersWhatAnAbbreviationStandsFor()
    {
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ה","expansions":["אלהים","יהוה","אדני"]}]})");

        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("ה֞")}),
                m_lexicon,
                PhraseRules(),
                QSet<QString>(),
                table),
            SuggestionKind::Abbreviation);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().column, 0);
        // Accepting writes the likeliest reading; the rest are named so the
        // choice among the divine names stays the editor's.
        QCOMPARE(found.first().replacement, QString::fromUtf8("אלהים"));
        QVERIFY(found.first().reason.contains(QString::fromUtf8("יהוה")));
        QVERIFY(found.first().reason.contains(QString::fromUtf8("אדני")));
    }

    void anAbbreviationDoesNotAlsoGetThePhraseRulesReason()
    {
        // A phrase rule is matched on the skeleton, which cannot see the
        // abbreviation mark, so the ישו rule reads יש֞ו as the bare form and
        // offers "a derogatory shortening" -- true of ישו, false of an
        // abbreviation. One flag, and the right one.
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ישו","expansions":["ישוע"]}]})");
        const PhraseRules rules = rulesFrom(
            R"({"rules":[{"match":["ישו"],"replace":["יֵשׁוּעַ"],
                "reason":"The truncated ישו is a derogatory shortening."}]})");

        const QList<Suggestion> found = reviewVerse(
            verse({QString::fromUtf8("יש֞ו")}),
            m_lexicon,
            rules,
            QSet<QString>(),
            table);

        QCOMPARE(of(found, SuggestionKind::Abbreviation).size(), 1);
        QVERIFY(of(found, SuggestionKind::PhraseRule).isEmpty());
    }

    void thePhraseRuleStillFiresOnTheBareForm()
    {
        // Without the mark it really is the bare ישו, and the rule is right.
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ישו","expansions":["ישוע"]}]})");
        const PhraseRules rules = rulesFrom(
            R"({"rules":[{"match":["ישו"],"replace":["יֵשׁוּעַ"],
                "reason":"The truncated ישו is a derogatory shortening."}]})");

        const QList<Suggestion> found = reviewVerse(
            verse({QString::fromUtf8("ישו")}),
            m_lexicon,
            rules,
            QSet<QString>(),
            table);

        QCOMPARE(of(found, SuggestionKind::PhraseRule).size(), 1);
        QVERIFY(of(found, SuggestionKind::Abbreviation).isEmpty());
    }

    void anUnmarkedLoneHeIsFlaggedForTheEditor()
    {
        // Cochin's James writes the divine name as a bare ה ten times, with no
        // mark at all -- עבד ה, לפני ה האב. Sloane writes a bare ה too, but
        // there it is a detached definite article. Nothing mechanical tells
        // them apart, so both are raised and the wording says so.
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ה","expansions":["אלהים","יהוה"]}]})");

        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("לפני"), QString::fromUtf8("ה")}),
                m_lexicon,
                PhraseRules(),
                QSet<QString>(),
                table),
            SuggestionKind::Abbreviation);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().column, 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("אלהים"));
        QVERIFY(found.first().reason.contains(QStringLiteral("detached")));
    }

    void aMarkedAbbreviationIsNotHedged()
    {
        // With the mark there is nothing to weigh up, so the reason must not
        // borrow the unmarked case's hedging.
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ה","expansions":["אלהים"]}]})");

        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("לפני"), QString::fromUtf8("ה֞")}),
                m_lexicon,
                PhraseRules(),
                QSet<QString>(),
                table),
            SuggestionKind::Abbreviation);

        QCOMPARE(found.size(), 1);
        QVERIFY(!found.first().reason.contains(QStringLiteral("detached")));
    }

    void anUnmarkedLongerWordIsNotFlagged()
    {
        // Only a lone letter. A longer unmarked word is simply that word --
        // a bare ישו is the form the phrase rules speak to, not an
        // abbreviation -- and עי unmarked is not a word at all.
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ה","expansions":["אלהים"]},
                           {"stem":"ישו","expansions":["ישוע"]},
                           {"stem":"עי","expansions":["על ידי"]}]})");

        for (const char *word : {"ישו", "עי"}) {
            QVERIFY(of(reviewVerse(
                           verse({QString::fromUtf8(word)}),
                           m_lexicon,
                           PhraseRules(),
                           QSet<QString>(),
                           table),
                       SuggestionKind::Abbreviation)
                        .isEmpty());
        }
    }

    // --- writing a replacement the way the edition is written ---------------

    void anUnpointedEditionGetsAnUnpointedExpansion()
    {
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ה","expansions":["אֱלֹהִים","יְהוָה"]}]})");

        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("ולכהנים"),
                       QString::fromUtf8("לפני"),
                       QString::fromUtf8("ה֞")}),
                m_lexicon,
                PhraseRules(),
                QSet<QString>(),
                table),
            SuggestionKind::Abbreviation);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("אלהים"));
        // The alternatives named in the reason are spelled the same way, or
        // the editor is offered a choice the edition cannot take.
        QVERIFY(found.first().reason.contains(QString::fromUtf8("יהוה")));
        QVERIFY(!found.first().reason.contains(QString::fromUtf8("יְהוָה")));
    }

    void aPointedEditionGetsThePointedExpansion()
    {
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ה","expansions":["אֱלֹהִים"]}]})");

        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("וְכֹהֲנִים"),
                       QString::fromUtf8("לִפְנֵי"),
                       QString::fromUtf8("ה֞")}),
                m_lexicon,
                PhraseRules(),
                QSet<QString>(),
                table),
            SuggestionKind::Abbreviation);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("אֱלֹהִים"));
    }

    void oneStrayPointedWordDoesNotFlipTheConvention()
    {
        // A single pointed word is a witness reading that won its column, not
        // a change of convention. Majority, not "any".
        QVERIFY(!readingsArePointed(
            {QString::fromUtf8("ולכהנים"),
             QString::fromUtf8("לפני"),
             QString::fromUtf8("הָאָרֶץ")}));

        QVERIFY(readingsArePointed(
            {QString::fromUtf8("וְכֹהֲנִים"),
             QString::fromUtf8("לִפְנֵי"),
             QString::fromUtf8("הארץ")}));
    }

    void aVerseWithNothingToGoOnKeepsTheTableForm()
    {
        // No word long enough to show a convention. Points can be stripped
        // afterwards but not invented, so the table's own spelling stands.
        QVERIFY(readingsArePointed({}));
        QVERIFY(readingsArePointed({QString::fromUtf8("ה֞")}));
        QVERIFY(readingsArePointed({QString::fromUtf8("׃")}));
    }

    void anAbbreviationMarkIsNotMistakenForPointing()
    {
        // The accents Cochin abbreviates with sit next to the vowel points in
        // Unicode. Counting them would call this unpointed verse pointed and
        // write אֱלֹהִים into a text that has no points anywhere.
        QVERIFY(!readingsArePointed(
            {QString::fromUtf8("מלאך"), QString::fromUtf8("יש֞ו")}));
    }

    void anAbbreviationWithNoEntryIsLeftAlone()
    {
        // Numerals carry the same mark but expand to numbers, which is a
        // different problem; they must pass through rather than guess.
        const AbbreviationTable table = abbreviationsFrom(
            R"({"entries":[{"stem":"ה","expansions":["אלהים"]}]})");

        QVERIFY(of(reviewVerse(
                       verse({QString::fromUtf8("א׳")}),
                       m_lexicon,
                       PhraseRules(),
                       QSet<QString>(),
                       table),
                   SuggestionKind::Abbreviation)
                    .isEmpty());
    }

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

        // The input carries an abbreviation mark (U+059E on the shin), proving
        // the single-word match ignores pointing.
        //
        // The verse is unpointed, so the rule's pointed יֵשׁוּעַ is offered
        // unpointed: accepting it must not point one word of a text that has
        // no points anywhere.
        const QList<Suggestion> found = of(
            reviewVerse(verse({QString::fromUtf8("יש֞ו")}), m_lexicon, rules),
            SuggestionKind::PhraseRule);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().column, 0);
        QCOMPARE(found.first().replacement, QString::fromUtf8("ישוע"));
    }

    void aPointedEditionKeepsThePhraseRulesPointing()
    {
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["ישו"],
            "replace": ["יֵשׁוּעַ"],
            "reason":  "written out as Yeshua"
        }]})");

        const QList<Suggestion> found = of(
            reviewVerse(
                verse({QString::fromUtf8("וְהָעֵדַת"),
                       QString::fromUtf8("מָשִׁיחַ"),
                       QString::fromUtf8("יש֞ו")}),
                m_lexicon,
                rules),
            SuggestionKind::PhraseRule);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().replacement, QString::fromUtf8("יֵשׁוּעַ"));
    }

    void aRuleIsNotOfferedWhenTheEditionAlreadyReadsIt()
    {
        // The spelling has to be settled before the "is this a change?" test,
        // or an unpointed edition already reading ישוע keeps being offered
        // ישוע as though it were one.
        const PhraseRules rules = rulesFrom(R"({"rules":[{
            "match":   ["ישוע"],
            "replace": ["יֵשׁוּעַ"],
            "reason":  "written out as Yeshua"
        }]})");

        QVERIFY(of(reviewVerse(
                       verse({QString::fromUtf8("ישוע"),
                              QString::fromUtf8("המשיח")}),
                       m_lexicon,
                       rules),
                   SuggestionKind::PhraseRule)
                    .isEmpty());
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
        // Unpointed verse, so the lamed keeps its place but not its pointing.
        QCOMPARE(found.first().replacement, QString::fromUtf8("לישוע"));
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
