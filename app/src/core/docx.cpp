#include "core/docx.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>
#include <quazip/quazipnewinfo.h>

namespace milah {
namespace {

/// The one namespace declaration every part of the document shares.
constexpr char kWordNs[] =
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";

/// What Hebrew is drawn in. A complex-script font is chosen separately from the
/// Latin one, and this is the face Windows ships that has both.
constexpr char kHebrewFont[] = "Times New Roman";

/// Text as it may appear inside an element.
///
/// Not serialize.cpp's escaper, which is file-static and, more to the point,
/// has never had to care about the second half of this: XML 1.0 forbids most
/// control characters outright, and a single one of them in a transcriber's
/// note is not a document that renders oddly — it is a document Word refuses to
/// open at all. They are turned into spaces rather than dropped, so a note that
/// had line breaks in it still has its words apart.
QString escaped(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar character : text) {
        switch (character.unicode()) {
        case u'&':
            out += QStringLiteral("&amp;");
            continue;
        case u'<':
            out += QStringLiteral("&lt;");
            continue;
        case u'>':
            out += QStringLiteral("&gt;");
            continue;
        case u'"':
            out += QStringLiteral("&quot;");
            continue;
        default:
            break;
        }
        const char16_t code = character.unicode();
        // Everything below a space, and the delete character, except that a tab
        // is harmless. XML 1.0 permits only tab, newline and carriage return
        // from that range, and a run cannot carry the latter two anyway.
        if ((code < 0x20 && code != 0x09) || code == 0x7F) {
            out += QLatin1Char(' ');
            continue;
        }
        out += character;
    }
    return out;
}

QString runXml(const DocxRun &run)
{
    QString properties;
    if (run.bold) {
        properties += QStringLiteral("<w:b/><w:bCs/>");
    }
    if (run.italic) {
        properties += QStringLiteral("<w:i/><w:iCs/>");
    }
    // strike then color, in that order and here: CT_RPr is a schema sequence,
    // and while Word forgives a run whose properties are out of it, there is no
    // reason to spend that forgiveness.
    if (run.strikeThrough) {
        properties += QStringLiteral("<w:strike/>");
    }
    if (!run.color.isEmpty()) {
        properties += QStringLiteral("<w:color w:val=\"%1\"/>").arg(escaped(run.color));
    }
    if (run.superscript) {
        properties += QStringLiteral("<w:vertAlign w:val=\"superscript\"/>");
    }
    if (run.hebrew) {
        // Both, and in this order. w:rtl orders the characters; the font entry
        // decides what they are drawn in, and without a complex-script face
        // named here Word falls back to a Latin one and shows boxes.
        properties += QStringLiteral("<w:rFonts w:cs=\"%1\"/><w:rtl/>")
                          .arg(QLatin1String(kHebrewFont));
    }
    if (run.footnoteId > 0) {
        properties += QStringLiteral("<w:rStyle w:val=\"FootnoteReference\"/>");
    }

    const QString wrapped =
        properties.isEmpty() ? QString() : QStringLiteral("<w:rPr>%1</w:rPr>").arg(properties);

    if (run.footnoteId > 0) {
        return QStringLiteral("<w:r>%1<w:footnoteReference w:id=\"%2\"/></w:r>")
            .arg(wrapped)
            .arg(run.footnoteId);
    }
    // xml:space, always: a gloss line is words joined by spaces, and a reader
    // that trims them would run them together.
    return QStringLiteral("<w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r>")
        .arg(wrapped, escaped(run.text));
}

QString paragraphXml(const DocxParagraph &paragraph)
{
    QString properties;
    if (!paragraph.style.isEmpty()) {
        properties +=
            QStringLiteral("<w:pStyle w:val=\"%1\"/>").arg(escaped(paragraph.style));
    }
    if (paragraph.rightToLeft) {
        // The paragraph's own direction, which decides which margin it starts
        // at. Separate from the runs': a Hebrew paragraph needs both, and one
        // without the other looks right until a Latin word turns up in it.
        properties += QStringLiteral("<w:bidi/>");
    }

    QString body;
    if (!properties.isEmpty()) {
        body += QStringLiteral("<w:pPr>%1</w:pPr>").arg(properties);
    }
    for (const DocxRun &run : paragraph.runs) {
        body += runXml(run);
    }
    return QStringLiteral("<w:p>%1</w:p>").arg(body);
}

/// Borders declared away, all six of them.
///
/// A collation is a grid to the writer and prose to the reader: the columns are
/// what makes the words line up, and lines drawn round them would turn an
/// edition into a spreadsheet.
QString tableBordersXml()
{
    QString borders;
    for (const QLatin1String &edge : {QLatin1String("top"),
                                      QLatin1String("left"),
                                      QLatin1String("bottom"),
                                      QLatin1String("right"),
                                      QLatin1String("insideH"),
                                      QLatin1String("insideV")}) {
        borders += QStringLiteral("<w:%1 w:val=\"nil\"/>").arg(edge);
    }
    return QStringLiteral("<w:tblBorders>%1</w:tblBorders>").arg(borders);
}

QString tableXml(const DocxTable &table)
{
    // CT_TblPrBase is a schema *sequence*, and Word is markedly less forgiving
    // about it than about a run's properties: bidiVisual, tblW, tblBorders,
    // tblLayout, tblCellMar, tblLook. Appending a property in the wrong place
    // here is how a file stops opening.
    QString properties;
    if (table.rightToLeft) {
        properties += QStringLiteral("<w:bidiVisual/>");
    }
    int total = 0;
    for (const int width : table.columnWidths) {
        total += width;
    }
    properties += QStringLiteral("<w:tblW w:w=\"%1\" w:type=\"dxa\"/>").arg(total);
    properties += tableBordersXml();
    properties += QStringLiteral("<w:tblLayout w:type=\"fixed\"/>");
    properties += QStringLiteral(
        "<w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/>"
        "<w:left w:w=\"57\" w:type=\"dxa\"/>"
        "<w:bottom w:w=\"0\" w:type=\"dxa\"/>"
        "<w:right w:w=\"57\" w:type=\"dxa\"/></w:tblCellMar>");
    properties += QStringLiteral(
        "<w:tblLook w:val=\"0000\" w:firstRow=\"0\" w:lastRow=\"0\""
        " w:firstColumn=\"0\" w:lastColumn=\"0\" w:noHBand=\"0\" w:noVBand=\"0\"/>");

    QString grid;
    for (const int width : table.columnWidths) {
        grid += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(width);
    }

    QString rows;
    for (const DocxTableRow &row : table.rows) {
        QString cells;
        for (int index = 0; index < row.cells.size(); ++index) {
            const DocxTableCell &cell = row.cells.at(index);
            // Derived from the grid rather than carried on the cell, so the two
            // cannot drift; a short row is given the last column's width rather
            // than none.
            const int width = table.columnWidths.value(
                index, table.columnWidths.isEmpty() ? 0 : table.columnWidths.constLast());

            QString content;
            for (const DocxParagraph &paragraph : cell.paragraphs) {
                content += paragraphXml(paragraph);
            }
            if (content.isEmpty()) {
                // The ordinary case, not an edge one: a witness silent at this
                // word. An empty w:tc is a file Word offers to repair.
                content = QStringLiteral("<w:p/>");
            }

            cells += QStringLiteral(
                         "<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>"
                         "<w:vAlign w:val=\"bottom\"/></w:tcPr>%2</w:tc>")
                         .arg(QString::number(width), content);
        }
        // A row of one verse's words is a unit; broken across a page it stops
        // being an alignment.
        rows += QStringLiteral("<w:tr><w:trPr><w:cantSplit/></w:trPr>%1</w:tr>").arg(cells);
    }

    // The trailing paragraph is not decoration. Two w:tbl elements that touch
    // are merged by Word into one table, which would run every band of a verse
    // into a single grid; and a table as the last thing in the body leaves
    // nowhere to put the cursor. Emitted here so no caller has to remember.
    return QStringLiteral("<w:tbl><w:tblPr>%1</w:tblPr><w:tblGrid>%2</w:tblGrid>%3</w:tbl><w:p/>")
        .arg(properties, grid, rows);
}

/// One paragraph of plain text in a named style, for titles and subtitles.
QString plainParagraph(const QString &text, const QString &style)
{
    DocxParagraph paragraph;
    paragraph.style = style;
    paragraph.runs.append(DocxRun{text});
    return paragraphXml(paragraph);
}

/// A style definition. `name` is the style's name for a reader, which Word
/// requires each one to carry.
QString styleXml(
    const QString &type,
    const QString &id,
    const QString &name,
    const QString &paragraphProperties,
    const QString &runProperties,
    bool isDefault = false)
{
    QString out = QStringLiteral("<w:style w:type=\"%1\"%2 w:styleId=\"%3\">"
                                 "<w:name w:val=\"%4\"/>")
                      .arg(
                          type,
                          isDefault ? QStringLiteral(" w:default=\"1\"") : QString(),
                          id,
                          name);
    if (id != QLatin1String("Normal") && type == QLatin1String("paragraph")) {
        out += QStringLiteral("<w:basedOn w:val=\"Normal\"/>");
    }
    if (!paragraphProperties.isEmpty()) {
        out += QStringLiteral("<w:pPr>%1</w:pPr>").arg(paragraphProperties);
    }
    if (!runProperties.isEmpty()) {
        out += QStringLiteral("<w:rPr>%1</w:rPr>").arg(runProperties);
    }
    return out + QStringLiteral("</w:style>");
}

QByteArray utf8(const QString &text)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n")
        .append(text)
        .toUtf8();
}

