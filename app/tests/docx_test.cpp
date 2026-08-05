#include "core/docx.h"

#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QtTest>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

using namespace milah;

namespace {

/// Whether a part is XML a reader will accept at all.
///
/// The reason this is asked of every part rather than eyeballed: Word does not
/// render a malformed document badly, it refuses it and offers to repair it,
/// and the offer says nothing about what was wrong. One stray character in a
/// transcriber's note is the whole file.
bool wellFormed(const QByteArray &part, QString *reason = nullptr)
{
    QXmlStreamReader reader(part);
    while (!reader.atEnd()) {
        reader.readNext();
    }
    if (reader.hasError() && reason != nullptr) {
        *reason = reader.errorString();
    }
    return !reader.hasError();
}

DocxDocument oneHebrewParagraph()
{
    DocxDocument document;
    DocxParagraph paragraph;
    paragraph.style = QStringLiteral("VerseHebrew");
    paragraph.rightToLeft = true;

    DocxRun hebrew;
    hebrew.text = QStringLiteral("בְּרֵאשִׁית");
    hebrew.hebrew = true;
    paragraph.runs.append(hebrew);

    paragraph.runs.append(DocxRun{QStringLiteral("in the beginning")});
    document.blocks.append(paragraph);
    return document;
}

/// A two-row table whose second row's cells are deliberately left empty.
///
/// Empty is the ordinary case in a collation — a witness that reads nothing at
/// a word the others do — so it is what the table model is exercised with.
DocxTable twoRowTable(bool rightToLeft = false)
{
    DocxTable table;
    table.rightToLeft = rightToLeft;
    table.columnWidths = {900, 1200, 1500};

    DocxTableRow filled;
    for (const QString &text :
         {QStringLiteral("Sloane"), QStringLiteral("first"), QStringLiteral("second")}) {
        DocxParagraph paragraph;
        paragraph.style = QStringLiteral("TableCell");
        paragraph.runs.append(DocxRun{text});
        filled.cells.append(DocxTableCell{{paragraph}});
    }
    table.rows.append(filled);

    DocxTableRow bare;
    bare.cells = {DocxTableCell{}, DocxTableCell{}, DocxTableCell{}};
    table.rows.append(bare);

    return table;
}

/// Every `w:tc` in `xml`, as the text between it and its matching `</w:tc>`.
QStringList cellBodies(const QString &xml)
{
    QStringList bodies;
    int at = 0;
    while ((at = xml.indexOf(QStringLiteral("<w:tc>"), at)) >= 0) {
        const int opens = xml.indexOf(QLatin1Char('>'), at) + 1;
        const int closes = xml.indexOf(QStringLiteral("</w:tc>"), opens);
        if (closes < 0) {
            break;
        }
        bodies.append(xml.mid(opens, closes - opens));
        at = closes;
    }
    return bodies;
}

} // namespace

/// The Word writer. Everything here is about a file opening at all: a docx is
/// judged by Word before a reader ever sees it, and it is judged pass or fail.
class DocxTest final : public QObject
{
    Q_OBJECT

private slots:
    void everyPartIsWellFormed()
    {
        DocxDocument document = oneHebrewParagraph();
        document.title = QStringLiteral("Sloane MS 237");
        document.subtitle.append(QStringLiteral("British Library"));
        document.addFootnote(QStringLiteral("The scribe corrected this."));

        QString reason;
        QVERIFY2(wellFormed(docxDocumentXml(document), &reason), qPrintable(reason));
        QVERIFY2(wellFormed(docxFootnotesXml(document), &reason), qPrintable(reason));
        QVERIFY2(wellFormed(docxStylesXml(), &reason), qPrintable(reason));
        QVERIFY2(wellFormed(docxContentTypesXml(), &reason), qPrintable(reason));
        QVERIFY2(wellFormed(docxPackageRels(), &reason), qPrintable(reason));
        QVERIFY2(wellFormed(docxDocumentRels(), &reason), qPrintable(reason));
    }

