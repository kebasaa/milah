#include "core/osis.h"
#include "core/tokenize.h"
#include "test_data.h"

#include <QtTest>

using namespace milah;

namespace {

ParseOptions options(const QString &id, const QString &name)
{
    ParseOptions parseOptions;
    parseOptions.id = id;
    parseOptions.name = name;
    parseOptions.role = SourceRole::Manuscript;
    return parseOptions;
}

} // namespace

class OsisTest final : public QObject
{
    Q_OBJECT

private slots:
    void readsHebrewAndAnchorsAnInlineNote()
    {
        const SourceDocument document = parseOsis(
            QString::fromUtf8(milah_test::kSampleOsis),
            options(QStringLiteral("witness-a"), QStringLiteral("test.osis")));

        const SourceVerse *verse = document.verse(QStringLiteral("Matt.1.1"));
        QVERIFY(verse != nullptr);
        QCOMPARE(verse->text, QString::fromUtf8("ספר הולדת"));
        QVERIFY(!verse->tokens.isEmpty());
        QVERIFY(!verse->tokens.at(0).notes.isEmpty());
        QCOMPARE(verse->tokens.at(0).notes.at(0).text, QStringLiteral("A comment"));
    }

    void punctuationStandsApartInAManuscript()
    {
        QCOMPARE(
            tokenize(QStringLiteral("Matt.1.1"), QStringLiteral("(horah),"), {})
                .size(),
            4);
    }

    void punctuationBelongsToTheWordInATranslation()
    {
        // A bracket around a gloss is part of the gloss, not a word of its own:
        // "(horah)," is one interlinear cell rather than four.
        const QList<SourceToken> tokens = tokenize(
            QStringLiteral("Matt.1.1"),
            QStringLiteral("(horah), to do"),
            {},
            TokenStyle::Attached);

        QCOMPARE(tokens.size(), 3);
        QCOMPARE(tokens.at(0).text, QStringLiteral("(horah),"));
        QCOMPARE(tokens.at(1).text, QStringLiteral("to"));
        QCOMPARE(tokens.at(2).text, QStringLiteral("do"));
    }

    void theRoleDecidesHowAVerseIsSplit()
    {
        // The choice is made by parseOsis from the role, so a translation and a
        // manuscript holding the same text come back split differently.
        const QString osis = milah_test::witnessOsis(
            QStringLiteral("w"), QStringLiteral("(horah), to"));

        ParseOptions asManuscript = options(QStringLiteral("m"), QStringLiteral("m.osis"));
        ParseOptions asTranslation = asManuscript;
        asTranslation.role = SourceRole::Translation;

        // Held by name: parseOsis returns by value, and a verse pointer into a
        // temporary would dangle the moment the expression ended.
        const SourceDocument manuscriptDocument = parseOsis(osis, asManuscript);
        const SourceDocument translationDocument = parseOsis(osis, asTranslation);

        const SourceVerse *manuscript =
            manuscriptDocument.verse(QStringLiteral("Matt.1.1"));
        const SourceVerse *translation =
            translationDocument.verse(QStringLiteral("Matt.1.1"));
        QVERIFY(manuscript && translation);

        QCOMPARE(translation->tokens.size(), 2);
        QCOMPARE(translation->tokens.at(0).text, QStringLiteral("(horah),"));
        // "(", "horah", ")", ",", "to" — the marks stand apart.
        QCOMPARE(manuscript->tokens.size(), 5);
    }

    void rejectsDtdAndEntityDeclarations()
    {
        QString unsafe = QString::fromUtf8(milah_test::kSampleOsis);
        unsafe.replace(
            QString::fromUtf8(milah_test::kXmlDeclaration),
            QString::fromUtf8(milah_test::kDoctypeDeclaration));

        bool threw = false;
        try {
            parseOsis(
                unsafe,
                options(QStringLiteral("unsafe"), QStringLiteral("unsafe.osis")));
        } catch (const OsisError &error) {
            threw = true;
            QVERIFY2(
                error.message().contains(QStringLiteral("DTD or ENTITY")),
                qPrintable(error.message()));
        }
        QVERIFY2(threw, "parseOsis accepted a document declaring an external entity");
    }
};

QTEST_MAIN(OsisTest)
#include "osis_test.moc"