bool writeEntry(
    QuaZip &archive,
    const QString &entryName,
    const QByteArray &contents,
    QString *errorMessage)
{
    QuaZipFile entry(&archive);
    if (!entry.open(QIODevice::WriteOnly, QuaZipNewInfo(entryName))) {
        *errorMessage = QStringLiteral("Could not create %1 in the document.").arg(entryName);
        return false;
    }
    if (entry.write(contents) != contents.size()) {
        *errorMessage = QStringLiteral("Could not write %1 in the document.").arg(entryName);
        entry.close();
        return false;
    }
    entry.close();
    if (entry.getZipError() != 0) {
        *errorMessage = QStringLiteral("Could not finish %1 in the document.").arg(entryName);
        return false;
    }
    return true;
}

} // namespace

int DocxDocument::addFootnote(const QString &text)
{
    const int id = FirstFootnoteId + static_cast<int>(footnotes.size());
    footnotes.append(DocxFootnote{id, text});
    return id;
}

QByteArray docxDocumentXml(const DocxDocument &document)
{
    QString body;
    if (!document.title.isEmpty()) {
        body += plainParagraph(document.title, QStringLiteral("Title"));
    }
    for (const QString &line : document.subtitle) {
        body += plainParagraph(line, QStringLiteral("Subtitle"));
    }
    for (const DocxBlock &block : document.blocks) {
        body += block.isTable() ? tableXml(*block.table) : paragraphXml(block.paragraph);
    }

    // A4 with even margins. A section is required — a body without one opens,
    // but Word supplies its own defaults and the page comes out US Letter.
    body += QStringLiteral(
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
        "<w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\""
        " w:header=\"709\" w:footer=\"709\" w:gutter=\"0\"/></w:sectPr>");

    return utf8(QStringLiteral("<w:document %1><w:body>%2</w:body></w:document>")
                    .arg(QLatin1String(kWordNs), body));
}

