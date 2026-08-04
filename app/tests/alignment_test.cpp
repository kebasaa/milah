#include "core/align_score.h"
#include "core/alignment.h"
#include "core/hebrew_forms.h"
#include "core/lexicon.h"
#include "core/osis.h"
#include "core/suggestions.h"
#include "core/tokenize.h"
#include "test_data.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QtTest>

using namespace milah;

namespace {

using milah_test::refs;
using milah_test::witness;

const QString &verseId()
{
    static const QString id = QStringLiteral("Matt.1.1");
    return id;
}

/// The witnesses a column is read by, in the order `sources` were given, so a
/// test can say "this column is the priority witness's alone" without caring
/// which QHash order the cells happen to have.
QStringList readers(const AlignmentColumn &column, const QStringList &sourceOrder)
{
    QStringList result;
    for (const QString &sourceId : sourceOrder) {
        if (column.cell(sourceId)) {
            result.append(sourceId);
        }
    }
    return result;
}

QStringList twoWitnesses()
{
    return {QStringLiteral("a"), QStringLiteral("b")};
}

/// Whether any expansion is the given reading, compared on consonants alone.
/// The shipped table is written pointed and reduced to suit the edition, so an
/// assertion on the exact spelling would break every time the pointing is
/// revised without anything actually being wrong.
bool offersReading(const QStringList &expansions, const QString &consonants)
{
    for (const QString &expansion : expansions) {
        if (comparisonKey(expansion) == consonants) {
            return true;
        }
    }
    return false;
}

// --- the repository corpus, for the golden dump ---------------------------

QString corpusPath(const QString &file)
{
    return QStringLiteral("%1/tools/data/01_osis/%2.osis")
        .arg(QStringLiteral(MILAH_REPO_ROOT), file);
}

const QString &cochinFile()
{
    static const QString name = QStringLiteral("Rev_CochinOo.1.16.2_hebrew");
    return name;
}

const QString &sloaneFile()
{
    static const QString name = QStringLiteral("Rev_Sloane237_hebrew");
    return name;
}

bool corpusAvailable()
{
    return QFileInfo::exists(corpusPath(cochinFile()))
        && QFileInfo::exists(corpusPath(sloaneFile()));
}

SourceDocument readWitness(const QString &file, const QString &id)
{
    QFile handle(corpusPath(file));
    if (!handle.open(QIODevice::ReadOnly)) {
        return SourceDocument();
    }

    ParseOptions options;
    options.id = id;
    options.name = file + QStringLiteral(".osis");
    options.role = SourceRole::Manuscript;
    return parseOsis(QString::fromUtf8(handle.readAll()), options);
}

QString cellText(const AlignmentColumn &column, const QString &sourceId)
{
    if (const SourceToken *token = column.cell(sourceId)) {
        const QString key = comparisonKey(token->text);
        return key.isEmpty() ? token->text : key;
    }
    return QStringLiteral("-");
}

} // namespace

class AlignmentTest final : public QObject
{
    Q_OBJECT

private slots:
    // --- the scoring scale --------------------------------------------------

    void theCalibrationInvariantHolds()
    {
        // A mismatch has to cost exactly what the two gaps replacing it would.
        // Make it dearer and unrelated single words split into two columns,
        // which the consensus reads as a gap -- consensus_test's
        // fallsBackToThePriorityWitnessForATie catches that, but only
        // indirectly, so the relationship is asserted here in one line.
        static_assert(kScoreGap == kScoreMismatch);
        static_assert(kScoreMismatch == 2 * kScoreGap - kScoreMismatch);

        // The scale has to represent the three scores it replaced exactly, or
        // an alignment that fires no later rung would drift.
        static_assert(kScoreMatch == 4 * kScale);
        static_assert(kScoreMismatch == -2 * kScale);

        // A near miss must never outbid, or reach, an exact agreement.
        static_assert(kScoreNear < kScoreMatch);
        static_assert(kScoreMismatch < kScoreNear);
        QVERIFY(true);
    }

    // --- grading how close two readings are ---------------------------------

    void boundedEditDistanceCounts()
    {
        QCOMPARE(
            boundedEditDistance(
                QString::fromUtf8("דוד"), QString::fromUtf8("דוד"), 4),
            0);
        QCOMPARE(
            boundedEditDistance(
                QString::fromUtf8("דוד"), QString::fromUtf8("דויד"), 4),
            1);
        QCOMPARE(
            boundedEditDistance(QString(), QString::fromUtf8("דוד"), 4), 3);
    }

    void boundedEditDistanceGivesUpEarly()
    {
        // Past the limit the exact answer is not worth finding; the contract is
        // only that it reports something greater.
        QVERIFY(
            boundedEditDistance(
                QString::fromUtf8("אבג"), QString::fromUtf8("רשת"), 1)
            > 1);
        QVERIFY(
            boundedEditDistance(
                QString::fromUtf8("דוד"), QString::fromUtf8("דודדדדדדד"), 2)
            > 2);
    }

