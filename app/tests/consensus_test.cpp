#include "core/alignment.h"
#include "core/osis.h"
#include "test_data.h"

#include <QtTest>

using namespace milah;

namespace {

SourceDocument witness(const QString &id, const QString &text)
{
    const QString osis = milah_test::witnessOsis(id, text);

    ParseOptions options;
    options.id = id;
    options.name = id + QStringLiteral(".osis");
    options.role = SourceRole::Manuscript;
    return parseOsis(osis, options);
}

DocumentRefs refs(const QList<SourceDocument> &documents)
{
    DocumentRefs result;
    result.reserve(documents.size());
    for (const SourceDocument &document : documents) {
        result.append(&document);
    }
    return result;
}

} // namespace

class ConsensusTest final : public QObject
{
    Q_OBJECT

private slots:
    void usesAStrictMajority()
    {
        const QList<SourceDocument> majority = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר")),
            witness(QStringLiteral("b"), QString::fromUtf8("סֵפֶר")),
            witness(QStringLiteral("c"), QString::fromUtf8("דבר")),
        };
        const DocumentRefs sources = refs(majority);

        const CombinedDraft draft = generateCombined(
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("a")),
            sources,
            QStringLiteral("a"));

        QVERIFY(!draft.columns.isEmpty());
        QCOMPARE(draft.columns.at(0).text.value_or(QString()), QString::fromUtf8("ספר"));
        QCOMPARE(draft.columns.at(0).needsReview, false);
    }

    void fallsBackToThePriorityWitnessForATie()
    {
        const QList<SourceDocument> tie = {
            witness(QStringLiteral("a"), QString::fromUtf8("ספר")),
            witness(QStringLiteral("b"), QString::fromUtf8("דבר")),
        };
        const DocumentRefs sources = refs(tie);

        const CombinedDraft draft = generateCombined(
            alignVerse(QStringLiteral("Matt.1.1"), sources, QStringLiteral("b")),
            sources,
            QStringLiteral("b"));

        QVERIFY(!draft.columns.isEmpty());
        QCOMPARE(draft.columns.at(0).text.value_or(QString()), QString::fromUtf8("דבר"));
        QCOMPARE(draft.columns.at(0).needsReview, true);
    }
};

QTEST_MAIN(ConsensusTest)
#include "consensus_test.moc"
