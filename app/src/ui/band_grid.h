#pragma once

#include "core/bands.h"

#include <QFont>
#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

class QColor;
class QGridLayout;
class QPalette;
class QVBoxLayout;

namespace milah {

/// The geometry the two workspaces share.
///
/// A verse of the comparison and a folio of a transcription are different
/// documents made of different things, but they are laid out the same way: a
/// stack of rows whose columns line up word for word, cut into bands wherever
/// the row runs wider than the window. That packing, its measurements and the
/// reflow loop that keeps it honest live here so the two cannot drift apart —
/// a reader who learns to read one is reading the other already.

/// Gap between adjacent readings. This is the only thing separating one column
/// from the next: the cells themselves carry no border and no padding, so a
/// measured width is the width the reading actually takes.
inline constexpr int ColumnSpacing = 12;
/// The band grid runs right to left, so its column 0 is the rightmost on
/// screen — where each row is named. Readings start one column further in.
inline constexpr int AcronymColumn = 0;
inline constexpr int FirstReadingColumn = 1;
/// Gap between a row's name and the reading beside it. Set in code rather than
/// in the stylesheet because the preview below the bands insets itself by
/// exactly this much to line up with the first word, and two copies of the
/// number would not stay equal.
inline constexpr int AcronymPadding = 8;
/// Rounding slack per column, so a reading never lands a pixel over its cell.
inline constexpr int MeasurementSlack = 4;
/// Room for the caret at either end of an editable word.
inline constexpr int CaretCushion = 10;
/// Room for the note marker inside a Combined field. Qt takes a side action's
/// space out of the text area, so a marked word needs this much more cell or
/// the word itself is squeezed.
inline constexpr int NoteMarkerWidth = 18;
/// Width assumed before the card has been laid out for the first time.
inline constexpr int UnlaidOutWidth = 900;
/// Ignore width changes smaller than this, so scrollbar jitter cannot start a
/// rebuild loop.
inline constexpr int ReflowThreshold = 4;
/// The inset of a card opened by addBandGroup(), which the bands inside it do
/// not have to themselves. Named because the packing has to subtract it: a card
/// measured as though it had the whole width packs one word too many onto a
/// line and then clips it.
inline constexpr int CardPadding = 12;

// Band and packBands moved to core/bands.h: the Word export packs the same way,
// in twentieths of a point instead of pixels, and the two have to break in the
// same places.

/// Readings are set a little larger than the interface: pointed Hebrew is hard
/// to read at the default size. Applied in code rather than in the stylesheet
/// so that measuring a cell and drawing it use the same font.
QFont scaledFont(const QWidget *widget, double factor);

/// The width rich text takes at `font`, plus the per-column rounding slack.
int htmlWidth(const QString &html, const QFont &font);

/// Quieter than the readings but still legible, in a light or a dark palette —
/// which `palette(mid)` is not, so this is applied to the label rather than
/// left to the window's stylesheet.
QString unsettledColor(const QPalette &palette);

QString acronymColor(const QPalette &palette);

/// The mark on a word somebody has written a remark about. Red, and lightened
/// on a dark background where a saturated red goes muddy against the base.
QColor noteMarkerColor(const QPalette &palette);

/// A stack of banded rows that repacks itself when the room it has changes.
///
/// Subclasses say what the rows are and how wide each column has to be; this
/// says where the bands break, keeps the packing from feeding back into the
/// width it was packed for, and collapses a run of resize events into one
/// rebuild. The band host is made here but not placed: a subclass puts it in
/// whatever it surrounds the bands with.
class BandedGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BandedGridWidget(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

    /// Draws the rows for the current content. Called on construction by the
    /// subclass, and again by this class whenever the available width moves.
    virtual void build() = 0;

    /// The widget holding the band stack. A subclass adds this to its own
    /// layout, wherever the bands belong among whatever else it draws.
    QWidget *bandHost() const { return m_bandHost; }

    /// Opens a band, optionally ruled off from the one above it.
    QGridLayout *addBand(bool separator);

    /// Opens a card that the bands after it belong to, and hands back its
    /// layout so a heading can go above them.
    ///
    /// For a stack that holds several things at once — a folio's verses, where
    /// the comparison has one widget per verse and needs none of this. Until
    /// this is called, and again after clearBands(), bands go straight into the
    /// stack as before.
    QVBoxLayout *addBandGroup();

    void clearBands();

    /// How much room the bands actually have, which is not this widget's own
    /// width. See the comment in the implementation — it is the one measurement
    /// the whole reflow loop rests on.
    int availableWidth() const;

    /// Names a row at the right-hand edge of the band. `menu`, when set, is
    /// called with the global position of a right-click on the label.
    void addRowAcronym(
        QGridLayout *grid,
        int row,
        const QString &text,
        const QString &tooltip = QString(),
        const std::function<void(const QPoint &)> &menu = {});

    QFont m_readingFont;
    QFont m_acronymFont;
    /// Right to left on the band alone: the toolbar and the row labels stay in
    /// reading order for the editor, while the text runs the way it is written.
    Qt::LayoutDirection m_bandDirection = Qt::RightToLeft;
    /// Width the current bands were packed for. Reflowing keys off this rather
    /// than off the widget's own width, which follows the content it is given.
    int m_builtForAvailable = -1;
    bool m_rebuildQueued = false;

private:
    QWidget *m_bandHost = nullptr;
    QVBoxLayout *m_bandLayout = nullptr;
    /// Where addBand() puts a band: the stack itself, or the card most recently
    /// opened by addBandGroup().
    QVBoxLayout *m_bandTarget = nullptr;
};

} // namespace milah