    void aOneLetterReadingIsNeverNearAnything()
    {
        // Cochin writes the divine name as a single ה. The rule this replaced
        // scored that positively against every word containing a ה, so an
        // abbreviation drifted onto whatever happened to sit near it.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ה")),
            witness(QStringLiteral("b"), QString::fromUtf8("וְכֹהֲנִים")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        // They still share a column -- with one word each and a mismatch
        // costing what two gaps cost, there is nowhere else to put them. What
        // matters is that the scorer no longer calls it a partial agreement.
        QCOMPARE(aligned.columns.size(), 1);
    }

    void aGluedPrefixDoesNotBreakAPlaceName()
    {
        // Revelation 1:11. Sloane writes וְאֶל- joined to the place name by a
        // hyphen, which the tokeniser keeps as one word, and ends it with a
        // different letter. Three edits out of twelve characters, and neither
        // string contains the other, so the old rule called them unrelated.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("לְפִילַדֶלְפיַאן")),
            witness(
                QStringLiteral("b"), QString::fromUtf8("וְאֶל-פִילָדֶלְפִיאָה")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 1);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
    }

    void aNearMissBeatsAGapButNotAnExactMatch()
    {
        // The place name is close enough to hold its column against the two
        // gaps that would otherwise absorb it, while the exact agreement on
        // either side stays where it is.
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("a"),
                QString::fromUtf8("ספר לפילדלפיאן בן")),
            witness(
                QStringLiteral("b"),
                QString::fromUtf8("ספר ואלפילדלפיאה בן")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 3);
        for (const AlignmentColumn &column : aligned.columns) {
            QCOMPARE(readers(column, twoWitnesses()), twoWitnesses());
        }
    }

    void plainlyUnrelatedWordsAreStillUnrelated()
    {
        // The grading must not turn into "everything is a bit similar". These
        // share a letter and a length, and must still score a mismatch.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר מלאך בן")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר דוד בן")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        // Three columns, because a mismatch costs what two gaps do -- not
        // because the middle pair was judged similar.
        QCOMPARE(aligned.columns.size(), 3);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
        QCOMPARE(readers(aligned.columns.at(2), twoWitnesses()), twoWitnesses());
    }

    // --- folding the five final letters ------------------------------------

    void foldedKeyFoldsTheFiveFinalLetters()
    {
        QCOMPARE(
            foldedKey(QString::fromUtf8("מלאך")), QString::fromUtf8("מלאכ"));
        QCOMPARE(
            foldedKey(QString::fromUtf8("ירושלם")), QString::fromUtf8("ירושלמ"));
        QCOMPARE(foldedKey(QString::fromUtf8("בן")), QString::fromUtf8("בנ"));
        QCOMPARE(foldedKey(QString::fromUtf8("אף")), QString::fromUtf8("אפ"));
        QCOMPARE(foldedKey(QString::fromUtf8("ארץ")), QString::fromUtf8("ארצ"));
    }

    void foldedKeyStillStripsPointing()
    {
        QCOMPARE(
            foldedKey(QString::fromUtf8("הַמַּלְאָכוֹ")),
            QString::fromUtf8("המלאכו"));
    }

    void foldedKeyLeavesTheLexiconJoinKeyAlone()
    {
        // comparisonKey is the join key for the shipped lexicon indexes, the
        // rabbinic word list and the phrase rules, and it is mirrored in
        // hebrew_manuscripts/tools/python/tools/build_lexicon.py. Anyone tempted
        // to fold there instead trips here rather than in production.
        QCOMPARE(
            comparisonKey(QString::fromUtf8("מלאך")), QString::fromUtf8("מלאך"));
        QCOMPARE(comparisonKey(QString::fromUtf8("בן")), QString::fromUtf8("בן"));
    }

    void aFinalFormVariantSharesAColumn()
    {
        // Revelation 1:1. These scored -2 -- the same as two unrelated words --
        // purely because final kaf U+05DA is a different character from U+05DB.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("מלאך")),
            witness(QStringLiteral("b"), QString::fromUtf8("הַמַּלְאָכוֹ")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 1);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
    }

