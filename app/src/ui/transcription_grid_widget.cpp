#include "ui/transcription_grid_widget.h"

#include "core/transcription.h"
#include "core/word_marker.h"
#include "transcription_controller.h"

#include <QFontMetrics>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QVBoxLayout>

#include <algorithm>

namespace milah {
namespace {

/// Marks a cell's place on the folio, so the typing layer can ask a field
/// which word it is without keeping a parallel list that could fall out of step.
const char *const kVerseProperty = "transcriptionVerse";
const char *const kColumnProperty = "transcriptionColumn";
const char *const kRoleProperty = "transcriptionRole";

} // namespace

TranscriptionGridWidget::TranscriptionGridWidget(
    TranscriptionController *controller,
    QWidget *parent)
    : BandedGridWidget(parent)
    , m_controller(controller)
{
    setObjectName(QStringLiteral("transcriptionPage"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 12, 14, 14);
    outer->setSpacing(8);

    m_emptyState = new QLabel;
    m_emptyState->setObjectName(QStringLiteral("emptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_emptyState->setText(QStringLiteral(
        "No folio open.\n\nFile ▸ Open Image starts a transcription. Type what you "
        "read; a space begins the next word, and a number begins a verse."));
    outer->addWidget(m_emptyState);

    outer->addWidget(bandHost());
    outer->addStretch(1);

    connect(
        m_controller,
        &TranscriptionController::versesChanged,
        this,
        &TranscriptionGridWidget::build);
    connect(
        m_controller,
        &TranscriptionController::pageChanged,
        this,
        &TranscriptionGridWidget::build);

    build();
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

QLineEdit *TranscriptionGridWidget::makeCell(int verse, int column, Role role)
{
    auto *field = new QLineEdit;
    field->setObjectName(role == Hebrew ? QStringLiteral("transcribedToken")
                                        : QStringLiteral("transcribedGloss"));
    field->setFrame(false);
    field->setTextMargins(0, 0, 0, 0);
    field->setAlignment(Qt::AlignCenter);
    field->setFont(role == Hebrew ? m_readingFont : m_acronymFont);
    if (role == English) {
        // Latin inside a right-to-left band: the gloss reads left to right in
        // its own cell, which still sits under the Hebrew word it belongs to.
        field->setLayoutDirection(Qt::LeftToRight);
    }
    field->setProperty(kVerseProperty, verse);
    field->setProperty(kColumnProperty, column);
    field->setProperty(kRoleProperty, int(role));
    field->installEventFilter(this);

    connect(field, &QLineEdit::editingFinished, this, [this, field] { commit(field); });
    return field;
}

void TranscriptionGridWidget::addVerseLabel(QGridLayout *grid, int verse, int rowSpan)
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        return;
    }
    const TranscribedVerse &data = page->verses.at(verse);

    // The chapter is shown beside the number rather than only in the toolbar,
    // because a chapter break is a thing that happens partway down a folio and
    // the transcriber has to be able to see where.
    const QString text = data.number.isEmpty()
        ? QStringLiteral("·")
        : QStringLiteral("%1:%2").arg(chapterOfVerse(*page, verse)).arg(data.number);

    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("rowAcronym"));
    label->setLayoutDirection(Qt::LeftToRight);
    label->setFont(m_acronymFont);
    label->setStyleSheet(QStringLiteral("color: %1; padding-left: %2px;")
                             .arg(acronymColor(palette()))
                             .arg(AcronymPadding));
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setToolTip(data.startsNewChapter
        ? QStringLiteral("This verse opens chapter %1. Right-click to put it back.")
              .arg(chapterOfVerse(*page, verse))
        : QStringLiteral("Right-click to start a new chapter here."));

    label->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        label,
        &QLabel::customContextMenuRequested,
        this,
        [this, label, verse](const QPoint &position) {
            showVerseMenu(verse, label->mapToGlobal(position));
        });

    grid->addWidget(label, 0, AcronymColumn, rowSpan, 1);
}

