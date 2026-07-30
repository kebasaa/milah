#include "ui/verse_grid_widget.h"

#include "app_controller.h"
#include "core/alignment.h"
#include "core/diff.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace milah {
namespace {

/// Gap between adjacent readings. This is the only thing separating one column
/// from the next: the cells themselves carry no border and no padding, so a
/// measured width is the width the reading actually takes.
constexpr int ColumnSpacing = 12;
/// The band grid runs right to left, so its column 0 is the rightmost on
/// screen — where each row is named. Readings start one column further in.
constexpr int AcronymColumn = 0;
constexpr int FirstReadingColumn = 1;
/// Rounding slack per column, so a reading never lands a pixel over its cell.
constexpr int MeasurementSlack = 4;
/// Room for the caret at either end of an editable Combined word.
constexpr int CaretCushion = 10;
/// Width assumed before the card has been laid out for the first time.
constexpr int UnlaidOutWidth = 900;
/// Ignore width changes smaller than this, so scrollbar jitter cannot start a
/// rebuild loop.
constexpr int ReflowThreshold = 4;

/// Readings are set a little larger than the interface: pointed Hebrew is hard
/// to read at the default size. Applied in code rather than in the stylesheet
/// so that measuring a cell and drawing it use the same font.
QFont scaledFont(const QWidget *widget, double factor)
{
    QFont font = widget->font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * factor);
    } else {
        font.setPixelSize(int(std::lround(font.pixelSize() * factor)));
    }
    return font;
}

int htmlWidth(const QString &html, const QFont &font)
{
    if (html.isEmpty()) {
        return 0;
    }
    QTextDocument document;
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    document.setHtml(html);
    return int(std::ceil(document.idealWidth())) + MeasurementSlack;
}

/// Inline colours for the letter-level marks. Qt's rich text does not see the
/// window's stylesheet, so these are chosen against the current base colour
/// rather than written as palette() functions in the QSS.
struct DiffColors
{
    QString removed;
    QString added;
};

/// Quieter than the readings but still legible, in a light or a dark palette —
/// which `palette(mid)` is not, so this is applied to the label rather than
/// left to the window's stylesheet.
/// A Combined word still holding the value consensus gave it reads muted, so
/// the eye can find what has not been settled yet.
QString unsettledColor(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128
        ? QStringLiteral("#8fa3bf")
        : QStringLiteral("#5a6c8c");
}

QString acronymColor(const QPalette &palette)
{
    const QColor text = palette.color(QPalette::Text);
    return QStringLiteral("rgba(%1, %2, %3, 0.72)")
        .arg(text.red())
        .arg(text.green())
        .arg(text.blue());
}

DiffColors diffColors(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128
        ? DiffColors{QStringLiteral("#f0776e"), QStringLiteral("#63c58a")}
        : DiffColors{QStringLiteral("#c0392b"), QStringLiteral("#2e8b57")};
}

QString markedRun(const QString &text, const QString &color, bool struck)
{
    const QString inner = struck
        ? QStringLiteral("<s>%1</s>").arg(text.toHtmlEscaped())
        : text.toHtmlEscaped();
    return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, inner);
}

/// Which of the reference reading's graphemes are absent from `other`.
QList<bool> missingFromOther(const QString &reference, const QString &other)
{
    QList<bool> flags;
    for (const DiffSegment &segment : diffGraphemes(reference, other)) {
        if (segment.op == DiffOp::Insert) {
            continue;
        }
        flags.append(QList<bool>(graphemes(segment.before).size(), segment.op == DiffOp::Delete));
    }
    return flags;
}

QString referenceTokenHtml(
    const QString &text,
    const QList<bool> &differs,
    const QString &color)
{
    const QStringList clusters = graphemes(text);
    QString html;
    int index = 0;
    while (index < clusters.size()) {
        const bool marked = differs.value(index, false);
        QString run;
        while (index < clusters.size() && differs.value(index, false) == marked) {
            run += clusters.at(index);
            ++index;
        }
        html += marked ? markedRun(run, color, true) : run.toHtmlEscaped();
    }
    return html;
}

QString variantTokenHtml(
    const QString &reference,
    const QString &text,
    const QString &color)
{
    QString html;
    for (const DiffSegment &segment : diffGraphemes(reference, text)) {
        switch (segment.op) {
        case DiffOp::Equal:
            html += segment.after.toHtmlEscaped();
            break;
        case DiffOp::Insert:
            html += markedRun(segment.after, color, false);
            break;
        case DiffOp::Delete:
            break;
        }
    }
    return html;
}