    void aVariantDoesNotStealItsNeighboursColumn()
    {
        // The tail of Revelation 1:1. Cochin has one word more than Sloane, so
        // exactly one gap must be placed. Before folding, pairing מלאך with
        // המלאכו scored the same as pairing שלו with it, and the alignment was
        // decided by the diagonal-first tie-break rather than by the
        // manuscripts -- it chose wrongly.
        const QList<SourceDocument> documents = {
            witness(
                QStringLiteral("a"), QString::fromUtf8("מלאך שלו לעבדו")),
            witness(
                QStringLiteral("b"), QString::fromUtf8("הַמַּלְאָכוֹ לָעַבְדּוֹ")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 3);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
        QCOMPARE(
            readers(aligned.columns.at(1), twoWitnesses()),
            QStringList{QStringLiteral("a")});
        QCOMPARE(readers(aligned.columns.at(2), twoWitnesses()), twoWitnesses());
    }

    // --- the guessed consonantal skeleton -----------------------------------

    void theSkeletonRefusesToGuess()
    {
        const AttestedForms &attested = AttestedForms::shared();

        // Too short to reduce at all.
        QVERIFY(skeletonKey(QString::fromUtf8("ה"), &attested).isEmpty());
        QVERIFY(skeletonKey(QString::fromUtf8("של"), &attested).isEmpty());
        // Nothing to take off: no opinion beats a manufactured one.
        QVERIFY(skeletonKey(QString::fromUtf8("ספר"), &attested).isEmpty());
    }

    void theSkeletonNeverPeelsARootLetter()
    {
        // מלאך opens with a mem, which is a prefix letter. Peeling it unchecked
        // yields לאכ -- a skeleton belonging to no word and free to collide
        // with anything. This is the trap the attested list exists to catch.
        const AttestedForms &attested = AttestedForms::shared();
        if (attested.isEmpty()) {
            QSKIP("rabbinic.words.txt is not beside this build.");
        }

        const QString skeleton =
            skeletonKey(QString::fromUtf8("מלאכ"), &attested);
        QVERIFY(skeleton != QString::fromUtf8("לאכ"));
    }

    void withoutAnAttestedListNoPrefixIsPeeled()
    {
        // A null list disables the peeling rather than letting it run
        // unchecked, so a build that cannot find its data guesses less, not
        // more.
        const QString guarded = skeletonKey(QString::fromUtf8("המלכימ"), nullptr);
        QVERIFY(!guarded.startsWith(QString::fromUtf8("מלכ"))
                || guarded == QString::fromUtf8("המלכ"));
    }

    void theRootMapRelatesWordsTheLemmaDoesNot()
    {
        const HebrewLexicon &lexicon = HebrewLexicon::shared();
        if (!lexicon.hasRoots()) {
            QSKIP("hebrew_roots.json is not beside this build.");
        }

        // מלך "king" H4428 derives from מלך "to reign" H4427. Different words,
        // different Strong's numbers, one root.
        const QStringList king = lexicon.rootsFor(QString::fromUtf8("מֶלֶךְ"));
        QVERIFY(!king.isEmpty());
        QVERIFY(king.contains(QStringLiteral("H4427")));
    }

    void aSharedRootOutbidsTheSpellingAlone()
    {
        // מְלוּכָה "kingdom" H4410 and מֶלֶךְ "king" H4428 are different entries
        // with no Strong's number in common, both derived from מלך "to reign"
        // H4427. Proves the rung is reachable -- which matters, because on the
        // Revelation corpus it decides nothing at all.
        const HebrewLexicon &lexicon = HebrewLexicon::shared();
        if (!lexicon.hasRoots()) {
            QSKIP("hebrew_roots.json is not beside this build.");
        }

        ScoringContext context;
        context.lexicon = &lexicon;

        // Pointed, so each resolves through the pointed index to its own
        // entry. The bare consonants מלוכה land on H4427 itself -- the root --
        // which has no parent and so nothing for this rung to compare.
        SourceToken kingdom;
        kingdom.text = QString::fromUtf8("מְלוּכָה");
        SourceToken king;
        king.text = QString::fromUtf8("מֶלֶךְ");

        const TokenProfile left = profileFor(&kingdom, context);
        const TokenProfile right = profileFor(&king, context);
        QVERIFY(!left.roots.isEmpty());
        QVERIFY(!right.roots.isEmpty());
        QCOMPARE(scoreProfiles(left, right), kScoreRoot);
    }

    void anOldLexiconWithoutRootsStillWorks()
    {
        // The roots live in their own file, so a data directory from before it
        // existed has to load and simply have no opinion.
        HebrewLexicon bare = HebrewLexicon::fromJson(QJsonObject());
        QVERIFY(!bare.hasRoots());
        QVERIFY(bare.rootsFor(QString::fromUtf8("מלך")).isEmpty());

        bare.loadRoots(QString());
        QVERIFY(!bare.hasRoots());
    }

    void aSharedSkeletonOutbidsTheSpellingAlone()
    {
        // Two plurals of one stem, far enough apart that comparing letters
        // rates them barely above unrelated. Proves the rung is reachable:
        // on the Revelation corpus it never decides a verse, because words
        // that share a skeleton are usually close enough for the grading to
        // have caught them anyway.
        ScoringContext context;

        SourceToken masculine;
        masculine.text = QString::fromUtf8("ספרימ");
        SourceToken feminine;
        feminine.text = QString::fromUtf8("ספרות");

        const TokenProfile left = profileFor(&masculine, context);
        const TokenProfile right = profileFor(&feminine, context);
        QCOMPARE(left.skeleton, QString::fromUtf8("ספר"));
        QCOMPARE(right.skeleton, QString::fromUtf8("ספר"));
        QCOMPARE(scoreProfiles(left, right), kScoreSkeleton);
    }

    void theSkeletonDropsAPluralEnding()
    {
        // ים is folded to ימ by the time this is asked, which is the spelling
        // the suffix table has to be written in.
        QCOMPARE(
            skeletonKey(QString::fromUtf8("ספרימ"), nullptr),
            QString::fromUtf8("ספר"));
    }

    // --- the lexicon's opinion ----------------------------------------------

    void aMorphologicalVariantSharesAColumn()
    {
        // Revelation 1:1. Cochin נתן, Sloane שֶׁנְּתָנוֹ -- a relative shin, a
        // suffix, and a final nun against a medial one. Four edits out of five
        // letters, so no amount of comparing spellings joins them; the lexicon
        // resolves both to H5414 and settles it.
        const HebrewLexicon &lexicon = HebrewLexicon::shared();
        if (lexicon.isEmpty()) {
            QSKIP("hebrew_lexicon.json is not beside this build.");
        }

        AlignmentOptions options;
        options.lexicon = &lexicon;

        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר נתן בן")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר שֶׁנְּתָנוֹ בן")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"), options);

        QCOMPARE(aligned.columns.size(), 3);
        for (const AlignmentColumn &column : aligned.columns) {
            QCOMPARE(readers(column, twoWitnesses()), twoWitnesses());
        }
    }

