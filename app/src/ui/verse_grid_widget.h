#pragma once

#include "core/alignment.h"
#include "core/suggestions.h"
#include "core/types.h"
#include "ui/band_grid.h"

#include <QFont>
#include <QMultiHash>
#include <QPlainTextEdit>

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QVBoxLayout;

namespace milah {

class AppController;

/// One verse of the comparison: a row of readings per manuscript, the aligned
/// translation spans beneath each, the Combined token row, and an editor for
/// the Combined text.
///
/// Corresponding readings share a column. A verse wider than the card is split
/// into successive bands, each an independent table that repeats the whole row
/// stack, so nothing has to scroll sideways.
class VerseGridWidget final : public BandedGridWidget
{
    Q_OBJECT

public:
    VerseGridWidget(
        AppController *controller,
        const AlignedVerse &aligned,
        QWidget *parent = nullptr);

    QString verseId() const { return m_aligned.reference.id; }

    /// Rebuilds from the controller's current state, keeping the aligned
    /// columns this card was created with.
    void refresh();

protected:
    /// Watches the Combined fields so that giving one the focus — by clicking
    /// it or tabbing to it — tells the controller which word is being worked
    /// on. A filter rather than a QLineEdit subclass, because the field needs
    /// nothing else of its own.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// One aligned column's contribution to one row, already marked up.
    /// `unsettled` marks a Combined word still holding its automatic value;
    /// `noted` marks a column some witness attaches a note to.
    struct Cell
    {
        QString html;
        QString plain;
        QString tooltip;
        bool unsettled = false;
        bool noted = false;
    };

    /// A row of readings: one manuscript, or the Combined draft. `acronym`
    /// names it at the right-hand edge; the full title lives in `tooltip`.
    struct Row
    {
        QString objectName;
        QString acronym;
        QString tooltip;
        /// The manuscript this row reads, when it is one. Empty for the
        /// Combined and Strong's rows, which no manuscript speaks for.
        QString sourceId;
        QList<Cell> cells;
        /// Carried with the row because the band packing measures every row,
        /// and the Strong's line is set smaller than the readings.
        QFont font;
        QString color;
    };

    void build() override;

    Row manuscriptRow(
        const SourceDocument *source,
        const SourceDocument *reference,
        const DocumentRefs &manuscripts,
        const QHash<QString, QString> &acronyms) const;
    Row combinedRow(const CombinedDraft &draft) const;
    /// The interlinear Strong's line, read off the Combined words above it.
    Row strongsRow(const Row &combined) const;
    /// The editable interlinear translation, one word per Combined column.
    Row interlinearRow() const;

    int addCells(QGridLayout *grid, int row, const Band &band, const Row &data);
    /// The Combined row: one editable field per aligned column.
    int addCombinedCells(QGridLayout *grid, int row, const Band &band, const Row &data);
    /// The Interlinear row: the same, without the apparatus a Combined word
    /// carries — no suggestions, no note marker, no witness readings.
    int addInterlinearCells(QGridLayout *grid, int row, const Band &band, const Row &data);
    void showWitnessMenu(int columnIndex, const QPoint &globalPosition);
    int addTranslationRows(
        QGridLayout *grid,
        int row,
        const Band &band,
        const SourceDocument *translation,
        const QHash<QString, QString> &acronyms);
    /// A row's name, with the right-click menu its source offers. Wraps the
    /// base class's version, which knows nothing of sources.
    void addRowAcronym(
        QGridLayout *grid,
        int row,
        const QString &text,
        const QString &tooltip,
        const QString &sourceId = QString(),
        bool translation = false);
    /// Offers to read this verse against `sourceId`, from that manuscript's
    /// own row label.
    void showReferenceMenu(
        const QString &sourceId,
        const QString &acronym,
        const QPoint &globalPosition);
    /// Offers to close a loaded translation, from its own row label.
    void showTranslationMenu(const QString &sourceId, const QPoint &globalPosition);

    AppController *m_controller = nullptr;
    AlignedVerse m_aligned;

    /// Holds the preview and its flags. Kept so build() can inset it by the
    /// width of the row-name column, which only build() knows.
    QHBoxLayout *m_previewRow = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QLabel *m_flags = nullptr;

    /// What the last build found worth flagging, keyed by aligned column, so
    /// the context menu can offer it without reviewing the verse again.
    QMultiHash<int, Suggestion> m_suggestions;
};

} // namespace milah