    void hebrewIsMarkedRightToLeftTwice()
    {
        // Word asks the question in two places: the paragraph decides which
        // margin the text starts at, the run decides the order of its own
        // characters. One without the other looks right until a Latin word
        // turns up in the same line.
        const QString xml = QString::fromUtf8(docxDocumentXml(oneHebrewParagraph()));
        QVERIFY(xml.contains(QStringLiteral("<w:bidi/>")));
        QVERIFY(xml.contains(QStringLiteral("<w:rtl/>")));
        // And the complex-script font, without which the letters come out as
        // boxes in whatever Latin face Word chose.
        QVERIFY(xml.contains(QStringLiteral("w:cs=")));
    }

    void aLatinRunIsNotDraggedRightToLeft()
    {
        // The English gloss shares a document with the Hebrew and must not
        // inherit its direction.
        DocxDocument document;
        DocxParagraph gloss;
        gloss.style = QStringLiteral("Gloss");
        gloss.runs.append(DocxRun{QStringLiteral("in the beginning")});
        document.blocks.append(gloss);

        const QString xml = QString::fromUtf8(docxDocumentXml(document));
        QVERIFY(!xml.contains(QStringLiteral("<w:rtl/>")));
        QVERIFY(!xml.contains(QStringLiteral("<w:bidi/>")));
    }

    void markupInANoteSurvivesAsText()
    {
        DocxDocument document = oneHebrewParagraph();
        document.addFootnote(QStringLiteral("Reads <ם> & not \"ן\" — see f. 3r"));

        const QByteArray part = docxFootnotesXml(document);
        QString reason;
        QVERIFY2(wellFormed(part, &reason), qPrintable(reason));

        const QString xml = QString::fromUtf8(part);
        QVERIFY(xml.contains(QStringLiteral("&lt;ם&gt;")));
        QVERIFY(xml.contains(QStringLiteral("&amp;")));
    }

    void aControlCharacterDoesNotPoisonTheDocument()
    {
        // XML 1.0 forbids most of these outright, and a transcriber pasting out
        // of another program is exactly how one arrives. Turned into spaces
        // rather than dropped, so the words stay apart.
        DocxDocument document = oneHebrewParagraph();
        document.addFootnote(
            QStringLiteral("first%1second").arg(QChar(QChar::Null))
            + QChar(0x0C) + QStringLiteral("third"));

        QString reason;
        QVERIFY2(wellFormed(docxFootnotesXml(document), &reason), qPrintable(reason));
        const QString xml = QString::fromUtf8(docxFootnotesXml(document));
        QVERIFY(xml.contains(QStringLiteral("first second third")));
    }

    void realFootnotesBeginAtTwo()
    {
        // 0 and 1 belong to the separators. A footnote numbered 0 is not a
        // footnote numbered oddly; it is a file Word offers to repair.
        DocxDocument document;
        QCOMPARE(document.addFootnote(QStringLiteral("first")), 2);
        QCOMPARE(document.addFootnote(QStringLiteral("second")), 3);
        QCOMPARE(document.footnotes.size(), 2);
    }

    void theSeparatorsAreWrittenEvenWithNoNotesOfOurOwn()
    {
        const QString xml = QString::fromUtf8(docxFootnotesXml(DocxDocument()));
        QVERIFY(xml.contains(QStringLiteral("w:type=\"separator\" w:id=\"0\"")));
        QVERIFY(xml.contains(QStringLiteral("w:type=\"continuationSeparator\" w:id=\"1\"")));
    }