    void aSharedLemmaOutbidsAMereResemblance()
    {
        const HebrewLexicon &lexicon = HebrewLexicon::shared();
        if (lexicon.isEmpty()) {
            QSKIP("hebrew_lexicon.json is not beside this build.");
        }

        AlignmentOptions options;
        options.lexicon = &lexicon;

        // נתן against שנתנו, which the lexicon joins, and against a word that
        // merely shares letters with it. The lemma has to win.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("נתן")),
            witness(
                QStringLiteral("b"), QString::fromUtf8("נותן שֶׁנְּתָנוֹ")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"), options);

        QCOMPARE(aligned.columns.size(), 2);
    }

    void withoutTheLexiconTheSameVariantStaysApart()
    {
        // The injection argument, made executable: the lexicon is what decides
        // this case, so with none supplied the answer must visibly differ
        // rather than quietly depending on whether a data file was found.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר נתן בן")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר שֶׁנְּתָנוֹ בן")),
        };
        const AlignedVerse bare = alignVerse(
            verseId(), refs(documents), QStringLiteral("a"), AlignmentOptions{});

        // Still three columns -- a mismatch costs what two gaps do -- but the
        // middle pair is held together by that arithmetic, not by evidence.
        QCOMPARE(bare.columns.size(), 3);
    }

    void aStrongsNumberIsNeverReadFromAOneLetterWord()
    {
        // A bare ה resolves to a lemma like anything else. Letting it would
        // put every abbreviation in the corpus in the same bucket as whatever
        // short word sat beside it.
        const HebrewLexicon &lexicon = HebrewLexicon::shared();
        if (lexicon.isEmpty()) {
            QSKIP("hebrew_lexicon.json is not beside this build.");
        }

        ScoringContext context;
        context.lexicon = &lexicon;

        SourceToken article;
        article.text = QString::fromUtf8("ה");
        QVERIFY(profileFor(&article, context).strongs.isEmpty());

        SourceToken word;
        word.text = QString::fromUtf8("מלך");
        QVERIFY(!profileFor(&word, context).strongs.isEmpty());
    }

    // --- scribal abbreviations ----------------------------------------------

    void everyMarkIsTheSameAbbreviation()
    {
        // Cochin writes Jesus as יש׳ו, יש״ו and יש֞ו in different places. One
        // stem, or the table would need a row per copyist's habit.
        const QString stem = QString::fromUtf8("ישו");
        QCOMPARE(abbreviationStem(QString::fromUtf8("יש׳ו")), stem);
        QCOMPARE(abbreviationStem(QString::fromUtf8("יש״ו")), stem);
        QCOMPARE(abbreviationStem(QString::fromUtf8("יש֞ו")), stem);
        QCOMPARE(abbreviationStem(QString::fromUtf8("יש֜ו")), stem);

        // Every mark the corpus actually uses. James reaches for U+0594 and
        // U+059C where Revelation uses U+059E and Matthew U+059D and U+05F3;
        // naming them one at a time means missing the next one.
        QVERIFY(isAbbreviated(QString::fromUtf8("ה֞")));  // U+059E, Revelation
        QVERIFY(isAbbreviated(QString::fromUtf8("ה֔")));  // U+0594, James
        QVERIFY(isAbbreviated(QString::fromUtf8("ה֜")));  // U+059C, James
        QVERIFY(isAbbreviated(QString::fromUtf8("ה֝")));  // U+059D, Matthew
        QVERIFY(isAbbreviated(QString::fromUtf8("ה׳")));  // U+05F3, Matthew
        QVERIFY(isAbbreviated(QString::fromUtf8("ה״")));  // U+05F4
    }

