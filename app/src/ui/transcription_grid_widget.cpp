#include "ui/transcription_grid_widget.h"

#include "core/transcription.h"
#include "core/word_marker.h"
#include "transcription_controller.h"

#include "ui/icons.h"

#include <QAction>
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

/// What the first cell of an unread folio says until something is typed into it.
///
/// Two words on purpose. The line above it says what the keys do; this only has
/// to say that the box is here — and because the cell is measured against it,
/// anything longer would set the width of the opening column of every new folio.
QString firstWordGhost()
{
    return QStringLiteral("start here");
}

/// Whether a user edit brought whitespace in with it.
///
/// Which is the same question as "was this pasted or dropped". A space cannot
/// be typed into a Hebrew cell — the filter below takes the space bar and turns
/// it into the next word — so whitespace can only have arrived whole, off the
/// clipboard or the end of a drag. QLineEdit has no insertFromMimeData to
/// override, and watching for Ctrl+V would miss the context menu, the middle
/// click and the drop; this misses none of them.
bool arrivedWhole(const QString &text)
{
    return std::any_of(text.cbegin(), text.cend(), [](QChar character) {
        return character.isSpace();
    });
}

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
        "No folio open.\n\nFile ▸ Open Image starts a transcription."));
    outer->addWidget(m_emptyState);

    // The rules, kept where they are needed: on screen while the folio is still
    // blank, and gone once the first word shows they are no longer news. They
    // used to live in the empty state above, which hides at exactly the moment
    // a transcriber first has to know them.
    m_hint = new QLabel;
    m_hint->setObjectName(QStringLiteral("transcriptionHint"));
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    m_hint->setText(QStringLiteral(
        "Type what you read. A space begins the next word, a number begins a "
        "verse, and right-clicking a verse number starts a new chapter there."));
    // Set here rather than in the window's stylesheet because it has to stay
    // legible on a dark palette, which palette(mid) does not.
    m_hint->setStyleSheet(QStringLiteral("color: %1;").arg(acronymColor(palette())));
    outer->addWidget(m_hint);

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

    if (role == Hebrew) {
        // Only the Hebrew row divides a paste. A gloss is English, and "son of
        // man" pasted into it is one gloss and not three.
        connect(field, &QLineEdit::textEdited, this, [this, verse, column](const QString &text) {
            if (arrivedWhole(text)) {
                handlePaste(verse, column, text);
            }
        });

        field->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            field,
            &QLineEdit::customContextMenuRequested,
            this,
            [this, field, verse, column](const QPoint &position) {
                showWordMenu(verse, column, field->mapToGlobal(position));
            });
    }

    connect(field, &QLineEdit::editingFinished, this, [this, field] { commit(field); });
    return field;
}

void TranscriptionGridWidget::showWordMenu(int verse, int column, const QPoint &globalPosition)
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page || verse < 0 || verse >= page->verses.size()
        || column < 0 || column >= page->verses.at(verse).words.size()) {
        return;
    }
    const TranscribedWord &word = page->verses.at(verse).words.at(column);

    QMenu menu(this);
    // One entry either way: whether a remark is there yet is not worth two
    // different menus, which is the same call the comparison makes.
    QAction *note = menu.addAction(QStringLiteral("Add/edit note"));
    note->setToolTip(QStringLiteral(
        "Writes your own remark on this word, in the Notes panel beside the "
        "manuscript's details."));
    connect(note, &QAction::triggered, this, [this, verse, column] {
        // Selecting it is what points the panel at this word; the panel then
        // puts the caret in its editor.
        m_controller->selectWord(verse, column);
        m_controller->requestNoteEditing();
    });

    if (!word.note.isEmpty()) {
        QAction *clear = menu.addAction(QStringLiteral("Remove note"));
        connect(clear, &QAction::triggered, this, [this, verse, column] {
            m_controller->setNote(verse, column, QString());
        });
    }

    menu.exec(globalPosition);
}

void TranscriptionGridWidget::addVerseHeading(QVBoxLayout *card, int verse)
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        return;
    }
    const TranscribedVerse &data = page->verses.at(verse);

    // The same heading a verse of the comparison carries, and the same rule in
    // the window's stylesheet sets it — except that a transcription is written
    // before it is identified, so it says as much of the reference as is known.
    auto *heading = new QLabel(verseHeading(*page, verse));
    heading->setObjectName(QStringLiteral("verseHeading"));
    heading->setLayoutDirection(Qt::LeftToRight);
    heading->setToolTip(data.startsNewChapter
        ? QStringLiteral("This verse opens chapter %1. Right-click to put it back.")
              .arg(chapterOfVerse(*page, verse))
        : QStringLiteral("Right-click to start a new chapter here."));

    heading->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        heading,
        &QLabel::customContextMenuRequested,
        this,
        [this, heading, verse](const QPoint &position) {
            showVerseMenu(verse, heading->mapToGlobal(position));
        });

    card->addWidget(heading);
}

