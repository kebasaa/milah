#pragma once

#include "core/types.h"

#include <QPlainTextEdit>
#include <QWidget>

class QGridLayout;
class QLabel;
class QScrollArea;

namespace milah {

class AppController;

/// A QPlainTextEdit that reports when the reader has finished with it, so the
/// Combined text is committed once rather than on every keystroke.
class CommitOnFocusOutEdit final : public QPlainTextEdit
{
    Q_OBJECT

public:
    using QPlainTextEdit::QPlainTextEdit;

signals:
    void editingFinished();

protected:
    void focusOutEvent(QFocusEvent *event) override;
};

/// One verse of the comparison: a row of readings per manuscript, the aligned
/// translation spans beneath each, the Combined token row, and an editor for
/// the Combined text.
class VerseGridWidget final : public QWidget
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

private:
    void build();
    void clearGrid();
    int addManuscriptRows(int row, const SourceDocument *source);
    int addTranslationRows(int row, const SourceDocument *translation);
    int addCombinedRow(int row, const CombinedDraft &draft);
    void addRowLabel(int row, const QString &text, const QString &tooltip);

    AppController *m_controller = nullptr;
    AlignedVerse m_aligned;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_gridHost = nullptr;
    QGridLayout *m_grid = nullptr;
    CommitOnFocusOutEdit *m_editor = nullptr;
    QLabel *m_flags = nullptr;
};

} // namespace milah