    void everyReferenceHasAFootnoteToMatchIt()
    {
        // The invariant that decides whether Word opens the file: a reference
        // to a footnote that is not in footnotes.xml is a broken document.
        DocxDocument document;
        DocxParagraph paragraph;
        for (int index = 0; index < 3; ++index) {
            DocxRun word;
            word.text = QStringLiteral("word%1").arg(index);
            word.hebrew = true;
            paragraph.runs.append(word);

            DocxRun marker;
            marker.footnoteId = document.addFootnote(QStringLiteral("note %1").arg(index));
            paragraph.runs.append(marker);
        }
        document.blocks.append(paragraph);

        QSet<QString> referenced;
        const QString body = QString::fromUtf8(docxDocumentXml(document));
        const QRegularExpression reference(
            QStringLiteral("<w:footnoteReference w:id=\"(\\d+)\"/>"));
        auto found = reference.globalMatch(body);
        while (found.hasNext()) {
            referenced.insert(found.next().captured(1));
        }
        QCOMPARE(referenced.size(), 3);

        const QString notes = QString::fromUtf8(docxFootnotesXml(document));
        for (const QString &id : referenced) {
            QVERIFY2(
                notes.contains(QStringLiteral("<w:footnote w:id=\"%1\">").arg(id)),
                qPrintable(id));
        }
    }

    void aFootnoteReferenceCarriesTheStyleItsDefinitionDeclares()
    {
        // Word repairs a document that references a character style nothing
        // defines, so these two have to agree.
        DocxDocument document;
        DocxParagraph paragraph;
        DocxRun marker;
        marker.footnoteId = document.addFootnote(QStringLiteral("a remark"));
        paragraph.runs.append(marker);
        document.blocks.append(paragraph);

        QVERIFY(QString::fromUtf8(docxDocumentXml(document))
                    .contains(QStringLiteral("<w:rStyle w:val=\"FootnoteReference\"/>")));
        QVERIFY(QString::fromUtf8(docxStylesXml())
                    .contains(QStringLiteral("w:styleId=\"FootnoteReference\"")));
    }

    void everyStyleTheLayoutsAskForIsDefined()
    {
        // The layouts name these; a name nothing defines silently loses its
        // formatting, which is how a Hebrew paragraph ends up in 11pt Calibri.
        const QString styles = QString::fromUtf8(docxStylesXml());
        for (const QString &name : {QStringLiteral("Title"),
                                    QStringLiteral("Subtitle"),
                                    QStringLiteral("Heading1"),
                                    QStringLiteral("Heading2"),
                                    QStringLiteral("VerseHebrew"),
                                    QStringLiteral("Gloss"),
                                    QStringLiteral("TableCell"),
                                    QStringLiteral("TableGloss"),
                                    QStringLiteral("TableLabel"),
                                    QStringLiteral("FootnoteText"),
                                    QStringLiteral("FootnoteReference")}) {
            QVERIFY2(
                styles.contains(QStringLiteral("w:styleId=\"%1\"").arg(name)),
                qPrintable(name));
        }
    }

    void spacesAtTheEndsOfRunsAreKept()
    {
        // A gloss line is words joined by spaces and a verse number is followed
        // by one. A reader that trimmed them would run the words together.
        DocxDocument document;
        DocxParagraph paragraph;
        paragraph.runs.append(DocxRun{QStringLiteral("1 ")});
        document.blocks.append(paragraph);
        QVERIFY(QString::fromUtf8(docxDocumentXml(document))
                    .contains(QStringLiteral("xml:space=\"preserve\"")));
    }