/// Lays the preview out the way the edition reads. The widget's layout
/// direction alone only moves the scrollbar: the paragraph direction lives on
/// the document's text option, and has to be reapplied after the text is set.
void applyDirection(QPlainTextEdit *preview, Qt::LayoutDirection direction)
{
    preview->setLayoutDirection(direction);

    QTextOption option = preview->document()->defaultTextOption();
    option.setTextDirection(direction);
    option.setAlignment(direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft);
    preview->document()->setDefaultTextOption(option);
}

QString noteTooltip(const SourceToken &token)
{
    QStringList lines;
    for (const SourceNote &note : token.notes) {
        lines.append(note.number.isEmpty()
            ? note.text
            : QStringLiteral("%1. %2").arg(note.number, note.text));
    }
    return lines.join(QStringLiteral("\n\n"));
}

QString sourceLabel(const SourceDocument *source)
{
    const QString title =
        source->metadata.title.isEmpty() ? source->name : source->metadata.title;
    return source->warnings.isEmpty() ? title : title + QStringLiteral("  ⚠");
}

QToolButton *spanButton(const QString &glyph, const QString &tooltip)
{
    auto *button = new QToolButton;
    button->setText(glyph);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

VerseGridWidget::VerseGridWidget(
    AppController *controller,
    const AlignedVerse &aligned,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_aligned(aligned)
{
    setObjectName(QStringLiteral("verseCard"));
    m_readingFont = scaledFont(this, 1.25);
    m_acronymFont = scaledFont(this, 0.85);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);

    auto *heading = new QLabel(QStringLiteral("%1 %2.%3")
                                   .arg(m_aligned.reference.book)
                                   .arg(m_aligned.reference.chapter)
                                   .arg(m_aligned.reference.verse));
    heading->setObjectName(QStringLiteral("verseHeading"));
    outer->addWidget(heading);

    m_bandHost = new QWidget;
    // The bands are packed to fit the viewport, so they must never be the
    // thing that decides how wide the viewport is: an ignored width keeps the
    // readings from pushing the card — and with it the scroll area, and with
    // it the next packing pass — steadily wider.
    m_bandHost->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_bandLayout = new QVBoxLayout(m_bandHost);
    m_bandLayout->setContentsMargins(0, 0, 0, 0);
    m_bandLayout->setSpacing(6);
    outer->addWidget(m_bandHost);

    auto *previewRow = new QHBoxLayout;
    // A preview, not an editor: the verse is built word by word in the
    // Combined row, and this shows what those words come to.
    m_preview = new QPlainTextEdit;
    m_preview->setObjectName(QStringLiteral("combinedPreview"));
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(72);
    m_preview->setAccessibleName(
        QStringLiteral("Combined text for %1").arg(m_aligned.reference.id));
    previewRow->addWidget(m_preview, 1);

    m_flags = new QLabel;
    m_flags->setObjectName(QStringLiteral("verseFlags"));
    m_flags->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    previewRow->addWidget(m_flags, 0);
    outer->addLayout(previewRow);

    build();
}

void VerseGridWidget::refresh()
{
    build();
}

void VerseGridWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (m_rebuildQueued || std::abs(availableWidth() - m_builtForAvailable) <= ReflowThreshold) {
        return;
    }
    // Collapse a run of resize events into one reflow, and never reflow from
    // inside the resize itself.
    m_rebuildQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildQueued = false;
        if (std::abs(availableWidth() - m_builtForAvailable) > ReflowThreshold) {
            build();
        }
    });
}

void VerseGridWidget::clearBands()
{
    while (QLayoutItem *item = m_bandLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            // A span control can trigger the rebuild that deletes it, so the
            // widget is only hidden here and freed once the stack unwinds.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

int VerseGridWidget::availableWidth() const
{
    // Not the card's own width: the card is inside a resizable scroll area and
    // grows to whatever its content demands, so measuring itself would always
    // report enough room and no verse would ever wrap. The viewport is the
    // real limit; the card sits inset from it by the verse list's margins.
    int limit = width();
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *area = qobject_cast<QScrollArea *>(ancestor)) {
            const int inset = mapTo(area->viewport(), QPoint(0, 0)).x();
            limit = area->viewport()->width() - 2 * std::max(0, inset);
            break;
        }
    }

    const QMargins margins = layout() ? layout()->contentsMargins() : QMargins();
    const int available = limit - margins.left() - margins.right();
    return available > 2 * ColumnSpacing ? available : UnlaidOutWidth;
}