void TranscriptionGridWidget::build()
{
    clearBands();
    m_hebrewCells.clear();
    m_englishCells.clear();

    const TranscribedPage *page = m_controller->currentPage();
    const bool open = page != nullptr;
    m_emptyState->setVisible(!open);
    bandHost()->setVisible(open);
    if (!open) {
        m_builtForAvailable = availableWidth();
        return;
    }

    const QFontMetrics readingMetrics(m_readingFont);
    const QFontMetrics glossMetrics(m_acronymFont);

    // The verse labels all sit in one column, so the widest of them decides how
    // much room is left for the words — measured over the whole folio, or the
    // columns would step sideways when a two-digit verse arrived.
    int labelWidth = glossMetrics.horizontalAdvance(QStringLiteral("000:000"));
    for (int verse = 0; verse < page->verses.size(); ++verse) {
        const QString number = page->verses.at(verse).number;
        if (number.isEmpty()) {
            continue;
        }
        labelWidth = std::max(
            labelWidth,
            glossMetrics.horizontalAdvance(
                QStringLiteral("%1:%2").arg(chapterOfVerse(*page, verse)).arg(number)));
    }

    const int available = availableWidth();
    m_builtForAvailable = available;
    const int readingRoom =
        std::max(ColumnSpacing, available - labelWidth - MeasurementSlack - ColumnSpacing);

    bool ruled = false;
    for (int verse = 0; verse < page->verses.size(); ++verse) {
        const TranscribedVerse &data = page->verses.at(verse);

        // A column is as wide as the widest of the three things stacked in it,
        // so a long gloss widens the word above it rather than being clipped.
        // No rich text is involved here, unlike the comparison, so plain font
        // metrics are the whole measurement.
        QList<int> widths;
        widths.reserve(data.words.size());
        QList<WordMarker> markers;
        markers.reserve(data.words.size());
        for (const TranscribedWord &word : data.words) {
            const WordMarker marker = markerFor(word.hebrew, m_controller->dictionary());
            markers.append(marker);
            widths.append(std::max({
                readingMetrics.horizontalAdvance(word.hebrew) + CaretCushion,
                glossMetrics.horizontalAdvance(word.english) + CaretCushion,
                glossMetrics.horizontalAdvance(marker.text) + MeasurementSlack,
            }));
        }

        const QList<Band> bands = packBands(widths, readingRoom);
        for (int index = 0; index < bands.size(); ++index) {
            const Band &band = bands.at(index);
            // Ruled between verses, and between the bands of one verse, so a
            // line that wraps is as plainly one line continued as it is in the
            // comparison.
            QGridLayout *grid = addBand(ruled);
            ruled = true;

            // The number names the verse once, at its first band: repeating it
            // down a wrapped verse would read as several verses.
            if (index == 0) {
                addVerseLabel(grid, verse, 3);
            }

            for (int column = band.start; column < band.end; ++column) {
                const TranscribedWord &word = data.words.at(column);
                const int gridColumn = FirstReadingColumn + column - band.start;

                QLineEdit *hebrew = makeCell(verse, column, Hebrew);
                hebrew->setText(word.hebrew);
                grid->addWidget(hebrew, 0, gridColumn);
                m_hebrewCells.insert(cellKey(verse, column), hebrew);

                QLineEdit *english = makeCell(verse, column, English);
                english->setText(word.english);
                english->setToolTip(word.englishIsOwn
                    ? QStringLiteral("Your own wording. Clear it to ask the lexicon again.")
                    : QStringLiteral("Suggested by the lexicon. Type over it to make it yours."));
                grid->addWidget(english, 1, gridColumn);
                m_englishCells.insert(cellKey(verse, column), english);

                // Read-only on purpose: the marker is what the corpora say
                // about the word, not an opinion the transcriber holds. It
                // changes by the word above it changing, or by the word being
                // defined in the editor's own dictionary.
                auto *marker = new QLabel(markers.at(column).text);
                marker->setObjectName(QStringLiteral("transcribedMarker"));
                marker->setFont(m_acronymFont);
                marker->setAlignment(Qt::AlignCenter);
                marker->setLayoutDirection(Qt::LeftToRight);
                marker->setFocusPolicy(Qt::NoFocus);
                marker->setStyleSheet(
                    QStringLiteral("color: %1;").arg(acronymColor(palette())));
                if (!markers.at(column).tooltip.isEmpty()) {
                    marker->setToolTip(markers.at(column).tooltip);
                }
                grid->addWidget(marker, 2, gridColumn);
            }

            // Slack collects past the last word, so the columns stay as tight
            // as the text rather than being spread across the folio.
            grid->setColumnStretch(
                FirstReadingColumn + std::max(1, band.end - band.start), 1);
        }
    }

    // Put the caret back where whatever caused this rebuild asked for it.
    if (m_pendingVerse >= 0) {
        const int key = cellKey(m_pendingVerse, m_pendingColumn);
        QLineEdit *field = m_pendingRole == English ? m_englishCells.value(key)
                                                    : m_hebrewCells.value(key);
        if (field) {
            field->setFocus(Qt::OtherFocusReason);
            field->setCursorPosition(
                m_pendingCaret < 0 ? field->text().size()
                                   : std::min(m_pendingCaret, int(field->text().size())));
        }
        m_pendingVerse = -1;
        m_pendingColumn = -1;
        m_pendingRole = Hebrew;
        m_pendingCaret = -1;
    }
}