QByteArray docxFootnotesXml(const DocxDocument &document)
{
    // The two rules Word draws above a page's notes. Written whether or not the
    // document has notes of its own, because the part is referenced either way
    // and one that lacks them is a file Word offers to repair.
    QString body = QStringLiteral(
        "<w:footnote w:type=\"separator\" w:id=\"0\"><w:p><w:pPr>"
        "<w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>"
        "<w:r><w:separator/></w:r></w:p></w:footnote>"
        "<w:footnote w:type=\"continuationSeparator\" w:id=\"1\"><w:p><w:pPr>"
        "<w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>"
        "<w:r><w:continuationSeparator/></w:r></w:p></w:footnote>");

    for (const DocxFootnote &note : document.footnotes) {
        body += QStringLiteral(
                    "<w:footnote w:id=\"%1\"><w:p>"
                    "<w:pPr><w:pStyle w:val=\"FootnoteText\"/></w:pPr>"
                    // footnoteRef is what prints the number at the foot of the
                    // page, and what makes Word renumber the lot when one is
                    // added in the middle.
                    "<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr>"
                    "<w:footnoteRef/></w:r>"
                    "<w:r><w:t xml:space=\"preserve\"> %2</w:t></w:r>"
                    "</w:p></w:footnote>")
                    .arg(QString::number(note.id), escaped(note.text));
    }

    return utf8(QStringLiteral("<w:footnotes %1>%2</w:footnotes>")
                    .arg(QLatin1String(kWordNs), body));
}