VerseGridWidget::Row VerseGridWidget::manuscriptRow(
    const SourceDocument *source,
    const SourceDocument *reference,
    const DocumentRefs &manuscripts,
    const QHash<QString, QString> &acronyms) const
{
    const DiffColors colors = diffColors(palette());
    const bool isReference = reference == nullptr || reference->id == source->id;

    // The reference reading is struck where any other witness that reaches
    // this verse reads something else — including where it reads nothing.
    DocumentRefs others;
    if (isReference) {
        for (const SourceDocument *other : manuscripts) {
            if (other->id != source->id && other->hasVerse(m_aligned.reference.id)) {
                others.append(other);
            }
        }
    }

    Row row;
    row.objectName = QStringLiteral("manuscriptToken");
    row.acronym = acronyms.value(source->id, source->id);
    // The acronym is all the reader sees, so the full title — and anything the
    // loader complained about — has to be reachable from it.
    QStringList tooltip{sourceLabel(source)};
    tooltip.append(source->warnings);
    row.tooltip = tooltip.join(QStringLiteral("\n"));
    row.cells.reserve(m_aligned.columns.size());

    for (const AlignmentColumn &column : m_aligned.columns) {
        Cell cell;
        if (const SourceToken *token = column.cell(source->id)) {
            if (isReference) {
                QList<bool> differs;
                for (const SourceDocument *other : others) {
                    const SourceToken *reading = column.cell(other->id);
                    const QList<bool> flags =
                        missingFromOther(token->text, reading ? reading->text : QString());
                    differs.resize(std::max(differs.size(), flags.size()));
                    for (int index = 0; index < flags.size(); ++index) {
                        differs[index] = differs.at(index) || flags.at(index);
                    }
                }
                cell.html = referenceTokenHtml(token->text, differs, colors.removed);
            } else {
                const SourceToken *anchor = column.cell(reference->id);
                cell.html = variantTokenHtml(
                    anchor ? anchor->text : QString(), token->text, colors.added);
            }

            if (!token->notes.isEmpty()) {
                cell.html = QStringLiteral("<u>%1</u>").arg(cell.html);
                cell.tooltip = noteTooltip(*token);
            }
            cell.plain = token->text;
        }
        row.cells.append(cell);
    }

    return row;
}

VerseGridWidget::Row VerseGridWidget::combinedRow(const CombinedDraft &draft) const
{
    Row row;
    row.objectName = QStringLiteral("combinedToken");
    row.acronym = QStringLiteral("Combined");
    row.cells.reserve(m_aligned.columns.size());

    for (int index = 0; index < m_aligned.columns.size(); ++index) {
        Cell cell;
        if (index < draft.columns.size()) {
            const ConsensusColumn &column = draft.columns.at(index);
            if (column.text.has_value()) {
                cell.plain = *column.text;
                cell.html = cell.plain.toHtmlEscaped();
            }
            cell.unsettled = column.needsReview;
        }
        row.cells.append(cell);
    }

    return row;
}

QGridLayout *VerseGridWidget::addBand(bool separator)
{
    if (separator) {
        auto *rule = new QFrame;
        rule->setObjectName(QStringLiteral("bandRule"));
        rule->setFrameShape(QFrame::HLine);
        rule->setFrameShadow(QFrame::Plain);
        m_bandLayout->addWidget(rule);
    }

    auto *host = new QWidget;
    // Right-to-left on the band alone: the toolbar and the row labels stay in
    // reading order for the editor, while manuscript readings run the way they
    // are written.
    host->setLayoutDirection(Qt::RightToLeft);

    auto *grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(ColumnSpacing);
    grid->setVerticalSpacing(2);
    m_bandLayout->addWidget(host);
    return grid;
}

void VerseGridWidget::addRowAcronym(
    QGridLayout *grid,
    int row,
    const QString &text,
    const QString &tooltip)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("rowAcronym"));
    // Latin in a right-to-left band: the label reads left to right within its
    // own cell, which sits at the right-hand edge of the row.
    label->setLayoutDirection(Qt::LeftToRight);
    label->setFont(m_acronymFont);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(acronymColor(palette())));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (!tooltip.isEmpty()) {
        label->setToolTip(tooltip);
    }
    grid->addWidget(label, row, AcronymColumn);
}