void TranscriptionGridWidget::focusWord(int verse, int column, int caret)
{
    const int key = cellKey(verse, column);
    if (QLineEdit *field = m_hebrewCells.value(key)) {
        field->setFocus(Qt::OtherFocusReason);
        field->setCursorPosition(
            caret < 0 ? field->text().size() : std::min(caret, int(field->text().size())));
        return;
    }
    // Not drawn yet, so say where to land and let the next build put it there.
    m_pendingVerse = verse;
    m_pendingColumn = column;
    m_pendingRole = Hebrew;
    m_pendingCaret = caret;
}

void TranscriptionGridWidget::showVerseMenu(int verse, const QPoint &globalPosition)
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page || verse < 0 || verse >= page->verses.size()) {
        return;
    }

    QMenu menu(this);
    if (page->verses.at(verse).startsNewChapter) {
        QAction *back = menu.addAction(
            QStringLiteral("Put this verse back in the previous chapter"));
        connect(back, &QAction::triggered, this, [this, verse] {
            m_controller->clearChapterBreak(verse);
        });
    } else {
        QAction *move = menu.addAction(QStringLiteral("Move to new chapter"));
        move->setToolTip(QStringLiteral(
            "This verse and every verse after it move into the next chapter."));
        connect(move, &QAction::triggered, this, [this, verse] {
            m_controller->moveVerseToNewChapter(verse);
        });
    }

    menu.addSeparator();
    QAction *remove = menu.addAction(QStringLiteral("Delete this verse"));
    connect(remove, &QAction::triggered, this, [this, verse] {
        m_controller->removeVerse(verse);
    });

    menu.exec(globalPosition);
}

// --------------------------------------------------------------------------
// Typing
// --------------------------------------------------------------------------

void TranscriptionGridWidget::commit(QLineEdit *field)
{
    const int verse = field->property(kVerseProperty).toInt();
    const int column = field->property(kColumnProperty).toInt();
    if (field->property(kRoleProperty).toInt() == English) {
        m_controller->setEnglish(verse, column, field->text());
    } else {
        m_controller->setWord(verse, column, field->text());
    }
}

bool TranscriptionGridWidget::handleSpace(QLineEdit *field, int verse, int column)
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        return false;
    }
    const QString text = field->text();
    const int caret = field->cursorPosition();
    const int wordCount = page->verses.at(verse).words.size();
    const bool last = column == wordCount - 1;

    // A space after nothing at the end of the folio would only open a second
    // empty column beside the one already waiting, so it does nothing.
    if (text.isEmpty() && last) {
        return true;
    }

    if (looksLikeVerseNumber(text) && last) {
        const QString number = text.trimmed();
        // A verse with no number yet takes this one: the first thing typed on
        // a folio is nearly always the number the folio opens at.
        if (page->verses.at(verse).number.isEmpty() && wordCount == 1) {
            m_controller->setWord(verse, column, QString());
            m_controller->setVerseNumber(verse, number);
            focusWord(verse, column);
        } else {
            // Otherwise it opens the next verse, and the digits go with it
            // rather than staying behind as a word.
            m_controller->setWord(verse, column, QString());
            m_controller->removeColumn(verse, column);
            m_controller->insertVerse(verse, number);
            focusWord(verse + 1, 0);
        }
        return true;
    }

    // An ordinary space divides here. A caret at the end simply opens the next
    // word, which is the same operation with nothing to carry over.
    commit(field);
    m_controller->splitAt(verse, column, caret);
    focusWord(verse, column + 1, 0);
    return true;
}