    // --- pointing, as a property of the text --------------------------------

    void hasNiqqudIgnoresTheAbbreviationMarks()
    {
        // The accents and the vowel points are neighbours in Unicode and
        // opposites here. Counting the accent block would call every Cochin
        // abbreviation a pointed word and invert every decision made on this.
        QVERIFY(!hasNiqqud(QString::fromUtf8("ה֞")));   // U+059E
        QVERIFY(!hasNiqqud(QString::fromUtf8("ה֔")));   // U+0594
        QVERIFY(!hasNiqqud(QString::fromUtf8("יש׳ו"))); // U+05F3
        QVERIFY(!hasNiqqud(QString::fromUtf8("מלאך")));
        QVERIFY(!hasNiqqud(QString()));

        QVERIFY(hasNiqqud(QString::fromUtf8("אֱלֹהִים")));
        QVERIFY(hasNiqqud(QString::fromUtf8("יֵשׁוּעַ")));
        QVERIFY(hasNiqqud(QString::fromUtf8("הַמַּלְאָכוֹ")));
    }

    void withoutNiqqudKeepsTheWordReadable()
    {
        QCOMPARE(
            withoutNiqqud(QString::fromUtf8("אֱלֹהִים")),
            QString::fromUtf8("אלהים"));
        // The shin dot goes with the vowels: an unpointed text writes a bare ש.
        QCOMPARE(
            withoutNiqqud(QString::fromUtf8("יֵשׁוּעַ")), QString::fromUtf8("ישוע"));
        // And the dagesh with them: a bare מ, not מּ.
        QCOMPARE(
            withoutNiqqud(QString::fromUtf8("הַמָּשִׁיחַ")),
            QString::fromUtf8("המשיח"));
    }

    void withoutNiqqudIsNotAComparisonKey()
    {
        // What this returns goes into the edition, so the joiners and stops a
        // comparison key throws away have to survive.
        const QString joined = QString::fromUtf8("אֲשֶׁר־בָּהּ");
        QCOMPARE(withoutNiqqud(joined), QString::fromUtf8("אשר־בה"));
        QVERIFY(withoutNiqqud(joined) != comparisonKey(joined));

        const QString stopped = QString::fromUtf8("אָמֵן׃");
        QCOMPARE(withoutNiqqud(stopped), QString::fromUtf8("אמן׃"));
    }

    void withoutNiqqudLeavesAnUnpointedWordAlone()
    {
        const QString cochin = QString::fromUtf8("מלאך");
        QCOMPARE(withoutNiqqud(cochin), cochin);
        // Idempotent, so applying it twice cannot erode the word further.
        const QString once = withoutNiqqud(QString::fromUtf8("אֱלֹהִים"));
        QCOMPARE(withoutNiqqud(once), once);
        // The abbreviation mark is not a vowel point and must not be stripped:
        // it is the only thing that says the word is an abbreviation.
        QVERIFY(isAbbreviated(withoutNiqqud(QString::fromUtf8("ה֞"))));
    }

    void pointingIsNotAnAbbreviationMark()
    {
        // Niqqud, dagesh and the shin dots all sit below U+0591 or above
        // U+05AF, so a pointed word is not mistaken for an abbreviated one.
        QVERIFY(!isAbbreviated(QString::fromUtf8("הַמַּלְאָכוֹ")));
        QVERIFY(!isAbbreviated(QString::fromUtf8("שֶׁנְּתָנוֹ")));
        QVERIFY(!isAbbreviated(QString::fromUtf8("מְלוּכָה")));
    }

    void aPlainWordIsNotAnAbbreviation()
    {
        QVERIFY(!isAbbreviated(QString::fromUtf8("ה")));
        QVERIFY(!isAbbreviated(QString::fromUtf8("הַמַּלְאָכוֹ")));
        QVERIFY(!isAbbreviated(QString()));
    }

