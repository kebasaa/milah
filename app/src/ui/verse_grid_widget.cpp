#include "ui/verse_grid_widget.h"

#include "app_controller.h"
#include "core/alignment.h"

#include <QFocusEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace milah {
namespace {

constexpr int MinimumColumnWidth = 78;

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

void CommitOnFocusOutEdit::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    emit editingFinished();
}

VerseGridWidget::VerseGridWidget(
    AppController *controller,
    const AlignedVerse &aligned,
    QWidget *parent)
    : QWidget(parent)
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

    m_gridHost = new QWidget;
    // Right-to-left on the grid alone: the toolbar and labels stay in reading
    // order for the editor, while manuscript readings run the way they are
    // written.
    m_gridHost->setLayoutDirection(Qt::RightToLeft);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(4);
    m_grid->setVerticalSpacing(2);

    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidget(m_gridHost);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->addWidget(m_scrollArea);

    auto *editorRow = new QHBoxLayout;
    m_editor = new CommitOnFocusOutEdit;
    m_editor->setObjectName(QStringLiteral("combinedEditor"));
    m_editor->setTabChangesFocus(true);
    m_editor->setMaximumHeight(72);
    m_editor->setAccessibleName(
        QStringLiteral("Combined text for %1").arg(m_aligned.reference.id));
    editorRow->addWidget(m_editor, 1);

    m_flags = new QLabel;
    m_flags->setObjectName(QStringLiteral("verseFlags"));
    m_flags->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    editorRow->addWidget(m_flags, 0);
    outer->addLayout(editorRow);

    connect(m_editor, &CommitOnFocusOutEdit::editingFinished, this, [this] {
        m_controller->setManualText(verseId(), m_editor->toPlainText());
    });

    build();
}

void VerseGridWidget::refresh()
{
    build();
}

void VerseGridWidget::clearGrid()
{
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    for (int column = 0; column < m_grid->columnCount(); ++column) {
        m_grid->setColumnMinimumWidth(column, 0);
        m_grid->setColumnStretch(column, 0);
    }
}

void VerseGridWidget::addRowLabel(int row, const QString &text, const QString &tooltip)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("rowLabel"));
    label->setLayoutDirection(Qt::LeftToRight);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (!tooltip.isEmpty()) {
        label->setToolTip(tooltip);
    }
    m_grid->addWidget(label, row, 0, 1, std::max(1, int(m_aligned.columns.size())));
}

int VerseGridWidget::addManuscriptRows(int row, const SourceDocument *source)
{
    const CombinedDraft draft = m_controller->draftFor(m_aligned);

    for (int index = 0; index < m_aligned.columns.size(); ++index) {
        const AlignmentColumn &column = m_aligned.columns.at(index);
        const SourceToken *token = column.cell(source->id);

        auto *button = new QPushButton;
        button->setFlat(true);
        button->setMinimumWidth(MinimumColumnWidth);
        button->setEnabled(token != nullptr);

        const std::optional<QString> chosen =
            index < draft.columns.size() ? draft.columns.at(index).text : std::nullopt;
        const std::optional<QString> reading =
            token ? std::optional<QString>(token->text) : std::nullopt;

        button->setText(token ? token->text : QStringLiteral("∅"));
        button->setProperty("variant", reading != chosen);
        button->setProperty("gap", token == nullptr);
        button->setProperty("hasNote", token && !token->notes.isEmpty());
        if (token && !token->notes.isEmpty()) {
            button->setToolTip(noteTooltip(*token));
            button->setAccessibleName(QStringLiteral("%1. Has comments.").arg(token->text));
        } else if (token) {
            button->setAccessibleName(token->text);
        } else {
            button->setAccessibleName(QStringLiteral("Missing reading"));
        }

        if (token) {
            const QString sourceId = source->id;
            connect(button, &QPushButton::clicked, this, [this, index, sourceId] {
                m_controller->chooseToken(verseId(), index, sourceId);
            });
        }

        m_grid->addWidget(button, row, index);
        m_grid->setColumnMinimumWidth(index, MinimumColumnWidth);
    }

    addRowLabel(row + 1, sourceLabel(source), source->warnings.join(QStringLiteral("\n")));
    return row + 2;
}

