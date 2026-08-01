#include "ui/verse_grid_widget.h"

#include "app_controller.h"
#include "core/alignment.h"
#include "core/diff.h"
#include "core/lexicon.h"
#include "core/suggestions.h"
#include "core/tokenize.h"
#include "core/word_marker.h"
#include "ui/band_grid.h"
#include "ui/icons.h"

#include <QAction>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTextOption>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace milah {
namespace {

/// The band packing, the measurements and the palette-derived colours the
/// comparison shares with the transcription workspace live in ui/band_grid.h.
/// What stays here is what only a comparison has: the letter-level diff marks
/// and the apparatus around them.

/// Inline colours for the letter-level marks. Qt's rich text does not see the
/// window's stylesheet, so these are chosen against the current base colour
/// rather than written as palette() functions in the QSS.
struct DiffColors
{
    QString removed;
    QString added;
};

/// The asterisk marking a word a manuscript comments on. Red, and lightened on
/// a dark background where a saturated red goes muddy against the base.
QColor noteMarkerColor(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128
        ? QColor(QStringLiteral("#ff6b6b"))
        : QColor(QStringLiteral("#c02626"));
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
    : BandedGridWidget(parent)
    , m_controller(controller)
    , m_aligned(aligned)
{
    setObjectName(QStringLiteral("verseCard"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);

    auto *heading = new QLabel(QStringLiteral("%1 %2.%3")
                                   .arg(m_aligned.reference.book)
                                   .arg(m_aligned.reference.chapter)
                                   .arg(m_aligned.reference.verse));
    heading->setObjectName(QStringLiteral("verseHeading"));
    outer->addWidget(heading);

    outer->addWidget(bandHost());

    m_previewRow = new QHBoxLayout;
    // A preview, not an editor: the verse is built word by word in the
    // Combined row, and this shows what those words come to.
    m_preview = new QPlainTextEdit;
    m_preview->setObjectName(QStringLiteral("combinedPreview"));
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(72);
    m_preview->setAccessibleName(
        QStringLiteral("Combined text for %1").arg(m_aligned.reference.id));
    m_previewRow->addWidget(m_preview, 1);

    m_flags = new QLabel;
    m_flags->setObjectName(QStringLiteral("verseFlags"));
    m_flags->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_previewRow->addWidget(m_flags, 0);
    outer->addLayout(m_previewRow);

    build();
}

void VerseGridWidget::refresh()
{
    build();
}

bool VerseGridWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        const QVariant column = watched->property("columnIndex");
        if (column.isValid()) {
            m_controller->selectWord(verseId(), column.toInt());
        }
    }
    return QWidget::eventFilter(watched, event);
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
    row.font = m_readingFont;
    row.acronym = acronyms.value(source->id, source->id);
    row.sourceId = source->id;
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
    row.font = m_readingFont;
    row.acronym = QStringLiteral("Combined");
    row.cells.reserve(m_aligned.columns.size());

    // Asked once for the whole row: the note belongs to the column, so a word
    // is marked whichever witness carries the remark, and even where the
    // edition itself reads nothing there.
    const DocumentRefs sources = m_controller->manuscripts();

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
        cell.noted = !columnNotes(m_aligned.columns.at(index), sources).isEmpty();
        row.cells.append(cell);
    }

    return row;
}

VerseGridWidget::Row VerseGridWidget::interlinearRow() const
{
    Row row;
    row.objectName = QStringLiteral("interlinearToken");
    row.font = m_acronymFont;
    row.acronym = QStringLiteral("Interlinear");
    row.tooltip = QStringLiteral(
        "The translation under each Combined word, several words to one joined "
        "by a dash. Type here to word it yourself; empty it to follow the "
        "translation again. This is what the interlinear export is made of.");
    row.cells.reserve(m_aligned.columns.size());

    for (int index = 0; index < m_aligned.columns.size(); ++index) {
        Cell cell;
        cell.plain = m_controller->interlinearWord(verseId(), index);
        row.cells.append(cell);
    }
    return row;
}