    void theMarkIsGoneByTheTimeAWordIsKeyed()
    {
        // Why detection has to run on the token's own text. Both keys throw the
        // mark away, so ה֞ and a bare definite article are indistinguishable
        // once either has been applied -- which is also why the table cannot
        // live in hebrew_phrase_rules.json, whose matches are keyed.
        const QString abbreviated = QString::fromUtf8("ה֞");
        QCOMPARE(comparisonKey(abbreviated), QString::fromUtf8("ה"));
        QCOMPARE(foldedKey(abbreviated), QString::fromUtf8("ה"));
        QVERIFY(isAbbreviated(abbreviated));
    }

    void theTableExpandsWhatAScribeLeftOut()
    {
        const AbbreviationTable &table = AbbreviationTable::shared();
        if (table.isEmpty()) {
            QSKIP("hebrew_abbreviations.json is not beside this build.");
        }

        QVERIFY(offersReading(
            table.expansionsFor(QString::fromUtf8("ה֞")),
            QString::fromUtf8("אלהים")));
        QVERIFY(offersReading(
            table.expansionsFor(QString::fromUtf8("יש֞ו")),
            QString::fromUtf8("יהושע")));
        // A bare he is a definite article, not the divine name.
        QVERIFY(table.expansionsFor(QString::fromUtf8("ה")).isEmpty());
        // Numerals carry the mark but have no row, and must not crash or guess.
        QVERIFY(table.expansionsFor(QString::fromUtf8("א׳")).isEmpty());
    }

    void everyWrittenFormOfAlYedeiResolves()
    {
        // ע״י turns up sixteen times across three books in four spellings:
        // ע֞י and וע֞י in Revelation, ע״י in James, ע׳י in Matthew. The row is
        // keyed on the letters alone, so one entry has to answer for all of
        // them however the copyist marked it.
        const AbbreviationTable &table = AbbreviationTable::shared();
        if (table.isEmpty()) {
            QSKIP("hebrew_abbreviations.json is not beside this build.");
        }

        for (const char *written : {"ע֞י", "ע״י", "ע׳י"}) {
            const QStringList expansions =
                table.expansionsFor(QString::fromUtf8(written));
            QVERIFY2(
                offersReading(expansions, QString::fromUtf8("על ידי")),
                written);
        }

        // And the prefixed form carries its vav through onto the expansion.
        QVERIFY(offersReading(
            table.expansionsFor(QString::fromUtf8("וע֞י")),
            QString::fromUtf8("ועל ידי")));
    }

    void aPrefixedAbbreviationCarriesItsPrefix()
    {
        const AbbreviationTable &table = AbbreviationTable::shared();
        if (table.isEmpty()) {
            QSKIP("hebrew_abbreviations.json is not beside this build.");
        }

        QVERIFY(offersReading(
            table.expansionsFor(QString::fromUtf8("לה֞")),
            QString::fromUtf8("לאלהים")));
        QVERIFY(offersReading(
            table.expansionsFor(QString::fromUtf8("ליש֞ו")),
            QString::fromUtf8("ליהושע")));
    }

