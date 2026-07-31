#include "main_window.h"

#include "app_controller.h"
#include "core/books.h"
#include "core/tokenize.h"
#include "ui/about_dialog.h"
#include "ui/icons.h"
#include "ui/notes_widget.h"
#include "ui/source_settings_widget.h"
#include "ui/verse_grid_widget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSet>
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

    createActions();
    buildMenuBar();
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

    // The settings widget rebuilds all its children whenever the sources
    // change, so the Notes panel is its sibling rather than its child: inside
    // it, it would be destroyed on every reload. Sharing a container puts it
    // directly under the Review filters box all the same.
    auto *dockBody = new QWidget;
    auto *dockLayout = new QVBoxLayout(dockBody);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->setSpacing(10);
    dockLayout->addWidget(m_settings);
    dockLayout->addWidget(new NotesWidget(m_controller));
    dockLayout->addStretch(1);

    auto *dock = new QDockWidget(QStringLiteral("Sources"), this);
    dock->setObjectName(QStringLiteral("sourcesDock"));
    dock->setWidget(dockBody);
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
    connect(m_controller, &AppController::locationChanged, this, [this] {
        // The Reference combo belongs to the chapter, so it is rebuilt with the
        // verses rather than only when the set of manuscripts changes.
        rebuildPriorityList();
        rebuildVerseList();
    });
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
    // What the Edit menu may do depends on which word is being worked on, and
    // on what has since happened to it — a split changes whether it can be
    // split again.
    connect(
        m_controller,
        &AppController::selectionChanged,
        this,
        &MainWindow::updateSelectionActions);
    connect(m_controller, &AppController::verseChanged, this, [this](const QString &) {
        updateSelectionActions();
    });

    rebuildAll();
}

void MainWindow::openFiles(
    const QStringList &manuscripts,
    const QStringList &translations)
{
    m_controller->loadPaths(SourceRole::Manuscript, manuscripts);
    m_controller->loadPaths(SourceRole::Translation, translations);
}