VerseGridWidget::Row VerseGridWidget::strongsRow(const Row &combined) const
{
    Row row;
    row.objectName = QStringLiteral("strongsToken");
    row.font = m_acronymFont;
    row.color = acronymColor(palette());
    row.acronym = QStringLiteral("Strong's");
    row.tooltip = markerRowTooltip();
    row.cells.reserve(combined.cells.size());

    // The verdict itself is in core/word_marker.cpp, because the transcription
    // workspace draws the same row under words nobody has aligned yet, and the
    // two must not be able to disagree about what a word is known by.
    for (const Cell &word : combined.cells) {
        const WordMarker marker = markerFor(word.plain, m_controller->dictionary());
        Cell cell;
        cell.plain = marker.text;
        cell.tooltip = marker.tooltip;
        cell.html = marker.text.toHtmlEscaped();
        row.cells.append(cell);
    }

    return row;
}

void VerseGridWidget::addRowAcronym(
    QGridLayout *grid,
    int row,
    const QString &text,
    const QString &tooltip,
    const QString &sourceId,
    bool translation)
{
    // A source's own row: right-clicking its name reaches what can be done with
    // that source here — reading the verse against a manuscript, or closing a
    // translation. A row no source speaks for gets no menu.
    std::function<void(const QPoint &)> menu;
    if (!sourceId.isEmpty()) {
        menu = [this, sourceId, text, translation](const QPoint &global) {
            if (translation) {
                showTranslationMenu(sourceId, global);
            } else {
                showReferenceMenu(sourceId, text, global);
            }
        };
    }
    BandedGridWidget::addRowAcronym(grid, row, text, tooltip, menu);
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
        label->setFont(data.font);
        label->setTextFormat(Qt::RichText);
        label->setText(cell.html);
        label->setAlignment(Qt::AlignCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setAccessibleName(cell.plain);
        if (!data.color.isEmpty()) {
            label->setStyleSheet(QStringLiteral("color: %1;").arg(data.color));
        }
        if (!cell.tooltip.isEmpty()) {
            label->setToolTip(cell.tooltip);
        }
        grid->addWidget(label, row, FirstReadingColumn + index - band.start);
    }

    addRowAcronym(grid, row, data.acronym, data.tooltip, data.sourceId);
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
        const QList<Suggestion> flags = m_suggestions.values(index);

        auto *field = new QLineEdit(cell.plain);
        field->setObjectName(QStringLiteral("combinedToken"));
        field->setProperty("unsettled", cell.unsettled);
        field->setProperty("flagged", !flags.isEmpty());
        field->setFont(m_readingFont);
        field->setFrame(false);
        field->setTextMargins(0, 0, 0, 0);
        field->setAlignment(Qt::AlignCenter);
        field->setAccessibleName(cell.plain.isEmpty()
            ? QStringLiteral("Empty Combined word")
            : cell.plain);
        if (cell.unsettled) {
            field->setStyleSheet(QStringLiteral("color: %1;").arg(muted));
        }

        QStringList tip;
        for (const Suggestion &flag : flags) {
            tip.append(flag.replacement.isEmpty()
                ? flag.reason
                : QStringLiteral("%1 → %2\n%3")
                      .arg(cell.plain, flag.replacement, flag.reason));
        }
        tip.append(QStringLiteral(
            "Type to change this word, or right-click for readings and suggestions."));
        field->setToolTip(tip.join(QStringLiteral("\n\n")));

        // A word some witness comments on. The marker is an action inside the
        // field rather than anything in its text: the text is the edition, and
        // an asterisk typed into it would be saved and exported as one.
        if (cell.noted) {
            QAction *marker = field->addAction(
                tintedIcon(QStringLiteral(":/img/icons/note-marker.svg"),
                           noteMarkerColor(palette())),
                QLineEdit::TrailingPosition);
            marker->setToolTip(QStringLiteral(
                "A manuscript notes this word. Click it to read the note in the "
                "Notes panel."));
            connect(marker, &QAction::triggered, this, [this, field] {
                field->setFocus(Qt::MouseFocusReason);
            });
        }

        // Focus is what "the word being worked on" means, so the Notes panel
        // and the Edit menu follow the caret whether it got here by a click or
        // by tabbing along the row.
        field->setProperty("columnIndex", index);
        field->installEventFilter(this);

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

int VerseGridWidget::addInterlinearCells(
    QGridLayout *grid,
    int row,
    const Band &band,
    const Row &data)
{
    for (int index = band.start; index < band.end; ++index) {
        const Cell &cell = data.cells.at(index);

        auto *field = new QLineEdit(cell.plain);
        field->setObjectName(QStringLiteral("interlinearToken"));
        field->setFont(data.font);
        field->setFrame(false);
        field->setTextMargins(0, 0, 0, 0);
        field->setAlignment(Qt::AlignCenter);
        // Latin under a right-to-left reading: the gloss reads left to right
        // inside its own cell, which still sits under the Hebrew word.
        field->setLayoutDirection(Qt::LeftToRight);
        field->setAccessibleName(cell.plain.isEmpty()
            ? QStringLiteral("Empty interlinear word")
            : cell.plain);
        field->setToolTip(data.tooltip);
        if (!data.color.isEmpty()) {
            field->setStyleSheet(QStringLiteral("color: %1;").arg(data.color));
        }

        connect(field, &QLineEdit::editingFinished, this, [this, field, index] {
            m_controller->setInterlinearWord(verseId(), index, field->text());
        });

        grid->addWidget(field, row, FirstReadingColumn + index - band.start);
    }

    addRowAcronym(grid, row, data.acronym, data.tooltip);
    return row + 1;
}

void VerseGridWidget::showTranslationMenu(
    const QString &sourceId,
    const QPoint &globalPosition)
{
    QMenu menu(this);

    // Which manuscript this translation belongs to, which is what decides the
    // columns its words are spread across — so it is the first thing to reach
    // for when a translation sits against the wrong words.
    const QString current = m_controller->associationMap().value(sourceId);
    const QHash<QString, QString> acronyms = m_controller->acronyms();
    QMenu *align = menu.addMenu(QStringLiteral("Align with"));
    for (const SourceDocument *manuscript : m_controller->manuscripts()) {
        QAction *action = align->addAction(
            acronyms.value(manuscript->id, manuscript->id));
        action->setCheckable(true);
        action->setChecked(manuscript->id == current);
        const QString manuscriptId = manuscript->id;
        connect(action, &QAction::triggered, this, [this, sourceId, manuscriptId] {
            m_controller->setAssociation(sourceId, manuscriptId);
        });
    }
    align->setEnabled(!align->isEmpty());
    align->setToolTip(QStringLiteral(
        "Spreads this translation across that manuscript's words, so its first "
        "word sits under that manuscript's first."));

    menu.addSeparator();

    QAction *close = menu.addAction(QStringLiteral("Close translation"));
    close->setToolTip(QStringLiteral(
        "Unloads this translation and the alignment made for it. What you have "
        "typed into the Interlinear row stays."));
    connect(close, &QAction::triggered, this, [this, sourceId] {
        m_controller->closeTranslation(sourceId);
    });
    menu.exec(globalPosition);
}

void VerseGridWidget::showReferenceMenu(
    const QString &sourceId,
    const QString &acronym,
    const QPoint &globalPosition)
{
    const QString current = m_controller->referenceFor(verseId());

    QMenu menu(this);
    QAction *use = menu.addAction(
        QStringLiteral("Read this verse against %1").arg(acronym));
    use->setCheckable(true);
    use->setChecked(sourceId == current);
    use->setToolTip(QStringLiteral(
        "The other manuscripts are marked against the one this verse is read "
        "against. Only this verse changes."));
    connect(use, &QAction::triggered, this, [this, sourceId] {
        m_controller->setVerseReference(verseId(), sourceId);
    });

    // Only worth offering where a choice has actually been made, since without
    // one the verse already follows the chapter.
    if (!m_controller->selection().verseId.isEmpty() || sourceId != current) {
        QAction *clear = menu.addAction(
            QStringLiteral("Follow the chapter's reference"));
        clear->setEnabled(sourceId == current || current != m_controller->priorityId());
        connect(clear, &QAction::triggered, this, [this] {
            m_controller->setVerseReference(verseId(), QString());
        });
    }

    menu.exec(globalPosition);
}

void VerseGridWidget::showWitnessMenu(int columnIndex, const QPoint &globalPosition)
{
    if (columnIndex < 0 || columnIndex >= m_aligned.columns.size()) {
        return;
    }
    const AlignmentColumn &column = m_aligned.columns.at(columnIndex);
    const QHash<QString, QString> acronyms = m_controller->acronyms();

    QMenu menu;

    // What was flagged comes first: it is the reason the word is marked, and
    // nothing here is applied until it is chosen.
    const QList<Suggestion> flags = m_suggestions.values(columnIndex);
    for (const Suggestion &flag : flags) {
        if (flag.replacement.isEmpty()) {
            continue;
        }
        QAction *action =
            menu.addAction(QStringLiteral("Change to %1").arg(flag.replacement));
        action->setToolTip(flag.reason);
        const QString replacement = flag.replacement;
        connect(action, &QAction::triggered, this, [this, columnIndex, replacement] {
            m_controller->setColumnText(verseId(), columnIndex, replacement);
        });
    }

    // Dividing is offered only where there is something to divide at, and it
    // says what the two words will be so the result is not a surprise.
    const CombinedDraft draft = m_controller->draftFor(m_aligned);
    const QString combinedWord = columnIndex < draft.columns.size()
        ? draft.columns.at(columnIndex).text.value_or(QString())
        : QString();
    const QStringList parts = dividedWords(combinedWord);
    if (parts.size() > 1) {
        QAction *split = menu.addAction(
            QStringLiteral("Split into %1").arg(parts.join(QStringLiteral(" + "))));
        split->setToolTip(QStringLiteral(
            "Gives each word a column of its own. The witnesses still read one "
            "word here, so their rows show a gap beside it."));
        connect(split, &QAction::triggered, this, [this, columnIndex] {
            m_controller->splitColumn(verseId(), columnIndex);
        });
    }

    // Joining back is offered only between two halves of a word this editor
    // divided; where the witnesses read two words there is nothing to undo.
    // Each entry names its neighbour, so which way it goes is not left to the
    // reader to work out from a right-to-left row.
    // A divided half can be left empty, and "Merge with the previous word, "
    // trailing off would read as a fault, so the name is only added when there
    // is one.
    const auto mergeLabel = [&draft](const QString &which, int neighbour) {
        const QString word = neighbour >= 0 && neighbour < draft.columns.size()
            ? draft.columns.at(neighbour).text.value_or(QString())
            : QString();
        return word.isEmpty()
            ? QStringLiteral("Merge with the %1 word").arg(which)
            : QStringLiteral("Merge with the %1 word, %2").arg(which, word);
    };
    const QString mergeHint =
        QStringLiteral("Puts the two back in one column, separated by a space.");
    if (m_controller->canMergeWithPrevious(verseId(), columnIndex)) {
        QAction *merge = menu.addAction(
            mergeLabel(QStringLiteral("previous"), columnIndex - 1));
        merge->setToolTip(mergeHint);
        connect(merge, &QAction::triggered, this, [this, columnIndex] {
            m_controller->mergeColumns(verseId(), columnIndex - 1);
        });
    }
    if (m_controller->canMergeWithNext(verseId(), columnIndex)) {
        QAction *merge = menu.addAction(
            mergeLabel(QStringLiteral("next"), columnIndex + 1));
        merge->setToolTip(mergeHint);
        connect(merge, &QAction::triggered, this, [this, columnIndex] {
            m_controller->mergeColumns(verseId(), columnIndex);
        });
    }

    // The editor's own remark on this word. One entry either way, because
    // whether a note is there yet is not worth two different menus.
    QAction *note = menu.addAction(QStringLiteral("Add/edit note"));
    note->setToolTip(QStringLiteral(
        "Writes your own remark on this word, in the Notes panel beneath what "
        "the manuscripts say."));
    connect(note, &QAction::triggered, this, [this, columnIndex] {
        // Selecting it is what points the panel at this word; the panel then
        // puts the caret in its editor.
        m_controller->selectWord(verseId(), columnIndex);
        m_controller->requestNoteEditing();
    });

    // Offered for any word the edition reads, not only one Milah has queried.
    // A word attested in the Mishnah is never flagged and so was never
    // offered — yet it is exactly the kind with no definition anywhere, and
    // the kind worth writing one for.
    if (!combinedWord.isEmpty()) {
        QAction *define = menu.addAction(
            QStringLiteral("Define %1 in my dictionary").arg(combinedWord));
        define->setToolTip(QStringLiteral(
            "Records what the word means, and stops Milah asking about it, in "
            "every project."));
        connect(define, &QAction::triggered, this, [this, combinedWord] {
            m_controller->addToDictionary(combinedWord);
        });
    }

    // Which manuscript this verse is read against — the same choice the row
    // labels offer, reached from the word the editor already has in hand.
    const QString currentReference = m_controller->referenceFor(verseId());
    QMenu *reference = menu.addMenu(QStringLiteral("Reference for this verse"));
    for (const SourceDocument *source : m_controller->manuscripts()) {
        if (!source->hasVerse(verseId())) {
            continue; // A witness silent here cannot be read against.
        }
        QAction *action = reference->addAction(acronyms.value(source->id, source->id));
        action->setCheckable(true);
        action->setChecked(source->id == currentReference);
        const QString sourceId = source->id;
        connect(action, &QAction::triggered, this, [this, sourceId] {
            m_controller->setVerseReference(verseId(), sourceId);
        });
    }
    reference->setEnabled(!reference->isEmpty());

    if (!menu.isEmpty()) {
        menu.addSeparator();
    }
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
        // A group the editor took out keeps its place in the project so the
        // verse is not regenerated from scratch, but it is not drawn.
        if (span.translationId == translation->id
            && span.verseId == m_aligned.reference.id && !span.removed) {
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

        // The editor's wording where they have given one, which is why this
        // asks the controller rather than reading the tokens directly.
        const QStringList words =
            continued ? QStringList() : m_controller->spanWords(span);

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
            // Column 0 is the rightmost in a right-to-left band, so raising a
            // span's column index moves it *left* on screen. The arrows name
            // what the reader sees, so their delta is turned round to match.
            const int leftward =
                m_controller->readingDirection() == Qt::RightToLeft ? 1 : -1;

            struct Control
            {
                const char *glyph;
                const char *tooltip;
                int delta;
                int kind; // 0 move, 1 resize, 2 merge, 3 split, 4 remove
                bool directional;
            };
            // Aligning only: the wording itself is corrected in the Interlinear
            // row, which is what the export is made of.
            static const Control controlSpecs[] = {
                {"←", "Move this translation left", 1, 0, true},
                {"→", "Move this translation right", -1, 0, true},
                {"−", "Narrow translation span", -1, 1, false},
                {"+", "Widen translation span", 1, 1, false},
                {"⧉", "Merge with the next translation span", 0, 2, false},
                {"⋮", "Split this translation span", 0, 3, false},
                {"🗑", "Remove this translation", 0, 4, false},
            };

            for (const Control &spec : controlSpecs) {
                QToolButton *button = spanButton(
                    QString::fromUtf8(spec.glyph), QString::fromUtf8(spec.tooltip));
                const int delta =
                    spec.directional ? spec.delta * leftward : spec.delta;
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
                    case 3:
                        m_controller->splitSpan(spanId);
                        break;
                    default:
                        m_controller->removeSpan(spanId);
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
        tooltip.join(QStringLiteral("\n")),
        translation->id,
        true);
    return row + usedRows;
}

void VerseGridWidget::build()
{
    clearBands();

    const CombinedDraft draft = m_controller->draftFor(m_aligned);
    const QHash<QString, QString> associations = m_controller->associationMap();
    const DocumentRefs translationList = m_controller->translations();

    // The reference witness reads first; everything else is marked against it.
    // Asked per verse, because a verse its usual witness is silent for is read
    // against one that is not.
    DocumentRefs manuscriptList = m_controller->manuscripts();
    const QString referenceId = m_controller->referenceFor(verseId());
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

    // Reviewed once per build and kept, so the context menu can offer what was
    // found without going over the verse again.
    m_suggestions.clear();
    QList<std::optional<QString>> combinedWords;
    combinedWords.reserve(combined.cells.size());
    for (const Cell &cell : combined.cells) {
        combinedWords.append(
            cell.plain.isEmpty() ? std::nullopt : std::optional<QString>(cell.plain));
    }
    for (const Suggestion &suggestion : reviewVerse(
             combinedWords,
             HebrewLexicon::shared(),
             PhraseRules::shared(),
             m_controller->acceptedForms(),
             AbbreviationTable::shared())) {
        m_suggestions.insert(suggestion.column, suggestion);
    }

    const bool showStrongs = m_controller->strongsVisible();
    const Row strongs = showStrongs ? strongsRow(combined) : Row();
    // Always drawn, whether or not a translation is loaded: it is a line of the
    // edition the editor may write themselves, not a view of a source.
    const Row interlinear = interlinearRow();

    // A column is as wide as its widest reading, so corresponding words line
    // up without any of them being padded out to a fixed cell.
    const QFontMetrics readingMetrics(m_readingFont);
    const QFontMetrics strongsMetrics(m_acronymFont);
    QList<int> widths(m_aligned.columns.size(), 0);
    for (int index = 0; index < widths.size(); ++index) {
        for (const Row &row : readings) {
            widths[index] =
                std::max(widths.at(index), htmlWidth(row.cells.at(index).html, row.font));
        }
        // The Combined cell is a line edit, not rich text: measure its plain
        // text and leave room for the caret, or the last word of a band clips.
        // A note marker sits inside the same field and takes its room from the
        // text, so a marked word is measured wider by exactly what it costs.
        widths[index] = std::max(
            widths.at(index),
            readingMetrics.horizontalAdvance(combined.cells.at(index).plain)
                + CaretCushion
                + (combined.cells.at(index).noted ? NoteMarkerWidth : 0));
        if (showStrongs) {
            widths[index] = std::max(
                widths.at(index),
                strongsMetrics.horizontalAdvance(strongs.cells.at(index).plain)
                    + MeasurementSlack);
        }
        // A line edit like the Combined one, so it needs the caret's room too.
        widths[index] = std::max(
            widths.at(index),
            strongsMetrics.horizontalAdvance(interlinear.cells.at(index).plain)
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
    if (showStrongs) {
        acronymWidth =
            std::max(acronymWidth, acronymMetrics.horizontalAdvance(strongs.acronym));
    }
    acronymWidth =
        std::max(acronymWidth, acronymMetrics.horizontalAdvance(interlinear.acronym));

    const int available = availableWidth();
    m_builtForAvailable = available;
    const int readingRoom =
        std::max(ColumnSpacing, available - acronymWidth - MeasurementSlack - ColumnSpacing);
    const QList<Band> bands = packBands(widths, readingRoom);

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
        row = addCombinedCells(grid, row, band, combined);
        if (showStrongs) {
            row = addCells(grid, row, band, strongs);
        }
        addInterlinearCells(grid, row, band, interlinear);

        // Slack collects on the far side of the readings, so the columns stay
        // as tight as the text rather than being spread across the card.
        grid->setColumnStretch(FirstReadingColumn + std::max(1, band.end - band.start), 1);
    }

    // The bands run right to left whatever the text does, so a row's name is
    // always at the right-hand edge and the verse's first word sits one column
    // further in. Inset the preview by exactly that much and the running text
    // begins under the verse rather than under the labels naming it.
    m_previewRow->setContentsMargins(
        0, 0, acronymWidth + AcronymPadding + ColumnSpacing, 0);

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