int VerseGridWidget::addCells(QGridLayout *grid, int row, const Band &band, const Row &data)
{
    for (int index = band.start; index < band.end; ++index) {
        const Cell &cell = data.cells.at(index);
        if (cell.html.isEmpty()) {
            continue; // No reading here: the column stays blank, as in a table.
        }

        auto *label = new QLabel;
        label->setObjectName(data.objectName);
        label->setFont(m_readingFont);
        label->setTextFormat(Qt::RichText);
        label->setText(cell.html);
        label->setAlignment(Qt::AlignCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setAccessibleName(cell.plain);
        if (!cell.tooltip.isEmpty()) {
            label->setToolTip(cell.tooltip);
        }
        grid->addWidget(label, row, FirstReadingColumn + index - band.start);
    }

    addRowAcronym(grid, row, data.acronym, data.tooltip);
    return row + 1;
}

int VerseGridWidget::addCombinedCells(
    QGridLayout *grid,
    int row,
    const Band &band,
    const Row &data)
{
    const QString muted = unsettledColor(palette());

    for (int index = band.start; index < band.end; ++index) {
        const Cell &cell = data.cells.at(index);

        // Frameless, so a word looks like text until it is being edited: the
        // stylesheet gives the field a frame once it takes focus.
        auto *field = new QLineEdit(cell.plain);
        field->setObjectName(QStringLiteral("combinedToken"));
        field->setProperty("unsettled", cell.unsettled);
        field->setFont(m_readingFont);
        field->setFrame(false);
        field->setTextMargins(0, 0, 0, 0);
        field->setAlignment(Qt::AlignCenter);
        field->setToolTip(QStringLiteral(
            "Type to change this word, or right-click to take a witness's reading."));
        field->setAccessibleName(cell.plain.isEmpty()
            ? QStringLiteral("Empty Combined word")
            : cell.plain);
        if (cell.unsettled) {
            field->setStyleSheet(QStringLiteral("color: %1;").arg(muted));
        }

        connect(field, &QLineEdit::editingFinished, this, [this, field, index] {
            m_controller->setColumnText(verseId(), index, field->text());
        });

        field->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            field,
            &QLineEdit::customContextMenuRequested,
            this,
            [this, field, index](const QPoint &position) {
                showWitnessMenu(index, field->mapToGlobal(position));
            });

        grid->addWidget(field, row, FirstReadingColumn + index - band.start);
    }

    addRowAcronym(grid, row, data.acronym, data.tooltip);
    return row + 1;
}

void VerseGridWidget::showWitnessMenu(int columnIndex, const QPoint &globalPosition)
{
    if (columnIndex < 0 || columnIndex >= m_aligned.columns.size()) {
        return;
    }
    const AlignmentColumn &column = m_aligned.columns.at(columnIndex);
    const QHash<QString, QString> acronyms = m_controller->acronyms();

    QMenu menu;
    for (const SourceDocument *source : m_controller->manuscripts()) {
        const SourceToken *token = column.cell(source->id);
        if (!token) {
            continue;
        }
        const QString name = acronyms.value(source->id, source->id);
        QAction *action =
            menu.addAction(QStringLiteral("%1  —  %2").arg(token->text, name));
        const QString sourceId = source->id;
        connect(action, &QAction::triggered, this, [this, columnIndex, sourceId] {
            m_controller->chooseToken(verseId(), columnIndex, sourceId);
        });
    }

    if (!menu.isEmpty()) {
        menu.addSeparator();
    }
    QAction *omit = menu.addAction(QStringLiteral("Omit this word"));
    connect(omit, &QAction::triggered, this, [this, columnIndex] {
        m_controller->setColumnText(verseId(), columnIndex, QString());
    });

    menu.exec(globalPosition);
}

