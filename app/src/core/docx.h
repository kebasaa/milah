#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace milah {

/// A stretch of text within a paragraph, and how it is set.
struct DocxRun
{
    QString text;
    bool bold = false;
    bool italic = false;
    /// For a verse number standing in the flow of the text.
    bool superscript = false;
    /// Hebrew. Word needs telling twice: `w:rtl` puts the run's own characters
    /// in order, and the complex-script font entry decides what they are drawn
    /// in — without which Word picks a Latin face that has no Hebrew in it.
    bool hebrew = false;
    /// Six hex digits without a leading '#' — "C0392B". Empty leaves the run
    /// whatever colour its style says, which is what almost every run wants.
    QString color;
    /// Ruled through. What a reading no other witness has looks like.
    bool strikeThrough = false;
    /// Greater than zero makes this run a reference to that footnote rather
    /// than text of its own. See DocxDocument::footnotes for the numbering.
    int footnoteId = 0;
};

struct DocxParagraph
{
    /// A style named in docxStylesXml(). Empty is the default paragraph.
    QString style;
    /// The paragraph's own direction, which is not the same question as the
    /// direction of the runs inside it: this decides which margin the text
    /// starts at and where its punctuation lands. A Hebrew paragraph needs
    /// both this and `hebrew` on its runs.
    bool rightToLeft = false;
    QList<DocxRun> runs;
};

/// One cell of a table.
///
/// Always drawn holding at least one paragraph: a `w:tc` with no `w:p` in it is
/// not a table that lays out oddly, it is a file Word offers to repair. The
/// writer supplies an empty paragraph where a caller gives none, because the
/// empty cell is the ordinary case — a witness that reads nothing at a word the
/// others do.
struct DocxTableCell
{
    QList<DocxParagraph> paragraphs;
};

struct DocxTableRow
{
    QList<DocxTableCell> cells;
};

/// A table laid out to widths the caller has already decided.
///
/// Fixed rather than autofit, because a table that resized itself would put
/// words under different words than the ones they were packed under — and a
/// collation whose columns do not line up is not merely ugly, it is wrong.
struct DocxTable
{
    QList<DocxTableRow> rows;
    /// One per grid column, in twentieths of a point. Cell widths are derived
    /// from these rather than stored per cell, so grid and cells cannot
    /// disagree.
    QList<int> columnWidths;
    /// The table reads right to left, so its first cell is drawn at the
    /// right-hand edge. The cells stay in reading order and are not reversed —
    /// Word does the flipping, and cell *n* stays column *n* for everyone who
    /// has to reason about the document afterwards.
    bool rightToLeft = false;
};

/// The width between the margins of the A4 page docxDocumentXml lays out:
/// 11906 twips of paper less 1134 each side. Named because the section
/// properties and anything sizing a table have to agree about it, and two
/// copies of the number would not stay equal.
inline constexpr int TextWidthTwips = 9638;

/// One entry of the document body, which is a sequence of paragraphs and
/// tables. Holding them in one list is what makes the order they alternate in
/// unambiguous; two parallel lists would not state it at all.
struct DocxBlock
{
    DocxBlock() = default;
    // Implicit on purpose: a caller appends a paragraph or a table and reads as
    // it means.
    DocxBlock(const DocxParagraph &paragraph)
        : paragraph(paragraph)
    {
    }
    DocxBlock(const DocxTable &table)
        : table(table)
    {
    }

    /// Read only when `table` is unset.
    DocxParagraph paragraph;
    std::optional<DocxTable> table;

    bool isTable() const { return table.has_value(); }
};

struct DocxFootnote
{
    /// Assigned by the document, never by hand — see FirstFootnoteId.
    int id = 0;
    QString text;
};

/// The first id a real footnote may take.
///
/// 0 and 1 are reserved by the format for the separator and the continuation
/// separator, the little rules Word draws above a page's notes. A footnote
/// numbered 0 is not a footnote numbered wrong; it is a file Word offers to
/// repair.
inline constexpr int FirstFootnoteId = 2;

struct DocxDocument
{
    QString title;
    /// Lines under the title — what the manuscript is, who read it. One
    /// paragraph each, so a document says what it is without anyone typing a
    /// header into Word.
    QStringList subtitle;
    /// The body, in the order it is read: paragraphs and tables interleaved.
    QList<DocxBlock> blocks;
    QList<DocxFootnote> footnotes;

    /// Records `text` as a footnote and answers the id to reference it by.
    int addFootnote(const QString &text);
};

/// The parts of the package, each a pure function of the document.
///
/// Separate from the writing so that what goes into a Word file can be examined
/// without a Word file: these are what the tests read.
QByteArray docxDocumentXml(const DocxDocument &document);
QByteArray docxFootnotesXml(const DocxDocument &document);
QByteArray docxStylesXml();
QByteArray docxContentTypesXml();
QByteArray docxPackageRels();
QByteArray docxDocumentRels();

/// Writes `document` to `path` as a .docx. False when it could not be written,
/// having put the reason in `errorMessage`.
bool writeDocx(const QString &path, const DocxDocument &document, QString *errorMessage);

} // namespace milah
