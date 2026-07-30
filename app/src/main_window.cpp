#include "main_window.h"

#include "app_controller.h"
#include "ui/icons.h"
#include "ui/source_settings_widget.h"
#include "ui/verse_grid_widget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace milah {
namespace {

const char *const kStyleSheet = R"CSS(
QWidget#verseCard {
    background: palette(base);
    border: 1px solid palette(mid);
    border-radius: 6px;
}
QLabel#verseHeading {
    font-weight: 600;
}
/* Names the row it sits beside, at the right-hand edge of the readings. Its
   size and colour are set in code, not here: the band packing measures the
   font, and the colour has to hold up in a light and a dark palette alike. */
QLabel#rowAcronym {
    padding-left: 8px;
}
/* Readings carry no box and no padding: a cell is exactly as wide as the
   word in it, which is what keeps the columns measurable and the rows
   readable as running text. Their size is set in code, not here, so that
   measuring a reading and drawing it agree. */
/* The Combined row is where the edition is built. Each word is an editable
   field that looks like plain text until it is being worked on. */
QLineEdit#combinedToken {
    background: transparent;
    border: none;
    border-bottom: 1px solid transparent;
    color: palette(text);
}
QLineEdit#combinedToken:hover {
    border-bottom: 1px solid palette(mid);
}
/* Something was flagged for this word. Advisory only — right-click to see
   what, and nothing changes until it is chosen. */
QLineEdit#combinedToken[flagged="true"] {
    border-bottom: 2px dotted rgba(214, 149, 46, 0.95);
}
QLineEdit#combinedToken:focus {
    background: palette(alternate-base);
    border-bottom: 2px solid palette(highlight);
}
QFrame#bandRule {
    color: palette(mid);
}
QWidget#translationSpan {
    background: rgba(80, 120, 190, 0.12);
    border: 1px solid rgba(80, 120, 190, 0.35);
    border-radius: 4px;
}
QWidget#translationSpan[uncertain="true"] {
    border: 1px dashed rgba(200, 120, 40, 0.85);
}
/* Span controls sit inside a cell only as wide as the readings above it. */
QWidget#translationSpan QToolButton {
    padding: 0px;
    margin: 0px;
    min-width: 14px;
    max-width: 14px;
    font-size: 10px;
}
QLabel#dataWarning {
    color: rgba(214, 149, 46, 1.0);
    padding-right: 8px;
}
QLabel#verseFlags {
    color: rgba(176, 90, 43, 1.0);
    font-size: 11px;
}
QLabel#emptyState {
    color: palette(mid);
    font-size: 14px;
}
)CSS";

/// Spells the accelerator out in the tooltip. There is no menu bar here, so a
/// tooltip is the only place a shortcut gets advertised.
void describeShortcut(QAction *action)
{
    const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
    action->setToolTip(shortcut.isEmpty()
        ? action->text()
        : QStringLiteral("%1 (%2)").arg(action->text(), shortcut));
}

