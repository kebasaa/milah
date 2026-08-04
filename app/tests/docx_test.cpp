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
    document.paragraphs.append(paragraph);
    return document;
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
        document.paragraphs.append(gloss);

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
        document.paragraphs.append(paragraph);

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
        document.paragraphs.append(paragraph);

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
        document.paragraphs.append(paragraph);
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
};

QTEST_MAIN(DocxTest)
#include "docx_test.moc"