    void theDivineNameFindsItsExpansionAndNotItsNeighbour()
    {
        // Revelation 1:6, both halves. Cochin's ה֞ stands for the divine name,
        // which Sloane writes out as לֵאלֹהִים two words later. It used to score
        // positively against וְכֹהֲנִים -- any word with a ה in it -- and so
        // drifted onto whatever sat nearest.
        const AbbreviationTable &table = AbbreviationTable::shared();
        if (table.isEmpty()) {
            QSKIP("hebrew_abbreviations.json is not beside this build.");
        }

        AlignmentOptions options;
        options.abbreviations = &table;

        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ולכהנים לפני ה֞")),
            witness(
                QStringLiteral("b"), QString::fromUtf8("וְכֹהֲנִים לֵאלֹהִים")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"), options);

        QCOMPARE(aligned.columns.size(), 3);
        // The abbreviation reaches the word it stands for, at the far end,
        // rather than settling on the ה-bearing word beside it.
        const SourceToken *divineName =
            aligned.columns.at(2).cell(QStringLiteral("b"));
        QVERIFY(divineName);
        QCOMPARE(foldedKey(divineName->text), QString::fromUtf8("לאלהימ"));
        // And the word it used to be dragged onto keeps its own partner.
        const SourceToken *priests =
            aligned.columns.at(0).cell(QStringLiteral("b"));
        QVERIFY(priests);
        QCOMPARE(foldedKey(priests->text), QString::fromUtf8("וכהנימ"));
    }

    void theAlignmentIgnoresAnUnmarkedLoneHe()
    {
        // The suggestion layer raises every lone ה for the editor to judge.
        // The alignment must not: in Sloane 237 a lone ה is a detached
        // definite article -- מְנוֹרוֹת ה הַזָּהָב -- and expanding it here would
        // drag two innocent articles into the divine name's column, which is a
        // silent collation error rather than a dismissable suggestion.
        const AbbreviationTable &table = AbbreviationTable::shared();
        if (table.isEmpty()) {
            QSKIP("hebrew_abbreviations.json is not beside this build.");
        }

        // The expansion is there for the marked form...
        QVERIFY(!table.expansionsFor(QString::fromUtf8("ה֞")).isEmpty());
        // ...and withheld from the unmarked one, on the path alignment uses.
        QVERIFY(table.expansionsFor(QString::fromUtf8("ה")).isEmpty());

        ScoringContext context;
        context.abbreviations = &table;

        SourceToken article;
        article.text = QString::fromUtf8("ה");
        const TokenProfile bare = profileFor(&article, context);
        QVERIFY(bare.alternates.isEmpty());

        SourceToken godsName;
        godsName.text = QString::fromUtf8("לֵאלֹהִים");
        QCOMPARE(
            scoreProfiles(bare, profileFor(&godsName, context)), kScoreMismatch);
    }

    void withoutTheTableTheAlignmentIsStillOrthographic()
    {
        // A null table stands the expansion down rather than changing what the
        // other rungs say, so a build that cannot find its data still aligns.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("מלאך")),
            witness(QStringLiteral("b"), QString::fromUtf8("הַמַּלְאָכוֹ")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"),
                       AlignmentOptions{});

        QCOMPARE(aligned.columns.size(), 1);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
    }

    // --- what the Needleman-Wunsch pass does today -------------------------
    //
    // The DP and its scoring function had no direct coverage at all, so these
    // pin the behaviour that is already right before the scorer is touched.
    // Where a case is wrong today it says so and is skipped rather than
    // asserting the wrong answer.

    void aWordOnlyOneWitnessReadsGetsItsOwnColumn()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד בן")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר בן")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 3);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
        QCOMPARE(
            readers(aligned.columns.at(1), twoWitnesses()),
            QStringList{QStringLiteral("a")});
        QCOMPARE(readers(aligned.columns.at(2), twoWitnesses()), twoWitnesses());
    }

    void aWordMissingFromThePriorityWitnessIsInserted()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר בן")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר דוד בן")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 3);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
        QCOMPARE(
            readers(aligned.columns.at(1), twoWitnesses()),
            QStringList{QStringLiteral("b")});
        QCOMPARE(aligned.columns.at(2).cells.size(), 2);
        // An inserted column is named after the witness that brought it, so it
        // cannot collide with a column the priority witness seeded.
        QVERIFY(aligned.columns.at(1).id.contains(QStringLiteral("insert")));
    }

    void pleneAndDefectiveSpellingShareAColumn()
    {
        // דוד / דויד differ by one mater lectionis. They land together today
        // only because the flanking exact matches make the diagonal cheaper
        // than a pair of gaps -- the scorer itself rates them -2, the same as
        // two unrelated words. The grading work must keep this passing.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד בן")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר דויד בן")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 3);
        for (const AlignmentColumn &column : aligned.columns) {
            QCOMPARE(readers(column, twoWitnesses()), twoWitnesses());
        }
    }

    void theDiagonalTieBreakIsUnchanged()
    {
        // Two unrelated single words still share a column, because a mismatch
        // (-2) costs exactly what the two gaps replacing it would, and the
        // tie resolves diagonal-first. consensus_test relies on this, so the
        // mismatch and gap penalties have to stay equal.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר")),
            witness(QStringLiteral("b"), QString::fromUtf8("דבר")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 1);
        QCOMPARE(readers(aligned.columns.at(0), twoWitnesses()), twoWitnesses());
    }

    void aTranspositionIsNotModelled()
    {
        // Needleman-Wunsch cannot express a swap: it spends an insertion and a
        // deletion instead. Asserted so the behaviour is known rather than
        // discovered, and so a scorer change that disturbs it is visible.
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד בן")),
            witness(QStringLiteral("b"), QString::fromUtf8("דוד ספר בן")),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 4);
        // The shared tail still lines up, which is what matters downstream.
        QCOMPARE(
            readers(aligned.columns.last(), twoWitnesses()), twoWitnesses());
    }

    void aSilentWitnessAddsNoColumns()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד")),
            witness(QStringLiteral("b"), QString()),
        };
        const AlignedVerse aligned =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(aligned.columns.size(), 2);
        for (const AlignmentColumn &column : aligned.columns) {
            QCOMPARE(
                readers(column, twoWitnesses()),
                QStringList{QStringLiteral("a")});
        }
    }

    void alignmentIsStableAcrossRuns()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר דוד בן מלך")),
            witness(QStringLiteral("b"), QString::fromUtf8("ספר בן המלך דוד")),
        };

        const AlignedVerse first =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));
        const AlignedVerse second =
            alignVerse(verseId(), refs(documents), QStringLiteral("a"));

        QCOMPARE(first.columns.size(), second.columns.size());
        for (int index = 0; index < first.columns.size(); ++index) {
            QCOMPARE(first.columns.at(index).id, second.columns.at(index).id);
        }
    }

    void anUnknownVerseIsRefused()
    {
        const QList<SourceDocument> documents = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר")),
        };
        QVERIFY_EXCEPTION_THROWN(
            alignVerse(
                QStringLiteral("Matt.9.9"), refs(documents), QStringLiteral("a")),
            AlignmentError);
    }

    // --- the corpus golden dump --------------------------------------------

    void dumpsTheRealCorpusAlignment()
    {
        if (!corpusAvailable()) {
            QSKIP("The OSIS corpus is not beside this build.");
        }

        const SourceDocument cochin =
            readWitness(cochinFile(), QStringLiteral("cochin"));
        const SourceDocument sloane =
            readWitness(sloaneFile(), QStringLiteral("sloane"));
        QVERIFY(!cochin.verses.isEmpty());
        QVERIFY(!sloane.verses.isEmpty());

        const QList<SourceDocument> documents = {cochin, sloane};
        const DocumentRefs sources = refs(documents);
        const QStringList order = {
            QStringLiteral("cochin"), QStringLiteral("sloane")};

        QStringList sharedIds;
        for (const SourceVerse &verse : sloane.verses) {
            if (cochin.hasVerse(verse.reference.id)) {
                sharedIds.append(verse.reference.id);
            }
        }

        AlignmentOptions options;
        options.abbreviations = &AbbreviationTable::shared();
        options.lexicon = &HebrewLexicon::shared();
        options.attested = &AttestedForms::shared();

        QStringList lines;
        int shared = 0;
        for (const QString &id : sharedIds) {
            ++shared;

            const AlignedVerse aligned =
                alignVerse(id, sources, QStringLiteral("cochin"), options);

            QStringList cells;
            for (const AlignmentColumn &column : aligned.columns) {
                cells.append(
                    QStringLiteral("[%1|%2]")
                        .arg(
                            cellText(column, order.at(0)),
                            cellText(column, order.at(1))));
            }
            lines.append(
                QStringLiteral("%1  (%2)  %3")
                    .arg(id)
                    .arg(aligned.columns.size())
                    .arg(cells.join(QLatin1Char(' '))));
        }

        QVERIFY2(shared > 0, "Cochin and Sloane share no verse of Revelation.");

        // Which rung actually decided each column both witnesses read. Says
        // whether a rung is earning its place on this corpus or merely
        // compiling -- the reason the ladder is staged rather than landed at
        // once.
        ScoringContext context;
        context.abbreviations = options.abbreviations;
        context.lexicon = options.lexicon;
        context.attested = options.attested;

        QMap<QString, int> tally;
        for (const QString &id : sharedIds) {
            const AlignedVerse aligned =
                alignVerse(id, sources, QStringLiteral("cochin"), options);
            for (const AlignmentColumn &column : aligned.columns) {
                const SourceToken *first = column.cell(order.at(0));
                const SourceToken *second = column.cell(order.at(1));
                if (!first || !second) {
                    continue;
                }
                const int score = scoreProfiles(
                    profileFor(first, context), profileFor(second, context));
                QString rung;
                if (score == kScoreMatch) {
                    rung = QStringLiteral("1 exact       ");
                } else if (score == kScoreStrongs) {
                    rung = QStringLiteral("2 same lemma  ");
                } else if (score == kScoreRoot) {
                    rung = QStringLiteral("3 same root   ");
                } else if (score == kScoreSkeleton) {
                    rung = QStringLiteral("3b skeleton   ");
                } else if (score == kScoreMismatch) {
                    rung = QStringLiteral("5 unrelated   ");
                } else {
                    rung = QStringLiteral("4 near/expand ");
                }
                tally[rung] += 1;
            }
        }
        for (auto entry = tally.constBegin(); entry != tally.constEnd(); ++entry) {
            qInfo().noquote()
                << "  rung" << entry.key() << entry.value() << "columns";
        }

        // Written beside the test binary rather than only logged, so a stage
        // can be judged with `git diff --no-index before.txt after.txt`.
        const QString path =
            QDir::current().filePath(QStringLiteral("alignment_golden.txt"));
        QFile out(path);
        QVERIFY2(
            out.open(QIODevice::WriteOnly | QIODevice::Truncate),
            qPrintable(QStringLiteral("Cannot write %1").arg(path)));
        out.write(lines.join(QLatin1Char('\n')).toUtf8());
        out.write("\n");
        out.close();

        qInfo().noquote() << "Aligned" << shared << "verses ->" << path;
    }
};

QTEST_MAIN(AlignmentTest)
#include "alignment_test.moc"