/// Drops an action's label while keeping it for the tooltip and for assistive
/// technology. Used for the chapter arrows and the history pair, whose glyphs
/// say nothing worth the width beside an icon.
void showIconOnly(QToolBar *toolBar, QAction *action)
{
    if (auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action))) {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Milah"));
    resize(1440, 900);
    setMinimumSize(900, 600);
    setStyleSheet(QString::fromUtf8(kStyleSheet));

    m_controller = new AppController(this, this);

    buildToolBar();

    m_verseHost = new QWidget;
    m_verseLayout = new QVBoxLayout(m_verseHost);
    m_verseLayout->setContentsMargins(14, 14, 14, 14);
    m_verseLayout->setSpacing(12);

    m_emptyState = new QLabel;
    m_emptyState->setObjectName(QStringLiteral("emptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_verseLayout->addWidget(m_emptyState);
    m_verseLayout->addStretch(1);

    m_verseArea = new QScrollArea;
    m_verseArea->setWidget(m_verseHost);
    m_verseArea->setWidgetResizable(true);
    m_verseArea->setFrameShape(QFrame::NoFrame);
    // Verse cards wrap themselves to the viewport. A scrollbar that comes and
    // goes would change that width under them, so it is always reserved.
    m_verseArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setCentralWidget(m_verseArea);

    m_settings = new SourceSettingsWidget(m_controller);
    auto *dock = new QDockWidget(QStringLiteral("Sources"), this);
    dock->setObjectName(QStringLiteral("sourcesDock"));
    dock->setWidget(m_settings);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    statusBar()->showMessage(m_controller->message());

    // A missing data file has to stay visible: an ordinary status message is
    // replaced by the next thing that happens, and the symptom — a blank
    // Strong's row — looks like the feature simply not existing.
    if (!m_controller->dataWarning().isEmpty()) {
        auto *warning = new QLabel(QStringLiteral("⚠ No lexicon"));
        warning->setObjectName(QStringLiteral("dataWarning"));
        warning->setToolTip(m_controller->dataWarning());
        statusBar()->addPermanentWidget(warning);
    }

    connect(m_controller, &AppController::sourcesChanged, this, &MainWindow::rebuildAll);
    connect(
        m_controller,
        &AppController::locationChanged,
        this,
        &MainWindow::rebuildVerseList);
    connect(m_controller, &AppController::verseChanged, this, &MainWindow::refreshVerse);
    connect(m_controller, &AppController::displayOptionsChanged, this, [this] {
        // Nothing the cards hold changed, only what they draw, so they redraw
        // themselves rather than the whole chapter being rebuilt.
        for (VerseGridWidget *card : m_verseCards) {
            card->refresh();
        }
    });
    connect(m_controller, &AppController::messageChanged, this, [this](const QString &text) {
        statusBar()->showMessage(text);
    });
    connect(m_controller, &AppController::dirtyChanged, this, [this](bool) {
        updateWindowTitle();
    });
    connect(
        m_controller,
        &AppController::historyChanged,
        this,
        &MainWindow::updateHistoryActions);

    rebuildAll();
}

void MainWindow::openFiles(
    const QStringList &manuscripts,
    const QStringList &translations)
{
    m_controller->loadPaths(SourceRole::Manuscript, manuscripts);
    m_controller->loadPaths(SourceRole::Translation, translations);
}

void MainWindow::buildToolBar()
{
    const QPalette windowPalette = palette();

    auto *toolBar = addToolBar(QStringLiteral("Main"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto *loadManuscripts = toolBar->addAction(QStringLiteral("Load manuscripts"));
    loadManuscripts->setIcon(appIcon(QStringLiteral("load-manuscript"), windowPalette));
    loadManuscripts->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    describeShortcut(loadManuscripts);
    connect(loadManuscripts, &QAction::triggered, this, [this] {
        m_controller->loadSources(SourceRole::Manuscript);
    });

    auto *loadTranslations = toolBar->addAction(QStringLiteral("Load translations"));
    loadTranslations->setIcon(appIcon(QStringLiteral("load-translation"), windowPalette));
    loadTranslations->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    describeShortcut(loadTranslations);
    connect(loadTranslations, &QAction::triggered, this, [this] {
        m_controller->loadSources(SourceRole::Translation);
    });

    auto *openProject = toolBar->addAction(QStringLiteral("Open project"));
    openProject->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentOpen, QStringLiteral("document-open"), windowPalette));
    openProject->setShortcut(QKeySequence::Open);
    describeShortcut(openProject);
    connect(openProject, &QAction::triggered, m_controller, &AppController::openProject);

    m_saveAction = toolBar->addAction(QStringLiteral("Save project"));
    m_saveAction->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentSave, QStringLiteral("document-save"), windowPalette));
    m_saveAction->setShortcut(QKeySequence::Save);
    describeShortcut(m_saveAction);
    connect(m_saveAction, &QAction::triggered, m_controller, &AppController::saveProject);

    m_exportAction = toolBar->addAction(QStringLiteral("Export Combined"));
    m_exportAction->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentSaveAs,
        QStringLiteral("document-save-as"),
        windowPalette));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    describeShortcut(m_exportAction);
    connect(
        m_exportAction, &QAction::triggered, m_controller, &AppController::exportCombined);

    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(QStringLiteral(" Chapter ")));

    m_chapterCombo = new QComboBox;
    m_chapterCombo->setMinimumWidth(150);
    connect(m_chapterCombo, &QComboBox::activated, this, [this](int index) {
        const QList<Location> &locations = m_controller->locations();
        if (index >= 0 && index < locations.size()) {
            m_controller->setLocation(locations.at(index));
        }
    });
    toolBar->addWidget(m_chapterCombo);

    m_previousAction = toolBar->addAction(QStringLiteral("Previous common chapter"));
    m_previousAction->setIcon(actionIcon(
        QIcon::ThemeIcon::GoPrevious, QStringLiteral("go-previous"), windowPalette));
    m_previousAction->setShortcut(QKeySequence(QStringLiteral("Alt+Left")));
    describeShortcut(m_previousAction);
    showIconOnly(toolBar, m_previousAction);
    connect(
        m_previousAction,
        &QAction::triggered,
        m_controller,
        &AppController::goToPreviousLocation);

    m_nextAction = toolBar->addAction(QStringLiteral("Next common chapter"));
    m_nextAction->setIcon(actionIcon(
        QIcon::ThemeIcon::GoNext, QStringLiteral("go-next"), windowPalette));
    m_nextAction->setShortcut(QKeySequence(QStringLiteral("Alt+Right")));
    describeShortcut(m_nextAction);
    showIconOnly(toolBar, m_nextAction);
    connect(
        m_nextAction, &QAction::triggered, m_controller, &AppController::goToNextLocation);

    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(QStringLiteral(" Reference ")));

    m_priorityCombo = new QComboBox;
    m_priorityCombo->setMinimumWidth(180);
    connect(m_priorityCombo, &QComboBox::activated, this, [this](int) {
        m_controller->setPriorityId(m_priorityCombo->currentData().toString());
    });
    toolBar->addWidget(m_priorityCombo);

    auto *strongsAction = toolBar->addAction(QStringLiteral("Strong's"));
    strongsAction->setCheckable(true);
    strongsAction->setChecked(m_controller->strongsVisible());
    strongsAction->setToolTip(
        QStringLiteral("Show Strong's numbers under the Combined row"));
    connect(strongsAction, &QAction::toggled, m_controller, &AppController::setStrongsVisible);

    m_regenerateAction = toolBar->addAction(QStringLiteral("Regenerate"));
    m_regenerateAction->setIcon(actionIcon(
        QIcon::ThemeIcon::ViewRefresh, QStringLiteral("view-refresh"), windowPalette));
    m_regenerateAction->setShortcut(QKeySequence(QStringLiteral("F5")));
    describeShortcut(m_regenerateAction);
    connect(
        m_regenerateAction, &QAction::triggered, m_controller, &AppController::regenerate);

    toolBar->addSeparator();

    m_undoAction = toolBar->addAction(QStringLiteral("Undo"));
    m_undoAction->setIcon(actionIcon(
        QIcon::ThemeIcon::EditUndo, QStringLiteral("edit-undo"), windowPalette));
    m_undoAction->setShortcut(QKeySequence::Undo);
    describeShortcut(m_undoAction);
    showIconOnly(toolBar, m_undoAction);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

    m_redoAction = toolBar->addAction(QStringLiteral("Redo"));
    m_redoAction->setIcon(actionIcon(
        QIcon::ThemeIcon::EditRedo, QStringLiteral("edit-redo"), windowPalette));
    m_redoAction->setShortcut(QKeySequence::Redo);
    describeShortcut(m_redoAction);
    showIconOnly(toolBar, m_redoAction);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);
}

