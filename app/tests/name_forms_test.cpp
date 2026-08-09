#include "core/name_forms.h"

#include <QJsonDocument>
#include <QSet>
#include <QtTest>

using namespace milah;

namespace {

NameForms namesFrom(const QByteArray &json)
{
    return NameForms::fromJson(QJsonDocument::fromJson(json).object());
}

/// John, Jesus and Laodicea — enough to exercise every rule without depending
/// on the shipped file. Delimited R"JSON( … )JSON" to match the other tests.
QByteArray sampleNames()
{
    return R"JSON({"names":[
      {"id":"john","prefer":"יוֹחָנָן","forms":["יאהנניס","יהאנניס"]},
      {"id":"jesus","prefer":"יֵשׁוּעַ","forms":["ישו","יהושע"]},
      {"id":"laodicea","prefer":"לָאוֹדִיקְיָא","forms":["לאדיצאן","לאודיקיאה"]},
      {"id":"ephesus","prefer":"אֶפֶסוֹס","forms":["עפהיזוס"]}
    ]})JSON";
}

/// The group `text` resolves to, by id, or empty when it resolves to none.
QString idOf(const NameForms &names, const QString &text)
{
    const NameGroup *group = names.group(names.groupFor(text));
    return group ? group->id : QString();
}

} // namespace

/// The one thing in Milah that can say two spellings are the same name. No
/// measure of similarity reaches יאהנניס and יוֹחָנָן, so this table is asserted
/// rather than computed — which makes its edges worth pinning.
class NameFormsTest final : public QObject
{
    Q_OBJECT

private slots:
    void aTransliterationAndTheHebrewNameAreOneGroup()
    {
        // The case the whole feature exists for.
        const NameForms names = namesFrom(sampleNames());
        QCOMPARE(idOf(names, QString::fromUtf8("יאהנניס")), QStringLiteral("john"));
        QCOMPARE(idOf(names, QString::fromUtf8("יוֹחָנָן")), QStringLiteral("john"));
    }

    void bothScribalSpellingsOfJohnAgree()
    {
        const NameForms names = namesFrom(sampleNames());
        QCOMPARE(
            names.groupFor(QString::fromUtf8("יאהנניס")),
            names.groupFor(QString::fromUtf8("יהאנניס")));
    }

    void thePreferredFormIsItsOwnGroupMember()
    {
        // It is not listed in forms, and must still resolve: otherwise a
        // witness reading the Hebrew name would not align with one reading the
        // transliteration.
        const NameForms names = namesFrom(sampleNames());
        QCOMPARE(idOf(names, QString::fromUtf8("יוחנן")), QStringLiteral("john"));
    }

    void pointingDoesNotDecideAGroup()
    {
        const NameForms names = namesFrom(sampleNames());
        QCOMPARE(idOf(names, QString::fromUtf8("יוחנן")), QStringLiteral("john"));
        QCOMPARE(idOf(names, QString::fromUtf8("יוֹחָנָן")), QStringLiteral("john"));
    }

    void aFinalLetterDoesNotSplitAGroup()
    {
        // Pins the foldedKey choice. Cochin writes פירגימום and לסמירנון with
        // final letters where the same names end plain elsewhere; a table keyed
        // on comparisonKey would need every ending listed twice.
        const NameForms names = namesFrom(
            R"JSON({"names":[{"id":"x","prefer":"סְמוּרְנָא","forms":["סמירנונ"]}]})JSON");
        QCOMPARE(idOf(names, QString::fromUtf8("סמירנון")), QStringLiteral("x"));
    }

    void onePrefixLetterIsPeeled()
    {
        const NameForms names = namesFrom(sampleNames());
        QString peeled;
        QCOMPARE(
            names.group(names.groupFor(QString::fromUtf8("ליאהנניס"), &peeled))->id,
            QStringLiteral("john"));
        QCOMPARE(peeled, QString::fromUtf8("ל"));
    }

    void twoPrefixLettersArePeeled()
    {
        // Cochin's Ephesus at Rev 1:11 carries both the conjunction and the
        // preposition.
        const NameForms names = namesFrom(sampleNames());
        QString peeled;
        QCOMPARE(
            names.group(names.groupFor(QString::fromUtf8("ולעפהיזוס"), &peeled))->id,
            QStringLiteral("ephesus"));
        QCOMPARE(peeled, QString::fromUtf8("ול"));
    }

    void theShorterPeelWinsSoLaodiceaKeepsItsLamed()
    {
        // The most important test here. Cochin's ולאדיצאן has to come off as
        // ו + לאדיצאן, because the name itself begins with a lamed; a greedy
        // two-letter peel would look for אדיצאן and find nothing. Sloane's
        // לאודיקיאה has to resolve whole, without its own lamed being taken for
        // a preposition. Only none-then-one-then-two answers both.
        const NameForms names = namesFrom(sampleNames());

        QString peeled;
        QCOMPARE(
            names.group(names.groupFor(QString::fromUtf8("ולאדיצאן"), &peeled))->id,
            QStringLiteral("laodicea"));
        QCOMPARE(peeled, QString::fromUtf8("ו"));

        QCOMPARE(
            names.group(names.groupFor(QString::fromUtf8("לאודיקיאה"), &peeled))->id,
            QStringLiteral("laodicea"));
        QVERIFY(peeled.isEmpty());
    }

