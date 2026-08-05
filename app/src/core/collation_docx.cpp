#include "core/collation_docx.h"

#include "core/bands.h"
#include "core/books.h"
#include "core/diff.h"
#include "core/reading_marks.h"

#include <algorithm>

namespace milah {
namespace {

/// The marks, set in ink.
///
/// The screen picks its two colours out of the window's palette, light or dark.
/// A Word document has no palette and is usually printed, so the export fixes
/// the light pair — the same two the verse card uses on a light theme.
constexpr char kMissingColor[] = "C0392B";
constexpr char kAddedColor[] = "2E8B57";

// Reckoned rather than measured; see the note on columnWidths(). Twentieths of
// a point throughout, which is what Word's table geometry is written in.
constexpr int kHebrewClusterTwips = 170;  ///< about 0.55 em of 14pt pointed Hebrew
constexpr int kGlossCharTwips = 95;       ///< about 0.45 em of 8pt Latin
constexpr int kCellPadTwips = 57;         ///< the cell margin the table declares
constexpr int kMinColumnTwips = 480;      ///< a one-letter word still gets a legible cell
constexpr int kMaxColumnTwips = 2600;     ///< past this a long gloss wraps instead
constexpr int kLabelColumnTwips = 900;    ///< the row's name, at the outer edge
constexpr int kCellGapTwips = 60;
/// A backstop on top of the width reckoning. Whatever the arithmetic says, more
/// columns than this across A4 is not a line anybody reads.
constexpr int kMaxColumnsPerBand = 12;

QString cellTextOr(const AlignmentColumn &column, const QString &sourceId)
{
    const SourceToken *token = column.cell(sourceId);
    return token ? token->text : QString();
}

DocxParagraph styled(const QString &style, bool rightToLeft)
{
    DocxParagraph paragraph;
    paragraph.style = style;
    paragraph.rightToLeft = rightToLeft;
    return paragraph;
}

/// A cell holding one plain line of text. Empty text still yields a paragraph,
/// which is what keeps the row in step with the ones above it.
DocxTableCell textCell(const QString &text, const QString &style, bool rightToLeft, bool hebrew)
{
    DocxParagraph paragraph = styled(style, rightToLeft);
    if (!text.isEmpty()) {
        DocxRun run;
        run.text = text;
        run.hebrew = hebrew;
        paragraph.runs.append(run);
    }
    return DocxTableCell{{paragraph}};
}

/// The marks the core decided, rendered as runs.
///
/// The twin of markedHtml() in ui/verse_grid_widget.cpp. Neither renderer has an
/// opinion about which stretches are marked — that is settled once, in
/// core/reading_marks.cpp, so the page and the screen cannot drift apart.
QList<DocxRun> markedRuns(const MarkedReading &reading)
{
    QList<DocxRun> runs;
    for (const MarkedSegment &segment : reading) {
        DocxRun run;
        run.text = segment.text;
        run.hebrew = true;
        switch (segment.mark) {
        case ReadingMark::Plain:
            break;
        case ReadingMark::Missing:
            run.color = QLatin1String(kMissingColor);
            run.strikeThrough = true;
            break;
        case ReadingMark::Added:
            run.color = QLatin1String(kAddedColor);
            break;
        }
        runs.append(run);
    }
    return runs;
}

/// The witnesses in the order their rows are drawn: the one the verse is read
/// against first, the rest after it in the order they were loaded.
///
/// Only those that reach this verse. A manuscript that is silent for the whole
/// verse gets no row at all, which is a different statement from a manuscript
/// that reads nothing at one word.
DocumentRefs witnessOrder(const CollationVerse &verse, const CollationExport &collation)
{
    DocumentRefs present;
    for (const SourceDocument *source : collation.manuscripts) {
        if (source->hasVerse(verse.aligned.reference.id)) {
            present.append(source);
        }
    }

    const QString reference =
        verse.referenceId.isEmpty() && !present.isEmpty() ? present.first()->id : verse.referenceId;
    for (int index = 1; index < present.size(); ++index) {
        if (present.at(index)->id == reference) {
            present.move(index, 0);
            break;
        }
    }
    return present;
}

QString headingFor(const VerseReference &reference)
{
    const QString book = bookName(reference.book);
    return QStringLiteral("%1 %2").arg(book.isEmpty() ? reference.book : book)
        .arg(reference.chapter);
}

DocxBlock heading(const QString &text, const QString &style)
{
    DocxParagraph paragraph;
    paragraph.style = style;
    paragraph.runs.append(DocxRun{text});
    return paragraph;
}

} // namespace

QList<int> columnWidths(const CollationVerse &verse, const CollationExport &collation)
{
    QList<int> widths;
    widths.reserve(verse.aligned.columns.size());

    for (int index = 0; index < verse.aligned.columns.size(); ++index) {
        const AlignmentColumn &column = verse.aligned.columns.at(index);

        int clusters = 0;
        for (const SourceDocument *source : collation.manuscripts) {
            if (const SourceToken *token = column.cell(source->id)) {
                clusters = std::max(clusters, int(graphemes(token->text).size()));
            }
        }
        if (index < verse.draft.columns.size() && verse.draft.columns.at(index).text) {
            clusters =
                std::max(clusters, int(graphemes(*verse.draft.columns.at(index).text).size()));
        }
        const int reckoned = clusters * kHebrewClusterTwips + 2 * kCellPadTwips;

        // The gloss is Latin, set small, and often longer than the word it
        // stands under — "and it came to pass" beneath one Hebrew word.
        const int gloss = index < verse.interlinear.size()
            ? verse.interlinear.at(index).size() * kGlossCharTwips + 2 * kCellPadTwips
            : 0;

        widths.append(std::clamp(std::max(reckoned, gloss), kMinColumnTwips, kMaxColumnTwips));
    }
    return widths;
}

DocxDocument collationWordDocument(const CollationExport &collation)
{
    DocxDocument document;
    document.title = collation.title;
    document.subtitle = collation.subtitle;

    const int readingRoom = TextWidthTwips - kLabelColumnTwips;
    QString openHeading;

    for (const CollationVerse &verse : collation.verses) {
        const DocumentRefs witnesses = witnessOrder(verse, collation);
        if (witnesses.isEmpty()) {
            // No witness reaches this verse. There is nothing to collate and a
            // table of empty rows would say there was.
            continue;
        }
        const QString referenceId = witnesses.first()->id;

        const QString chapter = headingFor(verse.aligned.reference);
        if (chapter != openHeading) {
            document.blocks.append(heading(chapter, QStringLiteral("Heading1")));
            openHeading = chapter;
        }
        document.blocks.append(heading(
            QStringLiteral("%1.%2").arg(chapter, verse.aligned.reference.verse),
            QStringLiteral("Heading2")));

        const QList<int> widths = columnWidths(verse, collation);
        QList<Band> bands = packBands(widths, readingRoom, kCellGapTwips);

        // The backstop, applied after the packing rather than inside it: the
        // width reckoning is an estimate and may be generous, and twelve words
        // across a page is the limit whatever it thinks.
        QList<Band> capped;
        for (const Band &band : bands) {
            for (int start = band.start; start < band.end; start += kMaxColumnsPerBand) {
                capped.append(Band{start, std::min(start + kMaxColumnsPerBand, band.end)});
            }
        }
        bands = capped;

        for (const Band &band : bands) {
            DocxTable table;
            table.rightToLeft = collation.rightToLeft;

            table.columnWidths.append(kLabelColumnTwips);
            for (int index = band.start; index < band.end; ++index) {
                table.columnWidths.append(widths.at(index));
            }

            for (const SourceDocument *source : witnesses) {
                DocxTableRow row;
                row.cells.append(textCell(
                    collation.acronyms.value(source->id, source->id),
                    QStringLiteral("TableLabel"),
                    false,
                    false));

                for (int index = band.start; index < band.end; ++index) {
                    const AlignmentColumn &column = verse.aligned.columns.at(index);
                    DocxParagraph paragraph =
                        styled(QStringLiteral("TableCell"), collation.rightToLeft);

                    if (const SourceToken *token = column.cell(source->id)) {
                        if (source->id == referenceId) {
                            QStringList others;
                            others.reserve(witnesses.size());
                            for (const SourceDocument *other : witnesses) {
                                if (other->id != source->id) {
                                    others.append(cellTextOr(column, other->id));
                                }
                            }
                            paragraph.runs =
                                markedRuns(markReferenceReading(token->text, others));
                        } else {
                            paragraph.runs = markedRuns(
                                markVariantReading(cellTextOr(column, referenceId), token->text));
                        }

                        // The remark rides on the word it was written about, in
                        // the row of the witness that carries it, so whose it is
                        // shows from where the marker sits as well as from what
                        // the note says.
                        for (const SourceNote &note : token->notes) {
                            const QString acronym =
                                collation.acronyms.value(source->id, source->id);
                            const QString text = note.number.isEmpty()
                                ? QStringLiteral("%1 %2").arg(acronym, note.text)
                                : QStringLiteral("%1 %2. %3")
                                      .arg(acronym, note.number, note.text);
                            DocxRun marker;
                            marker.footnoteId = document.addFootnote(text);
                            paragraph.runs.append(marker);
                        }
                    }
                    row.cells.append(DocxTableCell{{paragraph}});
                }
                table.rows.append(row);
            }

            DocxTableRow combined;
            combined.cells.append(textCell(
                QStringLiteral("Combined"), QStringLiteral("TableLabel"), false, false));
            for (int index = band.start; index < band.end; ++index) {
                DocxParagraph paragraph =
                    styled(QStringLiteral("TableCell"), collation.rightToLeft);
                if (index < verse.draft.columns.size()) {
                    const ConsensusColumn &settled = verse.draft.columns.at(index);
                    if (settled.text && !settled.text->isEmpty()) {
                        DocxRun run;
                        run.text = *settled.text;
                        run.hebrew = true;
                        paragraph.runs.append(run);
                    }
                }
                const QString remark = verse.editorNotes.value(index);
                if (!remark.isEmpty()) {
                    DocxRun marker;
                    marker.footnoteId = document.addFootnote(remark);
                    paragraph.runs.append(marker);
                }
                combined.cells.append(DocxTableCell{{paragraph}});
            }
            table.rows.append(combined);

            DocxTableRow interlinear;
            interlinear.cells.append(textCell(
                QStringLiteral("Interlinear"), QStringLiteral("TableLabel"), false, false));
            for (int index = band.start; index < band.end; ++index) {
                // Left to right and not Hebrew, matching the Interlinear line on
                // screen: the gloss is Latin even where it sits under Hebrew.
                interlinear.cells.append(textCell(
                    verse.interlinear.value(index),
                    QStringLiteral("TableGloss"),
                    false,
                    false));
            }
            table.rows.append(interlinear);

            document.blocks.append(DocxBlock(table));
        }
    }

    return document;
}

} // namespace milah