int VerseGridWidget::addTranslationRows(
    QGridLayout *grid,
    int row,
    const Band &band,
    const SourceDocument *translation,
    const QHash<QString, QString> &acronyms)
{
    const SourceVerse *verse = translation->verse(m_aligned.reference.id);

    QList<TranslationSpan> spans;
    for (const TranslationSpan &span : m_controller->translationSpans()) {
        if (span.translationId == translation->id
            && span.verseId == m_aligned.reference.id) {
            spans.append(span);
        }
    }

    // Corrections can leave two spans overlapping. A grid cannot show them in
    // the same row, so overlapping spans drop to a row of their own.
    QList<int> rowEnds;
    for (const TranslationSpan &span : spans) {
        const int spanStart = std::max(0, span.columnStart);
        const int spanEnd =
            std::max(spanStart + 1, std::min(int(m_aligned.columns.size()), span.columnEnd));

        const int start = std::max(band.start, spanStart);
        const int end = std::min(band.end, spanEnd);
        if (end <= start) {
            continue;
        }
        // A span cannot be cut into two texts — its token range does not map
        // onto single columns — so the band it starts in carries the whole
        // translation and later bands only mark that it runs on.
        const bool continued = spanStart < band.start;

        int lane = 0;
        while (lane < rowEnds.size() && rowEnds.at(lane) > start) {
            ++lane;
        }
        if (lane == rowEnds.size()) {
            rowEnds.append(end);
        } else {
            rowEnds[lane] = end;
        }

        QStringList words;
        if (verse && !continued) {
            for (int index = span.tokenStart;
                 index < span.tokenEnd && index < verse->tokens.size();
                 ++index) {
                words.append(verse->tokens.at(index).text);
            }
        }

        auto *cell = new QWidget;
        cell->setObjectName(QStringLiteral("translationSpan"));
        // Columns are sized by the readings alone. A span — whose controls are
        // wider than most words — takes the room its columns already have
        // rather than prising the whole band apart.
        cell->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        cell->setProperty("uncertain", span.confidence == SpanConfidence::Low);
        cell->setToolTip(span.confidence == SpanConfidence::Low
            ? QStringLiteral("Automatic alignment — use the controls to correct it.")
            : QStringLiteral("Aligned translation"));

        // Stacked, not side by side: the cell is only as wide as the readings
        // above it, and a row of controls beside the text would leave nothing
        // of the translation to read.
        auto *layout = new QVBoxLayout(cell);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(2);

        const QString joined = words.join(QLatin1Char(' '));
        auto *text = new QLabel(continued ? QStringLiteral("…") : joined);
        text->setWordWrap(true);
        text->setAlignment(Qt::AlignCenter);
        layout->addWidget(text);
        if (!joined.isEmpty()) {
            text->setToolTip(joined);
        }

        if (!continued) {
            auto *controls = new QWidget;
            controls->setLayoutDirection(Qt::LeftToRight);
            controls->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto *controlLayout = new QHBoxLayout(controls);
            controlLayout->setContentsMargins(0, 0, 0, 0);
            controlLayout->setSpacing(0);
            controlLayout->addStretch(1);

            const QString spanId = span.id;
            struct Control
            {
                const char *glyph;
                const char *tooltip;
                int delta;
                int kind; // 0 move, 1 resize, 2 merge, 3 split
            };
            static const Control controlSpecs[] = {
                {"←", "Move translation span left", -1, 0},
                {"→", "Move translation span right", 1, 0},
                {"−", "Narrow translation span", -1, 1},
                {"+", "Widen translation span", 1, 1},
                {"⧉", "Merge with the next translation span", 0, 2},
                {"⋮", "Split this translation span", 0, 3},
            };

            for (const Control &spec : controlSpecs) {
                QToolButton *button = spanButton(
                    QString::fromUtf8(spec.glyph), QString::fromUtf8(spec.tooltip));
                const int delta = spec.delta;
                const int kind = spec.kind;
                connect(button, &QToolButton::clicked, this, [this, spanId, delta, kind] {
                    switch (kind) {
                    case 0:
                        m_controller->moveSpan(spanId, delta);
                        break;
                    case 1:
                        m_controller->resizeSpan(spanId, delta);
                        break;
                    case 2:
                        m_controller->mergeSpan(spanId);
                        break;
                    default:
                        m_controller->splitSpan(spanId);
                        break;
                    }
                });
                controlLayout->addWidget(button);
            }
            controlLayout->addStretch(1);

            layout->addWidget(controls);
        }

        grid->addWidget(
            cell, row + lane, FirstReadingColumn + start - band.start, 1, end - start);
    }

    const int usedRows = std::max(1, int(rowEnds.size()));
    QStringList tooltip{sourceLabel(translation)};
    tooltip.append(translation->warnings);
    addRowAcronym(
        grid,
        row,
        acronyms.value(translation->id, translation->id),
        tooltip.join(QStringLiteral("\n")));
    return row + usedRows;
}

