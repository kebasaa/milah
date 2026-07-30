#include "core/osis.h"
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