    void anOrdinaryWordIsNotPeeledIntoAName()
    {
        // Peeling only ever succeeds onto an exact table key, so a word that
        // merely begins with a prefix letter resolves to nothing.
        const NameForms names = namesFrom(sampleNames());
        for (const char *word : {"וישלח", "הישועה", "במישור", "ישולם"}) {
            QVERIFY2(
                names.groupFor(QString::fromUtf8(word)) < 0,
                word);
        }
    }

    void aGluedParticleIsNotPeeledButMayBeListed()
    {
        // Sloane writes ואלסמרנה: ו + אל + סמרנה. Alef is not a prefix letter
        // and must not become one — אל is 'god', 'to', 'not' and 'these'
        // depending on pointing. The glued form is listed literally instead,
        // and the ו comes off it.
        const NameForms bare = namesFrom(
            R"JSON({"names":[{"id":"smyrna","prefer":"סְמוּרְנָא","forms":["סמרנה"]}]})JSON");
        QVERIFY(bare.groupFor(QString::fromUtf8("ואלסמרנה")) < 0);

        const NameForms listed = namesFrom(
            R"JSON({"names":[{"id":"smyrna","prefer":"סְמוּרְנָא",
                              "forms":["סמרנה","אלסמרנה"]}]})JSON");
        QCOMPARE(idOf(listed, QString::fromUtf8("ואלסמרנה")), QStringLiteral("smyrna"));
    }

    void aFormShorterThanThreeLettersIsRefused()
    {
        // Two letters are not evidence, and a two-letter group would claim
        // every prefixed particle in the corpus once peeling is allowed.
        const NameForms names = namesFrom(
            R"JSON({"names":[{"id":"x","prefer":"יוֹחָנָן","forms":["בן"]}]})JSON");
        QVERIFY(names.groupFor(QString::fromUtf8("בן")) < 0);
        // The preferred form is long enough, so the group itself survives.
        QCOMPARE(idOf(names, QString::fromUtf8("יוחנן")), QStringLiteral("x"));
    }

    void aGroupWithNothingLongEnoughIsDropped()
    {
        QVERIFY(namesFrom(
                    R"JSON({"names":[{"id":"x","prefer":"בן","forms":["של"]}]})JSON")
                    .isEmpty());
    }

    void anEntryWithoutAnIdOrAPreferredFormIsDropped()
    {
        QVERIFY(namesFrom(
                    R"JSON({"names":[
                      {"prefer":"יוֹחָנָן","forms":["יאהנניס"]},
                      {"id":"nameless","forms":["יאהנניס"]}
                    ]})JSON")
                    .isEmpty());
    }

    void aFormClaimedByTwoGroupsStaysWithTheFirst()
    {
        const NameForms names = namesFrom(
            R"JSON({"names":[
              {"id":"first","prefer":"יוֹחָנָן","forms":["יאהנניס"]},
              {"id":"second","prefer":"יֵשׁוּעַ","forms":["יאהנניס"]}
            ]})JSON");
        QCOMPARE(idOf(names, QString::fromUtf8("יאהנניס")), QStringLiteral("first"));
    }

    void anEmptyOrUnreadableTableIsSilent()
    {
        QVERIFY(namesFrom(QByteArray("{}")).isEmpty());
        QVERIFY(namesFrom(QByteArray("not json")).isEmpty());
        QVERIFY(NameForms::fromFile(QStringLiteral("nowhere.json")).isEmpty());
    }

    // --- merging the editor's own file --------------------------------------

    void theEditorsFileAddsAGroup()
    {
        NameForms names = namesFrom(sampleNames());
        names.append(namesFrom(
            R"JSON({"names":[{"id":"patmos","prefer":"פַּטְמוֹס","forms":["פטמוש"]}]})JSON"));
        QCOMPARE(idOf(names, QString::fromUtf8("פטמוש")), QStringLiteral("patmos"));
        QCOMPARE(idOf(names, QString::fromUtf8("יאהנניס")), QStringLiteral("john"));
    }

    void theEditorsFileReplacesAGroupWhole()
    {
        // Replace and not merge: a group is a set of spellings, and an editor
        // who finds a wrong one in the shipped table has to be able to take it
        // out. Appending could only ever add.
        NameForms names = namesFrom(sampleNames());
        names.append(namesFrom(
            R"JSON({"names":[{"id":"john","prefer":"יוֹחָנָן","forms":["יאהנניס"]}]})JSON"));

        QCOMPARE(idOf(names, QString::fromUtf8("יאהנניס")), QStringLiteral("john"));
        // יהאנניס was dropped by the replacement and must stop resolving.
        QVERIFY(names.groupFor(QString::fromUtf8("יהאנניס")) < 0);
    }

    // --- the shipped table --------------------------------------------------
    //
    // Nothing else in this suite asserts on the content of a shipped data file.
    // A name table that loads but knows none of the corpus's names would pass
    // every test above and fix nothing.

    void theShippedTableKnowsTheCorpusNames_data()
    {
        QTest::addColumn<QString>("spelling");
        QTest::addColumn<QString>("id");

        // Every spelling of a seeded name that occurs in the two Revelation
        // witnesses, taken from the texts rather than by eye. The prefixed ones
        // are here on purpose: they are what proves the peeling reaches them.
        //
        // Sloane's אל-prefixed spellings are deliberately absent. It writes
        // וְאֶל-סְמֻרְנָה, and the parser divides at the hyphen, so the bare name
        // is what the table is asked about. The glued forms remain in the file
        // as insurance and are covered by noShippedFormPeelsOntoAnotherGroup.
        const QList<QPair<const char *, const char *>> rows{
            {"יאהנניס", "john"},        {"יהאנניס", "john"},
            {"יאנניס", "john"},         {"יוחנן", "john"},
            {"ישו", "jesus"},           {"לישו", "jesus"},
            {"מישו", "jesus"},          {"ומישו", "jesus"},
            {"יהושע", "jesus"},         {"ומיהושע", "jesus"},
            {"פטמס", "patmos"},         {"פטמוש", "patmos"},
            {"ולעפהיזוס", "ephesus"},   {"עפהיזוס", "ephesus"},
            {"אפשוס", "ephesus"},       {"באפשוס", "ephesus"},
            {"לסמירנון", "smyrna"},     {"זמירנין", "smyrna"},
            {"סמרנה", "smyrna"},        {"בסמרנה", "smyrna"},
            {"ולפירגימוס", "pergamum"}, {"פירגימום", "pergamum"},
            {"פרגמוש", "pergamum"},     {"בפרגמוש", "pergamum"},
            {"לטיאטירס", "thyatira"},   {"טיאטירא", "thyatira"},
            {"בטיאטירא", "thyatira"},   {"תיאטירה", "thyatira"},
            {"ולזארדס", "sardis"},      {"סרדש", "sardis"},
            {"לפילדלפיאן", "philadelphia"},
            {"פילדלפיאה", "philadelphia"},
            {"ולאדיצאן", "laodicea"},   {"לאדיצא", "laodicea"},
            {"לאודיקיאה", "laodicea"},
        };
        for (const auto &row : rows) {
            QTest::newRow(row.first)
                << QString::fromUtf8(row.first) << QString::fromLatin1(row.second);
        }
    }

    void theShippedTableKnowsTheCorpusNames()
    {
        if (NameForms::shared().isEmpty()) {
            QSKIP("hebrew_names.json is not beside this build.");
        }
        QFETCH(QString, spelling);
        QFETCH(QString, id);

        // The id, not merely that it resolves: an assertion that a spelling
        // resolves would pass just as happily with John filed under Jesus.
        const NameGroup *group = NameForms::shared().group(
            NameForms::shared().groupFor(spelling));
        QVERIFY2(group != nullptr, qPrintable(spelling));
        QCOMPARE(group->id, id);
    }

    void theShippedGroupsAreDistinctAndLongEnough()
    {
        if (NameForms::shared().isEmpty()) {
            QSKIP("hebrew_names.json is not beside this build.");
        }

        QSet<QString> ids;
        for (const NameGroup &group : NameForms::shared().groups()) {
            QVERIFY2(!ids.contains(group.id), qPrintable(group.id));
            ids.insert(group.id);
            QVERIFY2(!group.preferred.isEmpty(), qPrintable(group.id));
            for (const QString &key : group.keys) {
                QVERIFY2(key.size() >= 3, qPrintable(group.id + QLatin1Char(' ') + key));
            }
        }
    }

    void noShippedFormPeelsOntoAnotherGroup()
    {
        // A later addition could quietly create a cross-group collision: a form
        // of one name that, once a prefix comes off, is a form of another.
        // Whichever answer lookup gave would be arbitrary.
        if (NameForms::shared().isEmpty()) {
            QSKIP("hebrew_names.json is not beside this build.");
        }

        const NameForms &names = NameForms::shared();
        for (const NameGroup &group : names.groups()) {
            for (const QString &key : group.keys) {
                const NameGroup *found = names.group(names.groupFor(key));
                QVERIFY2(
                    found != nullptr && found->id == group.id,
                    qPrintable(group.id + QStringLiteral(" lost ") + key));
            }
        }
    }
};

QTEST_MAIN(NameFormsTest)
#include "name_forms_test.moc"