void MainWindow::createActions()
{
    const QPalette windowPalette = palette();

    m_loadManuscriptsAction = new QAction(QStringLiteral("Load manuscripts"), this);
    m_loadManuscriptsAction->setIcon(
        appIcon(QStringLiteral("load-manuscript"), windowPalette));
    m_loadManuscriptsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    describeShortcut(m_loadManuscriptsAction);
    connect(m_loadManuscriptsAction, &QAction::triggered, this, [this] {
        m_controller->loadSources(SourceRole::Manuscript);
    });

    m_loadTranslationsAction = new QAction(QStringLiteral("Load translations"), this);
    m_loadTranslationsAction->setIcon(
        appIcon(QStringLiteral("load-translation"), windowPalette));
    m_loadTranslationsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    describeShortcut(m_loadTranslationsAction);
    connect(m_loadTranslationsAction, &QAction::triggered, this, [this] {
        m_controller->loadSources(SourceRole::Translation);
    });

    m_openAction = new QAction(QStringLiteral("Open project"), this);
    m_openAction->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentOpen, QStringLiteral("document-open"), windowPalette));
    m_openAction->setShortcut(QKeySequence::Open);
    describeShortcut(m_openAction);
    connect(m_openAction, &QAction::triggered, m_controller, &AppController::openProject);

    m_saveAction = new QAction(QStringLiteral("Save project"), this);
    m_saveAction->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentSave, QStringLiteral("document-save"), windowPalette));
    m_saveAction->setShortcut(QKeySequence::Save);
    describeShortcut(m_saveAction);
    connect(m_saveAction, &QAction::triggered, m_controller, &AppController::saveProject);

    m_exportAction = new QAction(QStringLiteral("Export Combined"), this);
    m_exportAction->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentSaveAs,
        QStringLiteral("document-save-as"),
        windowPalette));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    describeShortcut(m_exportAction);
    connect(
        m_exportAction, &QAction::triggered, m_controller, &AppController::exportCombined);

    m_quitAction = new QAction(QStringLiteral("Quit"), this);
    // QKeySequence::Quit is unbound on Windows, so it is written out.
    m_quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    describeShortcut(m_quitAction);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = new QAction(QStringLiteral("Undo"), this);
    m_undoAction->setIcon(actionIcon(
        QIcon::ThemeIcon::EditUndo, QStringLiteral("edit-undo"), windowPalette));
    m_undoAction->setShortcut(QKeySequence::Undo);
    describeShortcut(m_undoAction);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

    m_redoAction = new QAction(QStringLiteral("Redo"), this);
    m_redoAction->setIcon(actionIcon(
        QIcon::ThemeIcon::EditRedo, QStringLiteral("edit-redo"), windowPalette));
    m_redoAction->setShortcut(QKeySequence::Redo);
    describeShortcut(m_redoAction);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);

    // The word-level actions act on whichever Combined word has the focus, so
    // that what the context menu offers on a word is reachable from the menu
    // bar and from the keyboard as well.
    m_splitAction = new QAction(QStringLiteral("Split word"), this);
    m_splitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    describeShortcut(m_splitAction);
    connect(m_splitAction, &QAction::triggered, this, [this] {
        const WordSelection selected = m_controller->selection();
        if (selected.isValid()) {
            m_controller->splitColumn(selected.verseId, selected.columnIndex);
        }
    });

    m_mergePreviousAction = new QAction(QStringLiteral("Merge with previous word"), this);
    connect(m_mergePreviousAction, &QAction::triggered, this, [this] {
        const WordSelection selected = m_controller->selection();
        if (selected.isValid()) {
            m_controller->mergeColumns(selected.verseId, selected.columnIndex - 1);
        }
    });

    m_mergeNextAction = new QAction(QStringLiteral("Merge with next word"), this);
    connect(m_mergeNextAction, &QAction::triggered, this, [this] {
        const WordSelection selected = m_controller->selection();
        if (selected.isValid()) {
            m_controller->mergeColumns(selected.verseId, selected.columnIndex);
        }
    });

    m_dictionaryAction = new QAction(QStringLiteral("Add word to my dictionary"), this);
    connect(m_dictionaryAction, &QAction::triggered, this, [this] {
        const QString word = m_controller->selectedWord();
        if (!word.isEmpty()) {
            m_controller->addToDictionary(word);
        }
    });

    m_regenerateAction = new QAction(QStringLiteral("Regenerate"), this);
    m_regenerateAction->setIcon(actionIcon(
        QIcon::ThemeIcon::ViewRefresh, QStringLiteral("view-refresh"), windowPalette));
    m_regenerateAction->setShortcut(QKeySequence(QStringLiteral("F5")));
    describeShortcut(m_regenerateAction);
    connect(
        m_regenerateAction, &QAction::triggered, m_controller, &AppController::regenerate);

    m_previousAction = new QAction(QStringLiteral("Previous chapter"), this);
    m_previousAction->setIcon(actionIcon(
        QIcon::ThemeIcon::GoPrevious, QStringLiteral("go-previous"), windowPalette));
    m_previousAction->setShortcut(QKeySequence(QStringLiteral("Alt+Left")));
    describeShortcut(m_previousAction);
    connect(
        m_previousAction,
        &QAction::triggered,
        m_controller,
        &AppController::goToPreviousLocation);

    m_nextAction = new QAction(QStringLiteral("Next chapter"), this);
    m_nextAction->setIcon(actionIcon(
        QIcon::ThemeIcon::GoNext, QStringLiteral("go-next"), windowPalette));
    m_nextAction->setShortcut(QKeySequence(QStringLiteral("Alt+Right")));
    describeShortcut(m_nextAction);
    connect(
        m_nextAction, &QAction::triggered, m_controller, &AppController::goToNextLocation);

    m_aboutAction = new QAction(QStringLiteral("About Milah"), this);
    m_aboutAction->setIcon(actionIcon(
        QIcon::ThemeIcon::HelpAbout, QStringLiteral("help-about"), windowPalette));
    m_aboutAction->setShortcut(QKeySequence::HelpContents);
    describeShortcut(m_aboutAction);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildMenuBar()
{
    // The menus carry the very same QAction objects the toolbar does, so a
    // shortcut, an icon and an enabled state are each stated once and the two
    // cannot drift apart.
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(m_openAction);
    file->addAction(m_saveAction);
    file->addSeparator();
    file->addAction(m_loadManuscriptsAction);
    file->addAction(m_loadTranslationsAction);
    file->addSeparator();
    file->addAction(m_exportAction);
    file->addSeparator();
    file->addAction(m_quitAction);

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit->addAction(m_undoAction);
    edit->addAction(m_redoAction);
    edit->addSeparator();
    edit->addAction(m_splitAction);
    edit->addAction(m_mergePreviousAction);
    edit->addAction(m_mergeNextAction);
    edit->addSeparator();
    edit->addAction(m_dictionaryAction);

    QMenu *about = menuBar()->addMenu(QStringLiteral("&About"));
    about->addAction(m_aboutAction);

    updateSelectionActions();
}

