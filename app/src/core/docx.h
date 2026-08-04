#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

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
    QList<DocxParagraph> paragraphs;
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
