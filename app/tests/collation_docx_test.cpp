#include "core/collation_docx.h"
#include "core/docx.h"

#include <QtTest>

using namespace milah;

namespace {

/// A manuscript holding one verse, so hasVerse() answers and DocumentRefs into
/// it are valid. The tokens are not what the tables read — the aligned columns
/// are — but a witness with no verse is a witness with no row.
SourceDocument witness(const QString &id, const QString &verseId)
{
    SourceDocument source;
    source.id = id;
    source.name = id + QStringLiteral(".osis");
    source.role = SourceRole::Manuscript;

    SourceVerse verse;
    verse.reference.id = verseId;
    verse.reference.book = QStringLiteral("Rev");
    verse.reference.chapter = 1;
    verse.reference.verse = QStringLiteral("1");
    source.verses.append(verse);
    source.verseIndex.insert(verseId, 0);
    return source;
}

SourceToken token(const QString &text)
{
    SourceToken made;
    made.text = text;
    return made;
}

/// One column, each entry `{sourceId, reading}`. A source left out of the list
/// reads nothing there, which is what a gap in the alignment is.
AlignmentColumn column(const QList<QPair<QString, QString>> &readings)
{
    AlignmentColumn made;
    for (const auto &reading : readings) {
        made.cells.insert(reading.first, token(reading.second));
    }
    return made;
}

/// Every table of a document, in order.
QList<DocxTable> tablesIn(const DocxDocument &document)
{
    QList<DocxTable> tables;
    for (const DocxBlock &block : document.blocks) {
        if (block.isTable()) {
            tables.append(*block.table);
        }
    }
    return tables;
}

/// The plain text of a cell, its runs joined.
QString textOf(const DocxTableCell &cell)
{
    QString out;
    for (const DocxParagraph &paragraph : cell.paragraphs) {
        for (const DocxRun &run : paragraph.runs) {
            out += run.text;
        }
    }
    return out;
}

int paragraphsInStyle(const DocxDocument &document, const QString &style)
{
    int count = 0;
    for (const DocxBlock &block : document.blocks) {
        if (!block.isTable() && block.paragraph.style == style) {
            ++count;
        }
    }
    return count;
}

} // namespace

/// The collation as a Word document. What every test here is really asking is
/// whether the word below is still under the word above.
class CollationDocxTest final : public QObject
{
    Q_OBJECT

private:
    QString m_verseId = QStringLiteral("Rev.1.1");
    SourceDocument m_first;
    SourceDocument m_second;

    /// Two witnesses over three columns, the second reading nothing at the
    /// third and one extra letter at the second.
    CollationExport twoWitnesses()
    {
        m_first = witness(QStringLiteral("a"), m_verseId);
        m_second = witness(QStringLiteral("b"), m_verseId);

        CollationVerse verse;
        verse.referenceId = QStringLiteral("a");
        verse.aligned.reference.id = m_verseId;
        verse.aligned.reference.book = QStringLiteral("Rev");
        verse.aligned.reference.chapter = 1;
        verse.aligned.reference.verse = QStringLiteral("1");
        verse.aligned.columns = {
            column({{QStringLiteral("a"), QString::fromUtf8("אמר")},
                    {QStringLiteral("b"), QString::fromUtf8("אמר")}}),
            column({{QStringLiteral("a"), QString::fromUtf8("אמרת")},
                    {QStringLiteral("b"), QString::fromUtf8("אמרתי")}}),
            column({{QStringLiteral("a"), QString::fromUtf8("שלום")}}),
        };
        verse.draft.columns = {
            ConsensusColumn{QString::fromUtf8("אמר"), QStringLiteral("a"), false},
            ConsensusColumn{QString::fromUtf8("אמרת"), QStringLiteral("a"), false},
            ConsensusColumn{QString::fromUtf8("שלום"), QStringLiteral("a"), false},
        };
        verse.interlinear = {QStringLiteral("said"), QStringLiteral("you-said"), QString()};

        CollationExport collation;
        collation.title = QStringLiteral("Revelation");
        collation.manuscripts = {&m_first, &m_second};
        collation.acronyms.insert(QStringLiteral("a"), QStringLiteral("A"));
        collation.acronyms.insert(QStringLiteral("b"), QStringLiteral("B"));
        collation.rightToLeft = true;
        collation.verses.append(verse);
        return collation;
    }

private slots:
    void aColumnIsAColumn()
    {
        // The one that matters most. If the nth cell of every row is not the
        // nth aligned column, the document is not useless — it is confidently
        // wrong, and a reader has no way to tell.
        const CollationExport collation = twoWitnesses();
        const QList<DocxTable> tables = tablesIn(collationWordDocument(collation));
        QCOMPARE(tables.size(), 1);

        for (const DocxTableRow &row : tables.first().rows) {
            // One label cell plus one per aligned column, every row alike —
            // including the witness that reads nothing at the third.
            QCOMPARE(row.cells.size(), 4);
        }
        QCOMPARE(textOf(tables.first().rows.at(0).cells.at(3)), QString::fromUtf8("שלום"));
        QVERIFY(textOf(tables.first().rows.at(1).cells.at(3)).isEmpty());
    }