QByteArray docxStylesXml()
{
    QString styles;

    // The document's defaults, which every style below builds on.
    styles += QStringLiteral(
                  "<w:docDefaults><w:rPrDefault><w:rPr>"
                  "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:cs=\"%1\"/>"
                  "<w:sz w:val=\"22\"/><w:szCs w:val=\"24\"/>"
                  "</w:rPr></w:rPrDefault></w:docDefaults>")
                  .arg(QLatin1String(kHebrewFont));

    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("Normal"),
        QStringLiteral("Normal"),
        QStringLiteral("<w:spacing w:after=\"120\"/>"),
        QString(),
        true);
    styles += styleXml(
        QStringLiteral("character"),
        QStringLiteral("DefaultParagraphFont"),
        QStringLiteral("Default Paragraph Font"),
        QString(),
        QString(),
        true);

    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("Title"),
        QStringLiteral("Title"),
        QStringLiteral("<w:spacing w:after=\"60\"/>"),
        QStringLiteral("<w:b/><w:bCs/><w:sz w:val=\"40\"/><w:szCs w:val=\"40\"/>"));
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("Subtitle"),
        QStringLiteral("Subtitle"),
        QStringLiteral("<w:spacing w:after=\"40\"/>"),
        QStringLiteral("<w:i/><w:iCs/><w:color w:val=\"555555\"/>"));
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("Heading1"),
        QStringLiteral("heading 1"),
        QStringLiteral("<w:spacing w:before=\"320\" w:after=\"120\"/>"
                       "<w:outlineLvl w:val=\"0\"/>"),
        QStringLiteral("<w:b/><w:bCs/><w:sz w:val=\"30\"/><w:szCs w:val=\"30\"/>"));
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("Heading2"),
        QStringLiteral("heading 2"),
        QStringLiteral("<w:spacing w:before=\"200\" w:after=\"60\"/>"
                       "<w:outlineLvl w:val=\"1\"/>"),
        QStringLiteral("<w:b/><w:bCs/><w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/>"));

    // Hebrew wants to be larger than the Latin around it to be read at all, and
    // wants the line opened up, because pointing sits below the letters.
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("VerseHebrew"),
        QStringLiteral("Verse Hebrew"),
        QStringLiteral("<w:bidi/><w:spacing w:after=\"60\" w:line=\"360\""
                       " w:lineRule=\"auto\"/>"),
        QStringLiteral("<w:sz w:val=\"30\"/><w:szCs w:val=\"30\"/>"));
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("Gloss"),
        QStringLiteral("Gloss"),
        QStringLiteral("<w:spacing w:after=\"200\"/><w:ind w:left=\"284\"/>"),
        QStringLiteral("<w:color w:val=\"333333\"/>"));

    // The three inside a collation's cells. All of them close the paragraph up:
    // Normal leaves 120 twips after every paragraph, which inside a table cell
    // is a sixth of an inch of nothing under every word, repeated down every row
    // of every verse.
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("TableCell"),
        QStringLiteral("Table Cell"),
        QStringLiteral("<w:spacing w:after=\"0\"/><w:jc w:val=\"center\"/>"),
        QStringLiteral("<w:sz w:val=\"28\"/><w:szCs w:val=\"28\"/>"));
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("TableGloss"),
        QStringLiteral("Table Gloss"),
        QStringLiteral("<w:spacing w:after=\"0\"/><w:jc w:val=\"center\"/>"),
        QStringLiteral("<w:sz w:val=\"16\"/><w:szCs w:val=\"16\"/>"
                       "<w:color w:val=\"333333\"/>"));
    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("TableLabel"),
        QStringLiteral("Table Label"),
        QStringLiteral("<w:spacing w:after=\"0\"/><w:jc w:val=\"center\"/>"),
        QStringLiteral("<w:i/><w:iCs/><w:sz w:val=\"16\"/><w:szCs w:val=\"16\"/>"
                       "<w:color w:val=\"555555\"/>"));

    styles += styleXml(
        QStringLiteral("paragraph"),
        QStringLiteral("FootnoteText"),
        QStringLiteral("footnote text"),
        QStringLiteral("<w:spacing w:after=\"0\"/>"),
        QStringLiteral("<w:sz w:val=\"18\"/><w:szCs w:val=\"18\"/>"));
    styles += styleXml(
        QStringLiteral("character"),
        QStringLiteral("FootnoteReference"),
        QStringLiteral("footnote reference"),
        QString(),
        QStringLiteral("<w:vertAlign w:val=\"superscript\"/>"));

    return utf8(QStringLiteral("<w:styles %1>%2</w:styles>")
                    .arg(QLatin1String(kWordNs), styles));
}