void VerseGridWidget::build()
{
    clearBands();

    const CombinedDraft draft = m_controller->draftFor(m_aligned);
    const QHash<QString, QString> associations = m_controller->associationMap();
    const DocumentRefs translationList = m_controller->translations();

    // The reference witness reads first; everything else is marked against it.
    DocumentRefs manuscriptList = m_controller->manuscripts();
    const QString referenceId = m_controller->priorityId();
    for (int index = 0; index < manuscriptList.size(); ++index) {
        if (manuscriptList.at(index)->id == referenceId) {
            manuscriptList.move(index, 0);
            break;
        }
    }
    const SourceDocument *reference =
        manuscriptList.isEmpty() ? nullptr : manuscriptList.first();

    const QHash<QString, QString> acronyms = m_controller->acronyms();

    QList<Row> readings;
    readings.reserve(manuscriptList.size());
    for (const SourceDocument *source : manuscriptList) {
        readings.append(manuscriptRow(source, reference, manuscriptList, acronyms));
    }
    const Row combined = combinedRow(draft);

    // A column is as wide as its widest reading, so corresponding words line
    // up without any of them being padded out to a fixed cell.
    const QFontMetrics readingMetrics(m_readingFont);
    QList<int> widths(m_aligned.columns.size(), 0);
    for (int index = 0; index < widths.size(); ++index) {
        for (const Row &row : readings) {
            widths[index] =
                std::max(widths.at(index), htmlWidth(row.cells.at(index).html, m_readingFont));
        }
        // The Combined cell is a line edit, not rich text: measure its plain
        // text and leave room for the caret, or the last word of a band clips.
        widths[index] = std::max(
            widths.at(index),
            readingMetrics.horizontalAdvance(combined.cells.at(index).plain)
                + CaretCushion);
    }

    // The acronym column is not a reading, but it takes real room: the
    // readings get what is left once the widest row name is paid for.
    const QFontMetrics acronymMetrics(m_acronymFont);
    int acronymWidth = acronymMetrics.horizontalAdvance(combined.acronym);
    for (const Row &row : readings) {
        acronymWidth =
            std::max(acronymWidth, acronymMetrics.horizontalAdvance(row.acronym));
    }
    for (const SourceDocument *translation : translationList) {
        if (!associations.value(translation->id).isEmpty()) {
            acronymWidth = std::max(
                acronymWidth,
                acronymMetrics.horizontalAdvance(
                    acronyms.value(translation->id, translation->id)));
        }
    }

    QList<Band> bands;
    const int available = availableWidth();
    m_builtForAvailable = available;
    const int readingRoom =
        std::max(ColumnSpacing, available - acronymWidth - MeasurementSlack - ColumnSpacing);
    int start = 0;
    int used = 0;
    for (int index = 0; index < widths.size(); ++index) {
        const int required = widths.at(index) + (index > start ? ColumnSpacing : 0);
        if (index > start && used + required > readingRoom) {
            bands.append(Band{start, index});
            start = index;
            used = widths.at(index);
        } else {
            used += required;
        }
    }
    bands.append(Band{start, int(widths.size())});

    for (int index = 0; index < bands.size(); ++index) {
        const Band &band = bands.at(index);
        QGridLayout *grid = addBand(index > 0);

        int row = 0;
        for (int reading = 0; reading < readings.size(); ++reading) {
            row = addCells(grid, row, band, readings.at(reading));
            for (const SourceDocument *translation : translationList) {
                if (associations.value(translation->id) == manuscriptList.at(reading)->id) {
                    row = addTranslationRows(grid, row, band, translation, acronyms);
                }
            }
        }
        addCombinedCells(grid, row, band, combined);

        // Slack collects on the far side of the readings, so the columns stay
        // as tight as the text rather than being spread across the card.
        grid->setColumnStretch(FirstReadingColumn + std::max(1, band.end - band.start), 1);
    }

    const QString text = combinedText(draft);
    if (m_preview->toPlainText() != text) {
        m_preview->setPlainText(text);
    }
    applyDirection(m_preview, m_controller->readingDirection());

    QStringList flags;
    if (draft.manualText.has_value()) {
        flags.append(QStringLiteral("Manually edited"));
    }
    m_flags->setText(flags.join(QStringLiteral("\n")));
    m_flags->setVisible(!flags.isEmpty());
}

} // namespace milah
