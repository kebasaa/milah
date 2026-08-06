#pragma once

#include "core/alignment.h"
#include "core/docx.h"
#include "core/types.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace milah {

/// One verse as the export sees it.
///
/// Everything the controller alone knows, gathered up so that turning a
/// collation into a document is a pure function of its argument — which is what
/// lets the layout be tested without a window.
struct CollationVerse
{
    AlignedVerse aligned;
    CombinedDraft draft;
    /// The interlinear word under each column, indexed by column: what the
    /// editor sees on the Interlinear line, their own wording included, and
    /// empty where they emptied a cell.
    QStringList interlinear;
    /// The manuscript this verse is read against. Its row leads and the others
    /// are marked against it. Empty falls back to the first manuscript.
    QString referenceId;
    /// The editor's own remark on a column, by column index. A different thing
    /// from the manuscripts' notes, which travel on the tokens.
    QHash<int, QString> editorNotes;
};

/// A whole collation, ready to be laid out.
struct CollationExport
{
    QString title;
    QStringList subtitle;
    /// Borrowed, as everywhere else in the core: the documents have to outlive
    /// the call. Consistent with columnNotes(), which borrows the same way.
    DocumentRefs manuscripts;
    QHash<QString, QString> acronyms;
    bool rightToLeft = false;
    QList<CollationVerse> verses;
};

/// The collation as a document to read.
///
/// One table per verse — a row per witness, the edition beneath them and the
/// interlinear beneath that, one aligned word to a column, borders declared
/// away — with the differences coloured as they are on screen and every remark
/// a real footnote. A verse too wide for the page is cut into successive tables
/// the way the verse card cuts it into bands.
///
/// Deliberately absent: the Strong's line, which is a working aid read off the
/// Combined words rather than part of the edition; and the translation-span
/// rows, which span several columns at once and stack into lanes, so they do
/// not map onto a fixed grid — the interlinear line already carries their
/// wording, which is what the interlinear export is made of.
DocxDocument collationWordDocument(const CollationExport &collation);

/// How wide each aligned column is reckoned, in twentieths of a point.
///
/// Public because it is what decides where the bands fall, and a guess nobody
/// can test is a guess that goes wrong quietly. There is no QFontMetrics in the
/// core and there must not be, so these are reckoned from grapheme counts
/// rather than measured. Being wrong is cosmetic and not structural: a narrow
/// cell wraps, and nothing misaligns, because the alignment *is* the table.
QList<int> columnWidths(const CollationVerse &verse, const CollationExport &collation);

} // namespace milah