int VerseGridWidget::addTranslationRows(int row, const SourceDocument *translation)
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
        const int start = std::max(0, span.columnStart);
        const int end = std::max(start + 1, std::min(int(m_aligned.columns.size()), span.columnEnd));

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
        if (verse) {
            for (int index = span.tokenStart;
                 index < span.tokenEnd && index < verse->tokens.size();
                 ++index) {
                words.append(verse->tokens.at(index).text);
            }
        }

        auto *cell = new QWidget;
        cell->setObjectName(QStringLiteral("translationSpan"));
        cell->setProperty("uncertain", span.confidence == SpanConfidence::Low);
        cell->setToolTip(span.confidence == SpanConfidence::Low
            ? QStringLiteral("Automatic alignment — use the controls to correct it.")
            : QStringLiteral("Aligned translation"));

        auto *layout = new QHBoxLayout(cell);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(2);

        auto *text = new QLabel(words.join(QLatin1Char(' ')));
        text->setWordWrap(false);
        layout->addWidget(text, 1);

        auto *controls = new QWidget;
        controls->setLayoutDirection(Qt::LeftToRight);
        auto *controlLayout = new QHBoxLayout(controls);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->setSpacing(0);

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

        layout->addWidget(controls, 0);
        m_grid->addWidget(cell, row + lane, start, 1, end - start);
    }

    const int usedRows = std::max(1, int(rowEnds.size()));
    const QString title = translation->metadata.title.isEmpty()
        ? translation->name
        : translation->metadata.title;
    addRowLabel(row + usedRows, title, translation->warnings.join(QStringLiteral("\n")));
    return row + usedRows + 1;
}

int VerseGridWidget::addCombinedRow(int row, const CombinedDraft &draft)
{
    for (int index = 0; index < m_aligned.columns.size(); ++index) {
        auto *label = new QLabel;
        label->setObjectName(QStringLiteral("combinedToken"));
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(MinimumColumnWidth);

        const bool hasColumn = index < draft.columns.size();
        const std::optional<QString> text =
            hasColumn ? draft.columns.at(index).text : std::nullopt;
        label->setText(text.has_value() ? *text : QStringLiteral("∅"));
        label->setProperty(
            "needsReview", hasColumn && draft.columns.at(index).needsReview);
        label->setProperty("gap", !text.has_value());

        m_grid->addWidget(label, row, index);
        m_grid->setColumnMinimumWidth(index, MinimumColumnWidth);
    }

    addRowLabel(row + 1, QStringLiteral("Combined"), QString());
    return row + 2;
}

void VerseGridWidget::build()
{
    clearGrid();

    const CombinedDraft draft = m_controller->draftFor(m_aligned);
    const QHash<QString, QString> associations = m_controller->associationMap();
    const DocumentRefs translationList = m_controller->translations();

    int row = 0;
    for (const SourceDocument *source : m_controller->manuscripts()) {
        row = addManuscriptRows(row, source);
        for (const SourceDocument *translation : translationList) {
            if (associations.value(translation->id) == source->id) {
                row = addTranslationRows(row, translation);
            }
        }
    }
    row = addCombinedRow(row, draft);

    const QString combined = combinedText(draft);
    if (m_editor->toPlainText() != combined) {
        const QSignalBlocker blocker(m_editor);
        m_editor->setPlainText(combined);
    }

    QStringList flags;
    for (const ConsensusColumn &column : draft.columns) {
        if (column.needsReview) {
            flags.append(QStringLiteral("Review tie"));
            break;
        }
    }
    if (draft.manualText.has_value()) {
        flags.append(QStringLiteral("Manually edited"));
    }
    m_flags->setText(flags.join(QStringLiteral("\n")));
    m_flags->setVisible(!flags.isEmpty());

    m_gridHost->adjustSize();
    const int scrollBarHeight = m_scrollArea->horizontalScrollBar()->sizeHint().height();
    m_scrollArea->setFixedHeight(m_gridHost->sizeHint().height() + scrollBarHeight + 4);
}

} // namespace milah