void TranscriptionGridWidget::build()
{
    m_rebuilding = true;
    clearBands();
    m_hebrewCells.clear();
    m_englishCells.clear();
    m_rebuilding = false;

    const TranscribedPage *page = m_controller->currentPage();
    const bool open = page != nullptr;
    // Nothing read off this folio yet, so the workspace still offers to say
    // where to begin — a ghost in the opening cell, and the rules above it.
    const bool unread = open && isUntouched(*page);
    m_emptyState->setVisible(!open);
    m_hint->setVisible(unread);
    bandHost()->setVisible(open);
    if (!open) {
        m_focusedEntry.clear();
        m_builtForAvailable = availableWidth();
        return;
    }

    const QFontMetrics readingMetrics(m_readingFont);
    const QFontMetrics glossMetrics(m_acronymFont);

    const int available = availableWidth();
    m_builtForAvailable = available;
    // The verse is named above its words now rather than beside them, so the
    // whole width is the text's. What comes off it is the card's own padding.
    const int readingRoom =
        std::max(ColumnSpacing, available - 2 * CardPadding - MeasurementSlack - ColumnSpacing);

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
                // A note marker sits inside the same field and takes its room
                // out of the text, so a remarked word is measured wider by
                // exactly what the marker costs — or the word is squeezed.
                readingMetrics.horizontalAdvance(word.hebrew) + CaretCushion
                    + (word.note.isEmpty() ? 0 : NoteMarkerWidth),
                glossMetrics.horizontalAdvance(word.english) + CaretCushion,
                glossMetrics.horizontalAdvance(marker.text) + MeasurementSlack,
            }));
        }
        // A cell measured only by what is in it is ten pixels wide when that is
        // nothing, which leaves a placeholder nowhere to appear — and is the
        // whole reason an unread folio looks like an empty pane. The one cell
        // carrying the ghost is measured against the ghost instead.
        if (unread && verse == 0 && !widths.isEmpty()) {
            widths[0] = std::max(
                widths.at(0),
                readingMetrics.horizontalAdvance(firstWordGhost()) + CaretCushion);
        }

        // Its own card, headed with as much of its reference as is known — the
        // same shape a verse of the comparison takes, so a folio and a
        // comparison read as the same kind of thing.
        addVerseHeading(addBandGroup(), verse);

        const QList<Band> bands = packBands(widths, readingRoom);
        for (int index = 0; index < bands.size(); ++index) {
            const Band &band = bands.at(index);
            // Ruled only between the bands of one verse now, which is what the
            // rule was ever for: the card says where a verse ends.
            QGridLayout *grid = addBand(index > 0);

            for (int column = band.start; column < band.end; ++column) {
                const TranscribedWord &word = data.words.at(column);
                const int gridColumn = FirstReadingColumn + column - band.start;

                QLineEdit *hebrew = makeCell(verse, column, Hebrew);
                hebrew->setText(word.hebrew);
                if (!word.note.isEmpty()) {
                    // An action inside the field rather than anything in its
                    // text: the text is the manuscript, and an asterisk typed
                    // into it would be transcribed and exported as one.
                    QAction *marker = hebrew->addAction(
                        tintedIcon(QStringLiteral(":/img/icons/note-marker.svg"),
                                   noteMarkerColor(palette())),
                        QLineEdit::TrailingPosition);
                    marker->setToolTip(word.note);
                    connect(marker, &QAction::triggered, this, [this, hebrew] {
                        hebrew->setFocus(Qt::MouseFocusReason);
                        m_controller->requestNoteEditing();
                    });
                }
                if (unread && verse == 0 && column == 0) {
                    // Qt keeps a placeholder on screen while the field is empty
                    // even once it has the focus, so the ghost and the caret
                    // that lands here sit together until the first keystroke.
                    hebrew->setPlaceholderText(firstWordGhost());
                }
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
        m_focusedEntry = page->imageEntry;
        return;
    }

    // A folio that has just come up is opened at its first word, so there is a
    // caret to see and nothing to hunt for.
    //
    // Only when the folio itself changed. build() also runs on every edit, and
    // on naming the book — setBook() reports a page change too — so following
    // any page change would snatch the caret out of the Book field as it was
    // being filled in.
    if (page->imageEntry != m_focusedEntry) {
        m_focusedEntry = page->imageEntry;
        focusWord(0, 0);
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
    if (m_rebuilding) {
        // While the folio is being redrawn the model is what is right and the
        // fields are what is stale. clearBands() hides the focused one, which
        // Qt reports as editingFinished, carrying the text that was in it and
        // the verse and column it used to be — and writing that back is how a
        // number that had just become a verse ended up a word instead.
        return;
    }

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

    // What was typed before the caret, and whatever is left of the cell after
    // it. Read from before the caret rather than from the whole cell, because
    // a number put in front of a word already standing there gives "5מלה" —
    // which is not a verse number, and the space would merely have divided it
    // into two words with the number left among them as an ordinary one.
    QString number = text.left(caret).trimmed();
    QString remainder = text.mid(caret);
    if (!looksLikeVerseNumber(number)) {
        // Failing that, the whole cell, as before — so a caret parked in the
        // middle of "12" still opens verse 12 rather than verse 1 and a word
        // "2".
        number = looksLikeVerseNumber(text) ? text.trimmed() : QString();
        remainder.clear();
    }

    // Wherever it is typed, not only at the end of the verse: a number between
    // spaces is a verse boundary, and one typed further back used to become an
    // ordinary word without a word of complaint.
    if (!number.isEmpty()) {
        // A verse with no number yet takes this one: the first thing typed on
        // a folio is nearly always the number the folio opens at.
        if (page->verses.at(verse).number.isEmpty() && wordCount == 1) {
            m_controller->setWord(verse, column, remainder);
            m_controller->setVerseNumber(verse, number);
            focusWord(verse, column);
        } else {
            // One operation: the digits go, what followed them in this cell
            // opens the new verse, the words after it follow along, and the
            // whole thing is one thing to undo.
            m_controller->startVerse(verse, column, number, remainder);
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

void TranscriptionGridWidget::handlePaste(int verse, int column, const QString &text)
{
    // `text` is the whole field, not only what was dropped into it: whatever
    // was already typed there is part of what has to be divided, and where the
    // caret was is already answered by where the pasted words landed among the
    // letters. So the cell is replaced by everything the text divides into, and
    // the words after it on the line follow along.
    m_controller->pasteAt(verse, column, text);
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
