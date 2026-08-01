#pragma once

#include "ui/band_grid.h"

#include <QHash>

class QGridLayout;
class QLabel;
class QLineEdit;

namespace milah {

class TranscriptionController;

/// The text of a folio: each word as it was read, the English under it, and
/// what the lexicons make of it under that.
///
/// Packed into bands the same way a verse of the comparison is, and for the
/// same reason — corresponding lines have to be readable side by side without
/// anything scrolling sideways. What differs is the direction of the work: the
/// comparison arranges words that already exist, and this makes them, so nearly
/// all of what is here that is not geometry is the typing layer below.
class TranscriptionGridWidget final : public BandedGridWidget
{
    Q_OBJECT

public:
    explicit TranscriptionGridWidget(
        TranscriptionController *controller,
        QWidget *parent = nullptr);

    /// Puts the caret in a word, rebuilding first if the cell does not exist
    /// yet. Used by the typing layer to follow a split or a merge.
    void focusWord(int verse, int column, int caret = -1);

protected:
    void build() override;
    /// The whole typing layer. A filter rather than a QLineEdit subclass
    /// because what the keys do belongs to the folio — which word is beside
    /// which — and not to any one field.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Which of the three rows a cell belongs to. Held as a property on the
    /// widget so the filter can tell a Hebrew cell from its gloss.
    enum Role { Hebrew = 0, English = 1 };

    /// One editable cell, made the same way for both editable rows.
    QLineEdit *makeCell(int verse, int column, Role role);
    void addVerseLabel(QGridLayout *grid, int verse, int rowSpan);
    void showVerseMenu(int verse, const QPoint &globalPosition);

    /// What the space bar does: divide here, or open a verse when what was
    /// typed is a number. Returns true when the key has been dealt with.
    bool handleSpace(QLineEdit *field, int verse, int column);
    /// What backspace at the very start of a word does: join it onto the one
    /// before it. Returns true when the key has been dealt with.
    bool handleBackspace(QLineEdit *field, int verse, int column);
    /// Moves along the row, or between the two editable rows. Returns true
    /// when there was somewhere to go — Qt's own focus chain walks the order
    /// the cells were made in, which in a right-to-left band is neither along
    /// the line nor down it.
    bool moveFocus(int verse, int column, Role role, int columnStep, int rowStep);

    /// Writes what a cell holds back to the folio. Called on editingFinished
    /// and before any key that rearranges the words around it, so a change is
    /// never lost to the rebuild that follows.
    void commit(QLineEdit *field);

    static int cellKey(int verse, int column) { return (verse << 12) | (column & 0xFFF); }

    TranscriptionController *m_controller = nullptr;
    QLabel *m_emptyState = nullptr;

    QHash<int, QLineEdit *> m_hebrewCells;
    QHash<int, QLineEdit *> m_englishCells;

    /// Where the caret should land once the folio has been redrawn. A rebuild
    /// destroys every field, so a split cannot simply focus the cell it made —
    /// it says where it wants to be and build() puts it there.
    int m_pendingVerse = -1;
    int m_pendingColumn = -1;
    int m_pendingRole = Hebrew;
    int m_pendingCaret = -1;
};

} // namespace milah
