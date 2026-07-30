#include "core/types.h"

#include <QtTest>

using namespace milah;

namespace {

SourceDocument witness(const QString &id, const QString &workId, const QString &name)
{
    SourceDocument document;
    document.id = id;
    document.name = name;
    document.metadata.workId = workId;
    return document;
}

} // namespace

class AcronymTest final : public QObject
{
    Q_OBJECT

private slots:
    void takesTheWorkIdUpToItsFirstUnderscore()
    {
        const QList<SourceDocument> sources = {
            witness("m0", "Sloane237_REV_Hebrew", "REV_Sloane237_hebrew.osis"),
            witness("m1", "MS.Oo.1.16.2_REV_Hebrew", "Cochin_MS_Oo.1.16.2_REV_hebrew.osis"),
            witness("t0", "Ebr530_JOH_Gordon", "JOH_Ebr530_translation.osis"),
        };

        const QHash<QString, QString> acronyms = sourceAcronyms(sources);

        QCOMPARE(acronyms.value("m0"), QStringLiteral("Sloane237"));
        QCOMPARE(acronyms.value("m1"), QStringLiteral("MS.Oo.1.16.2"));
        QCOMPARE(acronyms.value("t0"), QStringLiteral("Ebr530"));
    }

    void witnessesOfOneManuscriptAreToldApart()
    {
        const QList<SourceDocument> sources = {
            witness("m0", "Ebr530_JOH_Hebrew", "JOH_Ebr530_hebrew.osis"),
            witness("m1", "Ebr530_JOH_Hebrew_Consonantal", "JOH_Ebr530_hebrew_consonantal.osis"),
            witness("m2", "Ebr530_JOH_Hebrew_Commented", "JOH_Ebr530_hebrew_commented.osis"),
        };

        const QHash<QString, QString> acronyms = sourceAcronyms(sources);

        QCOMPARE(acronyms.value("m0"), QStringLiteral("Ebr530 Hebrew"));
        QCOMPARE(acronyms.value("m1"), QStringLiteral("Ebr530 Consonantal"));
        QCOMPARE(acronyms.value("m2"), QStringLiteral("Ebr530 Commented"));
    }

    void anUnqualifiedClashFallsBackToNumbering()
    {
        // Same work id twice: nothing in the metadata separates them, so the
        // labels have to be made distinct anyway.
        const QList<SourceDocument> sources = {
            witness("m0", "Ebr530", "one.osis"),
            witness("m1", "Ebr530", "two.osis"),
        };

        const QHash<QString, QString> acronyms = sourceAcronyms(sources);

        QCOMPARE(acronyms.value("m0"), QStringLiteral("Ebr530"));
        QCOMPARE(acronyms.value("m1"), QStringLiteral("Ebr530 2"));
    }

    void aShortShelfmarkStandsInForAMissingWorkId()
    {
        SourceDocument document = witness("m0", QString(), "cochin.osis");
        document.metadata.identifiers.insert(
            QStringLiteral("x-shelfmark"), QStringLiteral("MS.Oo.1.32"));

        QCOMPARE(
            sourceAcronyms({document}).value("m0"), QStringLiteral("MS.Oo.1.32"));
    }

    void aLongShelfmarkIsPassedOverForTheFileName()
    {
        SourceDocument document = witness("m0", QString(), "JOH_Ebr530_hebrew.osis");
        document.metadata.identifiers.insert(
            QStringLiteral("x-shelfmark"),
            QStringLiteral("Biblioteca Apostolica Vaticana, Vat. ebr. 530"));

        QCOMPARE(
            sourceAcronyms({document}).value("m0"), QStringLiteral("JOH_Ebr530_hebrew"));
    }

    void aSourceWithNoMetadataAtAllStillGetsALabel()
    {
        const SourceDocument document = witness("manuscript-3", QString(), QString());

        QCOMPARE(
            sourceAcronyms({document}).value("manuscript-3"),
            QStringLiteral("manuscript-3"));
    }
};

QTEST_MAIN(AcronymTest)
#include "acronym_test.moc"