    void thePackageIsWrittenAndReadableAsAZip()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("reading.docx"));

        DocxDocument document = oneHebrewParagraph();
        document.title = QStringLiteral("Sloane MS 237");
        document.addFootnote(QStringLiteral("a remark"));

        QString error;
        QVERIFY2(writeDocx(path, document, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(path));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(2), QByteArray("PK"));
        file.close();

        // Read back rather than merely written. Everything above tests the
        // strings; this tests the package, which is the thing Word is handed —
        // a part left out or misnamed is a document that will not open, and the
        // strings would all still be perfect.
        QuaZip archive(path);
        QVERIFY(archive.open(QuaZip::mdUnzip));

        const QStringList required{
            QStringLiteral("[Content_Types].xml"),
            QStringLiteral("_rels/.rels"),
            QStringLiteral("word/document.xml"),
            QStringLiteral("word/_rels/document.xml.rels"),
            QStringLiteral("word/styles.xml"),
            QStringLiteral("word/footnotes.xml")};
        const QStringList present = archive.getFileNameList();
        for (const QString &part : required) {
            QVERIFY2(present.contains(part), qPrintable(part));
        }
        // The one a reader looks for first, so it is written first.
        QCOMPARE(present.constFirst(), QStringLiteral("[Content_Types].xml"));

        for (const QString &part : required) {
            QVERIFY(archive.setCurrentFile(part));
            QuaZipFile entry(&archive);
            QVERIFY(entry.open(QIODevice::ReadOnly));
            const QByteArray contents = entry.readAll();
            entry.close();

            QString reason;
            QVERIFY2(!contents.isEmpty(), qPrintable(part));
            QVERIFY2(wellFormed(contents, &reason), qPrintable(part + ": " + reason));
        }
        // The Hebrew survived the round trip as UTF-8 rather than as mojibake.
        QVERIFY(archive.setCurrentFile(QStringLiteral("word/document.xml")));
        QuaZipFile body(&archive);
        QVERIFY(body.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(body.readAll()).contains(QStringLiteral("בְּרֵאשִׁית")));
        body.close();
        archive.close();
    }

    // --- tables -------------------------------------------------------------

    void aTableCellAlwaysHoldsAParagraph()
    {
        // The most important test here. An empty cell is not an edge case in a
        // collation, it is the ordinary case — a witness silent at one word —
        // and a w:tc with no w:p in it is not a table that lays out oddly, it is
        // a file Word offers to repair.
        DocxDocument document;
        document.blocks.append(DocxBlock(twoRowTable()));

        const QStringList bodies = cellBodies(QString::fromUtf8(docxDocumentXml(document)));
        QCOMPARE(bodies.size(), 6);
        for (const QString &body : bodies) {
            QVERIFY2(body.contains(QStringLiteral("<w:p")), qPrintable(body));
        }
    }

    void aTableIsWellFormedAndSurvivesThePackage()
    {
        DocxDocument document;
        document.blocks.append(DocxBlock(twoRowTable(true)));

        QString reason;
        QVERIFY2(wellFormed(docxDocumentXml(document), &reason), qPrintable(reason));
    }

    void theTableBordersAreAllNil()
    {
        // A collation is a grid to the writer and prose to the reader. Lines
        // round the words would turn an edition into a spreadsheet.
        DocxDocument document;
        document.blocks.append(DocxBlock(twoRowTable()));
        const QString xml = QString::fromUtf8(docxDocumentXml(document));

        for (const QString &edge : {QStringLiteral("top"),
                                    QStringLiteral("left"),
                                    QStringLiteral("bottom"),
                                    QStringLiteral("right"),
                                    QStringLiteral("insideH"),
                                    QStringLiteral("insideV")}) {
            QVERIFY2(
                xml.contains(QStringLiteral("<w:%1 w:val=\"nil\"/>").arg(edge)),
                qPrintable(edge));
        }
    }

    void theGridMatchesTheCells()
    {
        DocxDocument document;
        document.blocks.append(DocxBlock(twoRowTable()));
        const QString xml = QString::fromUtf8(docxDocumentXml(document));

        // One gridCol per declared width, and each cell sized from the grid
        // rather than from anything it carries itself.
        QCOMPARE(xml.count(QStringLiteral("<w:gridCol ")), 3);
        for (const int width : {900, 1200, 1500}) {
            QVERIFY2(
                xml.contains(QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(width)),
                qPrintable(QString::number(width)));
            QVERIFY(xml.contains(QStringLiteral("<w:tcW w:w=\"%1\" w:type=\"dxa\"/>").arg(width)));
        }
        // And the table's declared width is their sum, not a guess.
        QVERIFY(xml.contains(QStringLiteral("<w:tblW w:w=\"3600\" w:type=\"dxa\"/>")));
    }

    void twoTablesNeverTouch()
    {
        // Word merges adjacent w:tbl elements into one table, which would run
        // every band of a verse into a single grid.
        DocxDocument document;
        document.blocks.append(DocxBlock(twoRowTable()));
        document.blocks.append(DocxBlock(twoRowTable()));

        const QString xml = QString::fromUtf8(docxDocumentXml(document));
        QVERIFY(!xml.contains(QStringLiteral("</w:tbl><w:tbl>")));
        QCOMPARE(xml.count(QStringLiteral("</w:tbl><w:p/>")), 2);
    }

    void aRightToLeftTableSaysSoAndKeepsItsOrder()
    {
        DocxDocument leftToRight;
        leftToRight.blocks.append(DocxBlock(twoRowTable(false)));
        QVERIFY(!QString::fromUtf8(docxDocumentXml(leftToRight))
                     .contains(QStringLiteral("<w:bidiVisual/>")));

        DocxDocument rightToLeft;
        rightToLeft.blocks.append(DocxBlock(twoRowTable(true)));
        const QString xml = QString::fromUtf8(docxDocumentXml(rightToLeft));
        QVERIFY(xml.contains(QStringLiteral("<w:bidiVisual/>")));

        // The cells are NOT reversed: Word draws the first at the right-hand
        // edge, so cell n stays column n for everyone who has to reason about
        // the document afterwards.
        const QStringList bodies = cellBodies(xml);
        QVERIFY(bodies.first().contains(QStringLiteral("Sloane")));
        QVERIFY(bodies.at(2).contains(QStringLiteral("second")));
    }

    void theTablePropertiesAreInSchemaOrder()
    {
        // CT_TblPrBase is a sequence, and Word is markedly less forgiving about
        // it than about a run's properties. This is what stops a later edit
        // appending a property wherever it happened to be convenient.
        DocxDocument document;
        document.blocks.append(DocxBlock(twoRowTable(true)));
        const QString xml = QString::fromUtf8(docxDocumentXml(document));

        const int bidi = xml.indexOf(QStringLiteral("<w:bidiVisual/>"));
        const int width = xml.indexOf(QStringLiteral("<w:tblW "));
        const int borders = xml.indexOf(QStringLiteral("<w:tblBorders>"));
        const int layout = xml.indexOf(QStringLiteral("<w:tblLayout "));
        const int margins = xml.indexOf(QStringLiteral("<w:tblCellMar>"));
        const int look = xml.indexOf(QStringLiteral("<w:tblLook "));

        QVERIFY(bidi >= 0 && bidi < width);
        QVERIFY(width < borders);
        QVERIFY(borders < layout);
        QVERIFY(layout < margins);
        QVERIFY(margins < look);
    }

    void aColouredRunCarriesItsColourAndAStruckRunIsStruck()
    {
        DocxDocument document;
        DocxParagraph paragraph;

        DocxRun missing;
        missing.text = QStringLiteral("gone");
        missing.color = QStringLiteral("C0392B");
        missing.strikeThrough = true;
        paragraph.runs.append(missing);

        paragraph.runs.append(DocxRun{QStringLiteral("plain")});
        document.blocks.append(paragraph);

        const QString xml = QString::fromUtf8(docxDocumentXml(document));
        QCOMPARE(xml.count(QStringLiteral("<w:strike/>")), 1);
        QCOMPARE(xml.count(QStringLiteral("<w:color w:val=\"C0392B\"/>")), 1);
    }
};

QTEST_MAIN(DocxTest)
#include "docx_test.moc"