    void everyCellHasAParagraph()
    {
        // The model-level twin of the docx-level test: a cell with no paragraph
        // is a file Word refuses.
        for (const DocxTable &table : tablesIn(collationWordDocument(twoWitnesses()))) {
            for (const DocxTableRow &row : table.rows) {
                for (const DocxTableCell &cell : row.cells) {
                    QVERIFY(!cell.paragraphs.isEmpty());
                }
            }
        }
    }

    void theWitnessesStandAboveTheEditionAndTheInterlinear()
    {
        const DocxTable table = tablesIn(collationWordDocument(twoWitnesses())).first();
        QCOMPARE(table.rows.size(), 4);
        QCOMPARE(textOf(table.rows.at(0).cells.first()), QStringLiteral("A"));
        QCOMPARE(textOf(table.rows.at(1).cells.first()), QStringLiteral("B"));
        QCOMPARE(textOf(table.rows.at(2).cells.first()), QStringLiteral("Combined"));
        QCOMPARE(textOf(table.rows.at(3).cells.first()), QStringLiteral("Interlinear"));
    }

    void theVerseIsReadAgainstTheManuscriptItNames()
    {
        CollationExport collation = twoWitnesses();
        collation.verses[0].referenceId = QStringLiteral("b");

        const DocxTable table = tablesIn(collationWordDocument(collation)).first();
        QCOMPARE(textOf(table.rows.at(0).cells.first()), QStringLiteral("B"));
    }

    void theReferenceIsStruckWhereAnotherWitnessDiffers()
    {
        // End to end: the rule in core/reading_marks reaches the page.
        const DocxTable table = tablesIn(collationWordDocument(twoWitnesses())).first();

        // Column three: only the reference reads it, so all of it is struck.
        const DocxTableCell &alone = table.rows.at(0).cells.at(3);
        QVERIFY(!alone.paragraphs.first().runs.isEmpty());
        for (const DocxRun &run : alone.paragraphs.first().runs) {
            QVERIFY(run.strikeThrough);
            QCOMPARE(run.color, QStringLiteral("C0392B"));
        }

        // Column two: the other witness reads one letter more, marked and not
        // struck, because it is an addition rather than a loss.
        const DocxTableCell &variant = table.rows.at(1).cells.at(2);
        bool sawAdded = false;
        for (const DocxRun &run : variant.paragraphs.first().runs) {
            QVERIFY(!run.strikeThrough);
            if (!run.color.isEmpty()) {
                QCOMPARE(run.color, QStringLiteral("2E8B57"));
                sawAdded = true;
            }
        }
        QVERIFY(sawAdded);
    }

    void aWitnessSilentForTheVerseGetsNoRowAndMarksNothing()
    {
        // A manuscript that does not reach the verse at all is absence, not
        // silence. Counting it would strike every word of the reference.
        CollationExport collation = twoWitnesses();
        SourceDocument absent = witness(QStringLiteral("c"), QStringLiteral("Rev.9.9"));
        collation.manuscripts.append(&absent);

        const DocxTable table = tablesIn(collationWordDocument(collation)).first();
        QCOMPARE(table.rows.size(), 4);

        // The first column, which both present witnesses agree on, stays plain.
        for (const DocxRun &run : table.rows.at(0).cells.at(1).paragraphs.first().runs) {
            QVERIFY(run.color.isEmpty());
            QVERIFY(!run.strikeThrough);
        }
    }

    void aManuscriptNoteBecomesAFootnoteOnItsOwnWitness()
    {
        CollationExport collation = twoWitnesses();
        SourceNote note;
        note.number = QStringLiteral("3");
        note.text = QStringLiteral("Blotted in the manuscript.");
        collation.verses[0].aligned.columns[0].cells[QStringLiteral("b")].notes.append(note);

        const DocxDocument document = collationWordDocument(collation);
        QCOMPARE(document.footnotes.size(), 1);
        // The acronym is in the note, so whose remark it is survives the page
        // break that separates the marker from the text.
        QVERIFY(document.footnotes.first().text.startsWith(QStringLiteral("B 3.")));

        // And the marker sits in that witness's own cell, not the reference's.
        const DocxTable table = tablesIn(document).first();
        const auto hasMarker = [](const DocxTableCell &cell) {
            for (const DocxParagraph &paragraph : cell.paragraphs) {
                for (const DocxRun &run : paragraph.runs) {
                    if (run.footnoteId > 0) {
                        return true;
                    }
                }
            }
            return false;
        };
        QVERIFY(hasMarker(table.rows.at(1).cells.at(1)));
        QVERIFY(!hasMarker(table.rows.at(0).cells.at(1)));
    }

    void anEditorNoteBecomesAFootnoteOnTheCombinedWord()
    {
        CollationExport collation = twoWitnesses();
        collation.verses[0].editorNotes.insert(1, QStringLiteral("Read with the Cochin."));

        const DocxDocument document = collationWordDocument(collation);
        QCOMPARE(document.footnotes.size(), 1);

        const DocxTable table = tablesIn(document).first();
        const DocxTableCell &combined = table.rows.at(2).cells.at(2);
        bool marked = false;
        for (const DocxRun &run : combined.paragraphs.first().runs) {
            marked = marked || run.footnoteId > 0;
        }
        QVERIFY(marked);
    }