bool TranscriptionGridWidget::handleBackspace(QLineEdit *field, int verse, int column)
{
    if (field->cursorPosition() != 0 || field->hasSelectedText()) {
        return false; // An ordinary backspace inside the word.
    }
    if (column == 0) {
        return true; // Nothing before it on this line to join onto.
    }

    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        return false;
    }
    // Where the seam will be, measured before the merge because afterwards the
    // two words are one and the join is no longer findable.
    const int seam = page->verses.at(verse).words.at(column - 1).hebrew.size();

    commit(field);
    m_controller->mergeWithPrevious(verse, column);
    focusWord(verse, column - 1, seam);
    return true;
}

bool TranscriptionGridWidget::moveFocus(
    int verse,
    int column,
    Role role,
    int columnStep,
    int rowStep)
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        return false;
    }

    int targetVerse = verse;
    int targetColumn = column + columnStep;
    const Role targetRole = rowStep == 0
        ? role
        : Role(std::clamp(int(role) + rowStep, int(Hebrew), int(English)));

    if (columnStep != 0) {
        // Running off either end of a verse carries on into the next one, so a
        // transcriber tabbing through a folio never has to reach for the mouse.
        while (targetColumn < 0) {
            if (--targetVerse < 0) {
                return false;
            }
            targetColumn += page->verses.at(targetVerse).words.size();
        }
        while (targetColumn >= page->verses.at(targetVerse).words.size()) {
            targetColumn -= page->verses.at(targetVerse).words.size();
            if (++targetVerse >= page->verses.size()) {
                return false;
            }
        }
    }

    const int key = cellKey(targetVerse, targetColumn);
    QLineEdit *field =
        targetRole == English ? m_englishCells.value(key) : m_hebrewCells.value(key);
    if (!field) {
        return false;
    }
    field->setFocus(Qt::TabFocusReason);
    field->selectAll();
    return true;
}

bool TranscriptionGridWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        const QVariant verse = watched->property(kVerseProperty);
        if (verse.isValid()) {
            m_controller->selectWord(
                verse.toInt(), watched->property(kColumnProperty).toInt());
        }
        return BandedGridWidget::eventFilter(watched, event);
    }

    if (event->type() != QEvent::KeyPress) {
        return BandedGridWidget::eventFilter(watched, event);
    }

    auto *field = qobject_cast<QLineEdit *>(watched);
    if (!field) {
        return BandedGridWidget::eventFilter(watched, event);
    }

    auto *key = static_cast<QKeyEvent *>(event);
    const int verse = field->property(kVerseProperty).toInt();
    const int column = field->property(kColumnProperty).toInt();
    const Role role = Role(field->property(kRoleProperty).toInt());

    switch (key->key()) {
    case Qt::Key_Space:
        // Only the Hebrew row divides on a space. A gloss is English, and
        // "son of man" is one gloss and not three.
        if (role == Hebrew && key->modifiers() == Qt::NoModifier) {
            return handleSpace(field, verse, column);
        }
        break;

    case Qt::Key_Backspace:
        if (role == Hebrew && key->modifiers() == Qt::NoModifier) {
            if (handleBackspace(field, verse, column)) {
                return true;
            }
        }
        break;

    case Qt::Key_Tab:
        commit(field);
        if (moveFocus(verse, column, role, 1, 0)) {
            return true;
        }
        break;

    case Qt::Key_Backtab:
        commit(field);
        if (moveFocus(verse, column, role, -1, 0)) {
            return true;
        }
        break;

    case Qt::Key_Down:
        commit(field);
        if (moveFocus(verse, column, role, 0, 1)) {
            return true;
        }
        break;

    case Qt::Key_Up:
        commit(field);
        if (moveFocus(verse, column, role, 0, -1)) {
            return true;
        }
        break;

    case Qt::Key_Left:
    case Qt::Key_Right:
        // Only at the edge of a word, so the arrows still walk the letters
        // inside it. The band runs right to left, so Left is the next word.
        if (field->hasSelectedText()) {
            break;
        }
        if (key->key() == Qt::Key_Left && field->cursorPosition() == 0) {
            commit(field);
            if (moveFocus(verse, column, role, 1, 0)) {
                return true;
            }
        } else if (
            key->key() == Qt::Key_Right
            && field->cursorPosition() == field->text().size()) {
            commit(field);
            if (moveFocus(verse, column, role, -1, 0)) {
                return true;
            }
        }
        break;

    default:
        break;
    }

    return BandedGridWidget::eventFilter(watched, event);
}

} // namespace milah