void MainWindow::undo()
{
    // Ctrl+Z reaches the window's action before the focus widget sees the key,
    // so a Combined word being typed in would otherwise lose the whole verse
    // edit instead of the last few characters.
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (editor && editor->isUndoAvailable()) {
        editor->undo();
        return;
    }
    m_controller->undo();
}

void MainWindow::redo()
{
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (editor && editor->isRedoAvailable()) {
        editor->redo();
        return;
    }
    m_controller->redo();
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(m_controller->isDirty()
        ? QStringLiteral("Milah •")
        : QStringLiteral("Milah"));
}

void MainWindow::updateHistoryActions()
{
    m_undoAction->setEnabled(m_controller->canUndo());
    m_redoAction->setEnabled(m_controller->canRedo());
}

void MainWindow::rebuildChapterList()
{
    const QSignalBlocker blocker(m_chapterCombo);
    m_chapterCombo->clear();

    const QList<Location> &locations = m_controller->locations();
    for (const Location &location : locations) {
        m_chapterCombo->addItem(
            QStringLiteral("%1 %2").arg(location.book).arg(location.chapter));
    }

    const std::optional<Location> current = m_controller->location();
    if (current.has_value()) {
        m_chapterCombo->setCurrentIndex(int(locations.indexOf(*current)));
    }
    m_chapterCombo->setEnabled(!locations.isEmpty());

    const int index = current.has_value() ? int(locations.indexOf(*current)) : -1;
    m_previousAction->setEnabled(index > 0);
    m_nextAction->setEnabled(index >= 0 && index < locations.size() - 1);
}