void MainWindow::updateSelectionActions()
{
    const WordSelection selected = m_controller->selection();
    const QString word = m_controller->selectedWord();

    // Dividing is offered only where there is something to divide at, the same
    // question the context menu asks.
    m_splitAction->setEnabled(selected.isValid() && dividedWords(word).size() > 1);
    m_mergePreviousAction->setEnabled(
        selected.isValid()
        && m_controller->canMergeWithPrevious(selected.verseId, selected.columnIndex));
    m_mergeNextAction->setEnabled(
        selected.isValid()
        && m_controller->canMergeWithNext(selected.verseId, selected.columnIndex));
    m_dictionaryAction->setEnabled(!word.isEmpty());
}

void MainWindow::buildToolBar()
{
    auto *toolBar = addToolBar(QStringLiteral("Main"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // Reading and editing live here; the file actions and the history are in
    // the menus, which keeps this bar to what a reader reaches for constantly.
    toolBar->addWidget(new QLabel(QStringLiteral(" Book ")));

    m_bookCombo = new QComboBox;
    m_bookCombo->setMinimumWidth(150);
    connect(m_bookCombo, &QComboBox::activated, this, [this](int) {
        // Choosing a book moves to the first chapter of it that anyone has.
        const QString book = m_bookCombo->currentData().toString();
        for (const LocationCoverage &covered : m_controller->locations()) {
            if (covered.location.book == book) {
                m_controller->setLocation(covered.location);
                return;
            }
        }
    });
    toolBar->addWidget(m_bookCombo);

    toolBar->addWidget(new QLabel(QStringLiteral(" Chapter ")));

    m_chapterCombo = new QComboBox;
    m_chapterCombo->setMinimumWidth(90);
    connect(m_chapterCombo, &QComboBox::activated, this, [this](int item) {
        // Each entry carries its place in the controller's flat list, so
        // getting back to the location is a lookup rather than a search.
        const QList<LocationCoverage> &locations = m_controller->locations();
        const int index = m_chapterCombo->itemData(item).toInt();
        if (index >= 0 && index < locations.size()) {
            m_controller->setLocation(locations.at(index).location);
        }
    });
    toolBar->addWidget(m_chapterCombo);

    toolBar->addAction(m_previousAction);
    showIconOnly(toolBar, m_previousAction);
    toolBar->addAction(m_nextAction);
    showIconOnly(toolBar, m_nextAction);

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

    toolBar->addAction(m_regenerateAction);
}

void MainWindow::showAbout()
{
    AboutDialog(this).exec();
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

void MainWindow::markCoverage(
    QComboBox *combo,
    int item,
    bool complete,
    const QString &reason)
{
    // Bold where every manuscript reaches the place, italic where they do not:
    // the two say the same thing from opposite ends and need no legend, and a
    // reader scanning the list can see at a glance which chapters are whole.
    QFont font = combo->font();
    font.setBold(complete);
    font.setItalic(!complete);
    combo->setItemData(item, font, Qt::FontRole);
    if (!complete && !reason.isEmpty()) {
        combo->setItemData(item, reason, Qt::ToolTipRole);
    }
}

void MainWindow::updateHistoryActions()
{
    m_undoAction->setEnabled(m_controller->canUndo());
    m_redoAction->setEnabled(m_controller->canRedo());
}

void MainWindow::rebuildBookList()
{
    const QSignalBlocker blocker(m_bookCombo);
    m_bookCombo->clear();

    // One entry per book, in the canonical order the controller already holds
    // them in, named as a reader knows the book rather than by its OSIS id.
    QStringList books;
    QSet<QString> partial;
    for (const LocationCoverage &covered : m_controller->locations()) {
        if (!books.contains(covered.location.book)) {
            books.append(covered.location.book);
        }
        if (!covered.complete) {
            partial.insert(covered.location.book);
        }
    }

    for (const QString &book : books) {
        m_bookCombo->addItem(bookName(book), book);
        markCoverage(
            m_bookCombo,
            m_bookCombo->count() - 1,
            !partial.contains(book),
            QStringLiteral("Some chapters of this book are missing from at "
                           "least one manuscript."));
    }

    const std::optional<Location> current = m_controller->location();
    if (current.has_value()) {
        m_bookCombo->setCurrentIndex(m_bookCombo->findData(current->book));
    }
    m_bookCombo->setEnabled(!books.isEmpty());
}

void MainWindow::rebuildChapterList()
{
    const QSignalBlocker blocker(m_chapterCombo);
    m_chapterCombo->clear();

    const QList<LocationCoverage> &locations = m_controller->locations();
    const std::optional<Location> current = m_controller->location();
    const QString book = current.has_value() ? current->book : QString();

    for (int index = 0; index < locations.size(); ++index) {
        const LocationCoverage &covered = locations.at(index);
        if (covered.location.book != book) {
            continue;
        }
        // The item carries its place in the flat list, not its own position in
        // this combo, so choosing it needs no second lookup.
        m_chapterCombo->addItem(QString::number(covered.location.chapter), index);
        markCoverage(
            m_chapterCombo,
            m_chapterCombo->count() - 1,
            covered.complete,
            missingFrom(covered.location));
        if (current.has_value() && covered.location == *current) {
            m_chapterCombo->setCurrentIndex(m_chapterCombo->count() - 1);
        }
    }
    m_chapterCombo->setEnabled(m_chapterCombo->count() > 0);

    const int index = current.has_value()
        ? m_controller->indexOfLocation(*current)
        : -1;
    m_previousAction->setEnabled(index > 0);
    m_nextAction->setEnabled(index >= 0 && index < locations.size() - 1);
}

QString MainWindow::missingFrom(const Location &location) const
{
    QStringList absent;
    for (const SourceDocument *manuscript : m_controller->manuscripts()) {
        bool has = false;
        for (const SourceVerse &verse : manuscript->verses) {
            if (verse.reference.book == location.book
                && verse.reference.chapter == location.chapter) {
                has = true;
                break;
            }
        }
        if (!has) {
            absent.append(manuscript->metadata.title.isEmpty()
                ? manuscript->name
                : manuscript->metadata.title);
        }
    }
    if (absent.isEmpty()) {
        return QString();
    }
    return QStringLiteral("Missing from %1.").arg(absent.join(QStringLiteral(", ")));
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

    // The chapter's own reference, not the project's: moving between chapters
    // has to bring the combo with it, or it would claim a manuscript the verses
    // on screen are not being read against.
    const int index = m_priorityCombo->findData(m_controller->currentReference());
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
            "Nothing to show\n\n"
            "These manuscripts hold no book and chapter between them."));
        m_emptyState->setVisible(true);
    } else {
        m_emptyState->setVisible(false);
    }

    if (!location.has_value()) {
        rebuildBookList();
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

    rebuildBookList();
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