QByteArray docxContentTypesXml()
{
    return utf8(QStringLiteral(
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\""
        " ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/footnotes.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>"
        "</Types>"));
}

QByteArray docxPackageRels()
{
    return utf8(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas."
        "openxmlformats.org/officeDocument/2006/relationships/officeDocument\""
        " Target=\"word/document.xml\"/></Relationships>"));
}

QByteArray docxDocumentRels()
{
    return utf8(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/"
        "officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/"
        "officeDocument/2006/relationships/footnotes\" Target=\"footnotes.xml\"/>"
        "</Relationships>"));
}

bool writeDocx(const QString &path, const DocxDocument &document, QString *errorMessage)
{
    if (errorMessage == nullptr) {
        return false;
    }
    errorMessage->clear();

    // Built beside the destination and moved over it, the same way a project is
    // saved: a document interrupted halfway through leaves nothing that looks
    // like a Word file.
    QTemporaryFile temporary(
        QFileInfo(path).absolutePath() + QStringLiteral("/milah-XXXXXX.tmp"));
    if (!temporary.open()) {
        *errorMessage = QStringLiteral("Could not create a temporary file beside %1.")
                            .arg(path);
        return false;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();

    QuaZip archive(temporaryPath);
    if (!archive.open(QuaZip::mdCreate)) {
        *errorMessage = QStringLiteral("Could not create the document archive.");
        return false;
    }

    // [Content_Types].xml first: it is what a reader opens the package with, and
    // some of them look for it at the front rather than in the index.
    const QList<QPair<QString, QByteArray>> parts{
        {QStringLiteral("[Content_Types].xml"), docxContentTypesXml()},
        {QStringLiteral("_rels/.rels"), docxPackageRels()},
        {QStringLiteral("word/document.xml"), docxDocumentXml(document)},
        {QStringLiteral("word/_rels/document.xml.rels"), docxDocumentRels()},
        {QStringLiteral("word/styles.xml"), docxStylesXml()},
        {QStringLiteral("word/footnotes.xml"), docxFootnotesXml(document)},
    };
    for (const auto &part : parts) {
        if (!writeEntry(archive, part.first, part.second, errorMessage)) {
            archive.close();
            return false;
        }
    }

    archive.close();
    if (archive.getZipError() != 0) {
        *errorMessage = QStringLiteral("Could not finish the document archive.");
        return false;
    }

    QFile completed(temporaryPath);
    if (!completed.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("Could not read the completed document.");
        return false;
    }
    const QByteArray bytes = completed.readAll();
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size()
        || !destination.commit()) {
        *errorMessage = QStringLiteral("Could not save the document to %1.").arg(path);
        return false;
    }
    return true;
}

} // namespace milah