    void everyFootnoteReferencedIsDefined()
    {
        CollationExport collation = twoWitnesses();
        SourceNote note;
        note.text = QStringLiteral("A remark.");
        collation.verses[0].aligned.columns[0].cells[QStringLiteral("a")].notes.append(note);
        collation.verses[0].editorNotes.insert(2, QStringLiteral("Another."));

        const DocxDocument document = collationWordDocument(collation);
        QSet<int> defined;
        for (const DocxFootnote &footnote : document.footnotes) {
            QVERIFY(footnote.id >= FirstFootnoteId);
            QVERIFY(!defined.contains(footnote.id));
            defined.insert(footnote.id);
        }

        int referenced = 0;
        for (const DocxTable &table : tablesIn(document)) {
            for (const DocxTableRow &row : table.rows) {
                for (const DocxTableCell &cell : row.cells) {
                    for (const DocxParagraph &paragraph : cell.paragraphs) {
                        for (const DocxRun &run : paragraph.runs) {
                            if (run.footnoteId > 0) {
                                QVERIFY(defined.contains(run.footnoteId));
                                ++referenced;
                            }
                        }
                    }
                }
            }
        }
        QCOMPARE(referenced, document.footnotes.size());
    }

    void aWideVerseIsCutIntoBands()
    {
        m_first = witness(QStringLiteral("a"), m_verseId);

        CollationVerse verse;
        verse.referenceId = QStringLiteral("a");
        verse.aligned.reference.id = m_verseId;
        verse.aligned.reference.book = QStringLiteral("Rev");
        verse.aligned.reference.chapter = 1;
        verse.aligned.reference.verse = QStringLiteral("1");
        for (int index = 0; index < 40; ++index) {
            verse.aligned.columns.append(
                column({{QStringLiteral("a"), QString::fromUtf8("מלאכה")}}));
            verse.interlinear.append(QStringLiteral("work"));
        }

        CollationExport collation;
        collation.manuscripts = {&m_first};
        collation.rightToLeft = true;
        collation.verses.append(verse);

        const DocxDocument document = collationWordDocument(collation);
        const QList<DocxTable> tables = tablesIn(document);
        QVERIFY(tables.size() > 1);

        // The bands concatenate back to the verse, in order, each column once.
        int seen = 0;
        for (const DocxTable &table : tables) {
            // One label column plus the band's own.
            QCOMPARE(table.columnWidths.size(), table.rows.first().cells.size());
            seen += table.rows.first().cells.size() - 1;

            int width = 0;
            for (const int each : table.columnWidths) {
                width += each;
            }
            QVERIFY2(width <= TextWidthTwips, qPrintable(QString::number(width)));
        }
        QCOMPARE(seen, 40);

        // One heading for the verse however many bands it takes, and every band
        // led by its row label so a reader landing on the third knows whose
        // line they are on.
        QCOMPARE(paragraphsInStyle(document, QStringLiteral("Heading2")), 1);
        for (const DocxTable &table : tables) {
            QCOMPARE(textOf(table.rows.first().cells.first()), QStringLiteral("a"));
        }
    }

    void theGlossKeepsItsPlaceUnderAnEmptyWord()
    {
        // The edition reads nothing at the third column but the interlinear
        // must still have a cell there, or the whole gloss line shifts by one.
        CollationExport collation = twoWitnesses();
        collation.verses[0].draft.columns[2].text.reset();
        collation.verses[0].interlinear[2] = QStringLiteral("peace");

        const DocxTable table = tablesIn(collationWordDocument(collation)).first();
        QVERIFY(textOf(table.rows.at(2).cells.at(3)).isEmpty());
        QCOMPARE(textOf(table.rows.at(3).cells.at(3)), QStringLiteral("peace"));
    }

    void theInterlinearReadsLeftToRightInARightToLeftTable()
    {
        const DocxTable table = tablesIn(collationWordDocument(twoWitnesses())).first();
        QVERIFY(table.rightToLeft);
        // The gloss is Latin even where it sits under Hebrew.
        QVERIFY(!table.rows.at(3).cells.at(1).paragraphs.first().rightToLeft);
        QVERIFY(table.rows.at(0).cells.at(1).paragraphs.first().rightToLeft);
    }

    void anEmptyBookMakesAnEmptyDocument()
    {
        // What the controller's guard tests before opening a save dialog.
        QVERIFY(collationWordDocument(CollationExport{}).blocks.isEmpty());
    }

    void aVerseNoWitnessReachesIsNotGivenATable()
    {
        CollationExport collation = twoWitnesses();
        SourceDocument elsewhere = witness(QStringLiteral("z"), QStringLiteral("Rev.9.9"));
        collation.manuscripts = {&elsewhere};

        // A table of empty rows would say there was something to collate here.
        QVERIFY(tablesIn(collationWordDocument(collation)).isEmpty());
    }
};

QTEST_MAIN(CollationDocxTest)
#include "collation_docx_test.moc"