void MainWindow::rebuildPriorityList()
{
    const QSignalBlocker blocker(m_priorityCombo);
    m_priorityCombo->clear();

    const DocumentRefs manuscripts = m_controller->manuscripts();
    for (const SourceDocument *manuscript : manuscripts) {
        m_priorityCombo->addItem(
            manuscript->metadata.title.isEmpty() ? manuscript->name
                                                 : manuscript->metadata.title,
            manuscript->id);
    }

    const int index = m_priorityCombo->findData(m_controller->priorityId());
    if (index >= 0) {
        m_priorityCombo->setCurrentIndex(index);
    }

    const bool hasManuscripts = !manuscripts.isEmpty();
    m_priorityCombo->setEnabled(hasManuscripts);
    m_regenerateAction->setEnabled(hasManuscripts);
    m_saveAction->setEnabled(hasManuscripts);
    m_exportAction->setEnabled(hasManuscripts);
}

void MainWindow::rebuildVerseList()
{
    for (VerseGridWidget *card : m_verseCards) {
        m_verseLayout->removeWidget(card);
        card->deleteLater();
    }
    m_verseCards.clear();

    const DocumentRefs manuscripts = m_controller->manuscripts();
    const std::optional<Location> location = m_controller->location();

    if (manuscripts.isEmpty()) {
        m_emptyState->setText(QStringLiteral(
            "Compare manuscript witnesses\n\n"
            "Load OSIS manuscripts to create an editable Combined edition."));
        m_emptyState->setVisible(true);
    } else if (!location.has_value()) {
        m_emptyState->setText(QStringLiteral(
            "No common chapter\n\n"
            "These manuscripts do not currently share a book and chapter."));
        m_emptyState->setVisible(true);
    } else {
        m_emptyState->setVisible(false);
    }

    if (!location.has_value()) {
        rebuildChapterList();
        return;
    }

    int insertAt = 1; // after the empty-state label
    for (const AlignedVerse &aligned : m_controller->alignedVerses()) {
        if (!m_controller->matchesFilters(aligned)) {
            continue;
        }
        auto *card = new VerseGridWidget(m_controller, aligned, m_verseHost);
        m_verseLayout->insertWidget(insertAt, card);
        m_verseCards.insert(aligned.reference.id, card);
        ++insertAt;
    }

    rebuildChapterList();
}

void MainWindow::refreshVerse(const QString &verseId)
{
    const auto card = m_verseCards.constFind(verseId);
    if (card != m_verseCards.constEnd()) {
        card.value()->refresh();
    }
}

void MainWindow::rebuildAll()
{
    rebuildPriorityList();
    rebuildVerseList();
    m_settings->refresh();
    updateHistoryActions();
    updateWindowTitle();
    statusBar()->showMessage(m_controller->message());
}

} // namespace milah
