#include "main_window.h"

#include "app_controller.h"
#include "core/books.h"
#include "core/recent_files.h"
#include "core/tokenize.h"
#include "core/transcription.h"
#include "transcription_controller.h"
#include "ui/about_dialog.h"
#include "ui/htr_last_run_dialog.h"
#include "ui/icons.h"
#include "ui/training_set.h"
#include "ui/notes_widget.h"
#include "ui/source_settings_widget.h"
#include "ui/transcription_meta_widget.h"
#include "ui/transcription_notes_widget.h"
#include "ui/transcription_widget.h"
#include "ui/verse_grid_widget.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QIntValidator>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionMenuItem>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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
/* Names the row it sits beside, at the right-hand edge of the readings. Nothing
   about it is set here: its size, colour and the gap to the reading beside it
   are all applied in code. The band packing measures the font, the colour has
   to hold up in a light and a dark palette alike, and the gap is a named
   constant because the combined-text preview insets itself by the same amount
   to line up with the first word. See AcronymPadding in verse_grid_widget.cpp. */
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
/* What to do when there is nothing on screen yet — so the one label whose whole
   job is telling a newcomer where to start has to be legible, and palette(mid)
   is not that on a dark palette. Its colour is the sole substitution this sheet
   takes, filled in from the palette where the sheet is applied. */
QLabel#emptyState {
    color: %1;
    font-size: 14px;
}
/* The mode tabs are not here: the colour of the tab that is not chosen has to be
   quieter than the other and still plainly legible, and palette(mid) is not that
   on a dark palette — it all but disappears. So the whole block is built in
   buildModeTabs() against a colour worked out from the palette, which is the
   same rule the readings follow. */
/* A transcribed word and its gloss. Like the Combined row, they look like plain
   text until they are being worked on, because a folio of boxed fields reads as
   a form rather than as a text. Their size is set in code, so that measuring a
   word and drawing it agree. */
QLineEdit#transcribedToken, QLineEdit#transcribedGloss {
    background: transparent;
    border: none;
    border-bottom: 1px solid transparent;
    color: palette(text);
}
QLineEdit#transcribedToken:hover, QLineEdit#transcribedGloss:hover {
    border-bottom: 1px solid palette(mid);
}
QLineEdit#transcribedToken:focus, QLineEdit#transcribedGloss:focus {
    background: palette(alternate-base);
    border-bottom: 2px solid palette(highlight);
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
/// technology. Used for the history pair, whose glyphs say nothing worth the
/// width beside an icon.
void showIconOnly(QToolBar *toolBar, QAction *action)
{
    if (auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action))) {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
}

/// An arrow with a word under it: "prev", "next".
///
/// **An arrow alone cannot say which way is forward in a Hebrew manuscript.** A
/// left-pointing arrow means "back" to anyone reading these interfaces and
/// "onward" to anyone reading the folio in front of them, and the transcriber is
/// doing both at once. The word settles it and costs a few pixels.
///
/// `label` goes in the action's iconText, which is what a toolbar draws when it
/// is showing text — the full label stays on the menu entry, the tooltip and the
/// accessible name, where there is room to say "Previous folio".
void showArrowWithWord(QToolBar *toolBar, QAction *action, const QString &label)
{
    action->setIconText(label);
    if (auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action))) {
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    }
}

/// A width that fits the longest of `entries`, plus the room a line edit needs
/// around its text.
///
/// Measured rather than written down because the longest book name is fifteen
/// characters and a pixel count that suits one interface font clips at the
/// next — and because a field that is too narrow here silently truncates the
/// name of the book being transcribed.
/// An abbreviation fit to stand in a verse id.
///
/// A verse is addressed as book, chapter and number with dots between them, so
/// a book carrying a dot of its own would make "Tob.it.1.1" out of Tobit and
/// there would be no reading it back. Whitespace goes for the same reason.
/// Stripped rather than refused: the transcriber meant a book, not a syntax.
QString sanitisedBookId(const QString &raw)
{
    QString id;
    id.reserve(raw.size());
    for (const QChar character : raw) {
        if (!character.isSpace() && character != QLatin1Char('.')) {
            id.append(character);
        }
    }
    return id;
}

int fieldWidthFor(const QWidget *field, const QStringList &entries)
{
    const QFontMetrics metrics(field->font());
    int widest = 0;
    for (const QString &entry : entries) {
        widest = std::max(widest, metrics.horizontalAdvance(entry));
    }
    // Frame, text margins and a little air. A line edit draws its text inset
    // from its own edge, and the amount is the style's business, not ours.
    return widest + 28;
}

/// The mode tabs, kept to the height of a menu row.
///
/// QMenuBar takes its own height from its corner widget's size hint and does
/// not clamp it, so a stock QTabBar — which asks for a good deal more room than
/// a menu row — would push the whole bar, and everything under it, down. The
/// cap has to be on the hint rather than on the maximum height: a maximum
/// clamps what is drawn while the bar still reserves the full hint, which
/// leaves a short tab bar floating in a bar that is too tall.
///
/// The row is measured off the style rather than remembered, so a change of
/// screen scaling or of interface font carries the tabs with it.
class MenuRowTabBar final : public QTabBar
{
public:
    using QTabBar::QTabBar;

    QSize sizeHint() const override { return capped(QTabBar::sizeHint()); }
    QSize minimumSizeHint() const override { return capped(QTabBar::minimumSizeHint()); }

private:
    QSize capped(QSize size) const
    {
        auto *bar = qobject_cast<QMenuBar *>(parentWidget());
        if (!bar) {
            return size;
        }
        QStyleOptionMenuItem option;
        option.initFrom(bar);
        option.menuItemType = QStyleOptionMenuItem::Normal;
        const int row =
            bar->style()
                ->sizeFromContents(
                    QStyle::CT_MenuBarItem,
                    &option,
                    QSize(1, bar->fontMetrics().height()),
                    bar)
                .height();
        size.setHeight(std::min(size.height(), row));
        return size;
    }
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Milah"));
    // Not the size Milah opens at — main.cpp maximises it. This is the size it
    // returns to when the window is restored, so it still has to be wide enough
    // for a verse to read as one band.
    resize(1440, 900);
    setMinimumSize(900, 600);
    setStyleSheet(QString::fromUtf8(kStyleSheet).arg(acronymColor(palette())));

    m_controller = new AppController(this, this);
    // The editor's dictionary is the editor's, not the edition's: a word
    // defined while transcribing is a word defined while comparing, so the
    // transcription side is handed the one AppController already keeps.
    m_transcriptionController =
        new TranscriptionController(this, &m_controller->dictionary(), this);

    createActions();
    createTranscriptionActions();
    buildMenuBar();
    buildModeTabs();
    buildToolBar();
    buildTranscriptionToolBar();

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

    m_transcription = new TranscriptionWidget(m_transcriptionController);

    // Two jobs, one window. A stack rather than a second window because the
    // reader keeps their place in both, and because Quit, the title bar and the
    // unsaved-work question stay one thing each.
    //
    // A stacked layout lays out only the page on screen, so the verse cards get
    // no resize event while a folio is up and repack themselves on the way
    // back. That is safe only while nothing can rebuild them from the other
    // mode: if a transcription command ever reaches AppController, guard
    // rebuildVerseList() on m_verseArea->isVisible(), or the cards will be
    // packed for a width nothing was ever drawn at.
    m_pages = new QStackedWidget;
    m_pages->setFrameShape(QFrame::NoFrame);
    // A fresh layout is handed the style's own margin. The verse list already
    // insets itself by 14px and the readings are measured against the viewport,
    // so anything here would move the text sideways for no reason.
    if (QLayout *stack = m_pages->layout()) {
        stack->setContentsMargins(0, 0, 0, 0);
    }
    m_pages->addWidget(m_verseArea);
    m_pages->addWidget(m_transcription);
    setCentralWidget(m_pages);

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

    m_sourcesDock = new QDockWidget(QStringLiteral("Sources"), this);
    m_sourcesDock->setObjectName(QStringLiteral("sourcesDock"));
    m_sourcesDock->setWidget(dockBody);
    m_sourcesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_sourcesDock);

    // The transcription side's own panel, in the same place: the two describe
    // what is being worked on, and only one job is being done at a time.
    m_metadata = new TranscriptionMetaWidget(m_transcriptionController);
    auto *metaBody = new QWidget;
    auto *metaLayout = new QVBoxLayout(metaBody);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->setSpacing(10);
    metaLayout->addWidget(m_metadata);
    // Beneath the codex's details, the way the comparison puts a word's notes
    // beneath its sources: what is being worked on, then what is said about it.
    metaLayout->addWidget(new TranscriptionNotesWidget(m_transcriptionController));
    metaLayout->addStretch(1);

    m_metadataDock = new QDockWidget(QStringLiteral("Manuscript"), this);
    m_metadataDock->setObjectName(QStringLiteral("metadataDock"));
    m_metadataDock->setWidget(metaBody);
    m_metadataDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_metadataDock);

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
        updateDictionaryActions();
    });
    connect(m_controller, &AppController::messageChanged, this, [this](const QString &text) {
        statusBar()->showMessage(text);
    });
    connect(m_controller, &AppController::dirtyChanged, this, [this](bool) {
        updateWindowTitle();
    });
    connect(
        m_controller,
        &AppController::sourcesChanged,
        this,
        &MainWindow::updateWindowTitle);
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

    connect(
        m_magnifyAction,
        &QAction::toggled,
        m_transcription,
        &TranscriptionWidget::setMagnifierEnabled);
    connect(m_overlayAction, &QAction::toggled, this, [this](bool shown) {
        m_transcription->setOverlayVisible(shown);
        // The stroke through the eye is the icon, swapped here rather than
        // drawn: one file for each state is what the recolouring loader already
        // takes, and a QIcon has no notion of being crossed out.
        m_overlayAction->setIcon(appIcon(
            shown ? QStringLiteral("eye") : QStringLiteral("eye-off"), palette()));
        // The switch between the two views lives or dies with the overlay it
        // switches, and nothing else on this path would notice.
        updateTranscriptionActions();
    });

    connect(m_lineBoxesAction, &QAction::toggled, this, [this](bool lines) {
        m_transcription->setLineBoxesVisible(lines);
        m_lineBoxesAction->setIcon(appIcon(
            lines ? QStringLiteral("boxes-line") : QStringLiteral("boxes-word"),
            palette()));
    });

    // Through the action, so the eye's own state and the overlay stay one thing.
    connect(m_transcription, &TranscriptionWidget::overlayWanted, this, [this] {
        m_overlayAction->setChecked(true);
    });

    connect(
        m_transcriptionController,
        &TranscriptionController::recognitionApplied,
        this,
        [this] {
            // The enablement first: the eye is grey until the folio has boxes
            // on it, and it has this instant got them.
            updateTranscriptionActions();
            // Through the action rather than the widget, so the toggled lambda
            // above draws the overlay and un-strikes the icon — one statement
            // of what "shown" means rather than two that can disagree.
            m_overlayAction->setChecked(true);
        });

    connect(
        m_transcriptionController,
        &TranscriptionController::messageChanged,
        this,
        [this](const QString &text) { statusBar()->showMessage(text); });
    connect(
        m_transcriptionController,
        &TranscriptionController::dirtyChanged,
        this,
        [this](bool) { updateWindowTitle(); });
    // The file's name is in the title now, and opening one when nothing was
    // dirty changes the name without changing the flag — so the document itself
    // has to say when it has been replaced.
    connect(
        m_transcriptionController,
        &TranscriptionController::documentChanged,
        this,
        &MainWindow::updateWindowTitle);
    connect(
        m_transcriptionController,
        &TranscriptionController::historyChanged,
        this,
        &MainWindow::updateTranscriptionActions);
    connect(
        m_transcriptionController,
        &TranscriptionController::selectionChanged,
        this,
        [this] {
            updateTranscriptionActions();
            // Defining a word is offered on this side too, so which word is
            // under the caret decides whether it is live.
            updateSelectionActions();
            // The Chapter field shows the chapter of the verse being typed in,
            // which a chapter break partway down a folio moves.
            refreshTranscriptionToolBar();
        });
    connect(
        m_transcriptionController,
        &TranscriptionController::pageChanged,
        this,
        [this] {
            refreshTranscriptionToolBar();
            updateTranscriptionActions();
        });
    connect(
        m_transcriptionController,
        &TranscriptionController::documentChanged,
        this,
        &MainWindow::updateTranscriptionActions);
    connect(
        m_transcriptionController,
        &TranscriptionController::versesChanged,
        this,
        &MainWindow::updateTranscriptionActions);

    rebuildAll();
    // States the opening position — which is Textual criticism — rather than
    // leaving it implied by the order things were built in.
    applyMode();
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

    m_downloadAction = new QAction(QStringLiteral("Download manuscripts…"), this);
    m_downloadAction->setToolTip(QStringLiteral(
        "Fetches published manuscripts and translations into your library. The "
        "only part of Milah that uses the internet."));
    connect(
        m_downloadAction,
        &QAction::triggered,
        m_controller,
        &AppController::downloadManuscripts);

    m_libraryAction = new QAction(QStringLiteral("Library…"), this);
    m_libraryAction->setToolTip(QStringLiteral(
        "Opens what you have downloaded. Milah knows which of them are "
        "translations, so they load the right way round without being asked."));
    connect(
        m_libraryAction, &QAction::triggered, m_controller, &AppController::openLibrary);

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

    m_closeAction = new QAction(QStringLiteral("Close project"), this);
    // Written out rather than QKeySequence::Close: on Windows that offers
    // Ctrl+F4 first and Ctrl+W second, and setShortcut takes only the first —
    // so the standard key would bind the MDI-child shortcut nobody expects
    // here. Same reason the quit action writes Ctrl+Q out.
    m_closeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    describeShortcut(m_closeAction);
    connect(m_closeAction, &QAction::triggered, m_controller, &AppController::closeProject);

    m_exportAction = new QAction(QStringLiteral("Export Combined"), this);
    m_exportAction->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentSaveAs,
        QStringLiteral("document-save-as"),
        windowPalette));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    describeShortcut(m_exportAction);
    connect(
        m_exportAction, &QAction::triggered, m_controller, &AppController::exportCombined);

    // No shortcut: Ctrl+E is Export Combined, and the transcription tab's Word
    // export goes without one for the same reason — a File menu with two
    // exports does not need two chords to remember.
    m_exportCollationWordAction = new QAction(QStringLiteral("Export to Word…"), this);
    m_exportCollationWordAction->setToolTip(QStringLiteral(
        "Writes the whole book — not only the chapter on screen — as a Word "
        "document: each verse a table of the witnesses one above another, the "
        "differences coloured as they are here, and the notes as footnotes."));
    connect(
        m_exportCollationWordAction,
        &QAction::triggered,
        m_controller,
        &AppController::exportCollationWord);

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

    m_saveDictionaryAction =
        new QAction(QStringLiteral("Save my dictionary as…"), this);
    m_saveDictionaryAction->setToolTip(
        QStringLiteral("Write a copy of your dictionary somewhere of your own. "
                       "Milah goes on using its own."));
    connect(
        m_saveDictionaryAction,
        &QAction::triggered,
        m_controller,
        &AppController::saveDictionaryAs);

    m_loadDictionaryAction = new QAction(QStringLiteral("Load a dictionary…"), this);
    m_loadDictionaryAction->setToolTip(
        QStringLiteral("Read a saved dictionary in. Nothing you already have is "
                       "lost, and nothing is taken twice."));
    connect(
        m_loadDictionaryAction,
        &QAction::triggered,
        m_controller,
        &AppController::loadDictionary);

    m_dictionaryAction =
        new QAction(QStringLiteral("Define word in my dictionary"), this);
    m_dictionaryAction->setToolTip(QStringLiteral(
        "Records what the selected word means, and stops Milah asking about "
        "it, in every project."));
    connect(m_dictionaryAction, &QAction::triggered, this, [this] {
        // The dictionary is one, so the action is one; which word it means is
        // whichever the mode in front has under the caret.
        const QString word = editingEdition()
            ? m_controller->selectedWord()
            : m_transcriptionController->selectedWord();
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

void MainWindow::createTranscriptionActions()
{
    const QPalette windowPalette = palette();

    m_openImageAction = new QAction(QStringLiteral("Open Image"), this);
    m_openImageAction->setIcon(
        actionIcon(QIcon::ThemeIcon::DocumentOpen, QStringLiteral("document-open"), windowPalette));
    connect(
        m_openImageAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::openImage);

    m_openScanAction = new QAction(QStringLiteral("Get online manuscript scan…"), this);
    m_openScanAction->setToolTip(QStringLiteral(
        "Transcribe from a manuscript a library has published, without "
        "downloading it first. Folios are fetched as you reach them."));
    connect(
        m_openScanAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::openOnlineScan);

    m_openTranscriptionAction =
        new QAction(QStringLiteral("Open Transcription Project"), this);
    connect(
        m_openTranscriptionAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::openTranscription);

    m_saveTranscriptionAction =
        new QAction(QStringLiteral("Save Transcription project"), this);
    m_saveTranscriptionAction->setIcon(
        actionIcon(QIcon::ThemeIcon::DocumentSave, QStringLiteral("document-save"), windowPalette));
    // The same key the edition saves with. Safe because at most one of the two
    // is ever enabled — the mode sees to that — so the shortcut is never
    // ambiguous even though two actions carry it.
    m_saveTranscriptionAction->setShortcut(QKeySequence::Save);
    describeShortcut(m_saveTranscriptionAction);
    connect(m_saveTranscriptionAction, &QAction::triggered, this, [this] {
        m_transcriptionController->saveTranscription();
    });

    m_exportOsisAction = new QAction(QStringLiteral("Export to OSIS"), this);
    m_exportOsisAction->setIcon(actionIcon(
        QIcon::ThemeIcon::DocumentSaveAs, QStringLiteral("document-save-as"), windowPalette));
    m_exportOsisAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    describeShortcut(m_exportOsisAction);
    connect(
        m_exportOsisAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::exportOsis);

    m_exportWordAction = new QAction(QStringLiteral("Export to Word…"), this);
    m_exportWordAction->setToolTip(QStringLiteral(
        "Writes two Word documents: the manuscript as a text to read, with the "
        "notes as footnotes, and the same verse by verse with the English "
        "underneath."));
    connect(
        m_exportWordAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::exportWord);

    m_addToLibraryAction = new QAction(QStringLiteral("Add to my library"), this);
    m_addToLibraryAction->setToolTip(QStringLiteral(
        "Files this transcription with the manuscripts the Textual criticism "
        "tab collates, one book to a file, so it can be read against them."));
    connect(
        m_addToLibraryAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::addToLibrary);

    m_closeTranscriptionAction =
        new QAction(QStringLiteral("Close Transcription Project"), this);
    m_closeTranscriptionAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    describeShortcut(m_closeTranscriptionAction);
    connect(
        m_closeTranscriptionAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::closeTranscription);

    m_transcriptionUndoAction = new QAction(QStringLiteral("Undo"), this);
    m_transcriptionUndoAction->setIcon(
        actionIcon(QIcon::ThemeIcon::EditUndo, QStringLiteral("edit-undo"), windowPalette));
    m_transcriptionUndoAction->setShortcut(QKeySequence::Undo);
    describeShortcut(m_transcriptionUndoAction);
    connect(m_transcriptionUndoAction, &QAction::triggered, this, &MainWindow::undo);

    m_transcriptionRedoAction = new QAction(QStringLiteral("Redo"), this);
    m_transcriptionRedoAction->setIcon(
        actionIcon(QIcon::ThemeIcon::EditRedo, QStringLiteral("edit-redo"), windowPalette));
    m_transcriptionRedoAction->setShortcut(QKeySequence::Redo);
    describeShortcut(m_transcriptionRedoAction);
    connect(m_transcriptionRedoAction, &QAction::triggered, this, &MainWindow::redo);

    m_newChapterAction = new QAction(QStringLiteral("Move verse to new chapter"), this);
    m_newChapterAction->setToolTip(QStringLiteral(
        "The verse being typed in, and every verse after it, move into the next "
        "chapter."));
    connect(m_newChapterAction, &QAction::triggered, this, [this] {
        m_transcriptionController->moveVerseToNewChapter(
            m_transcriptionController->selectedVerse());
    });

    // The chapter arrows' keys, which are free here because the chapter actions
    // carrying them are dead while a folio is on screen.
    m_previousImageAction = new QAction(QStringLiteral("Previous image"), this);
    m_previousImageAction->setIcon(actionIcon(
        QIcon::ThemeIcon::GoPrevious, QStringLiteral("go-previous"), windowPalette));
    m_previousImageAction->setShortcut(QKeySequence(QStringLiteral("Alt+Left")));
    connect(
        m_previousImageAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::goToPreviousImage);

    m_nextImageAction = new QAction(QStringLiteral("Next image"), this);
    m_nextImageAction->setIcon(
        actionIcon(QIcon::ThemeIcon::GoNext, QStringLiteral("go-next"), windowPalette));
    m_nextImageAction->setShortcut(QKeySequence(QStringLiteral("Alt+Right")));
    connect(
        m_nextImageAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::goToNextImage);

    m_magnifyAction = new QAction(QStringLiteral("Magnify"), this);
    m_magnifyAction->setIcon(appIcon(QStringLiteral("zoom-in"), windowPalette));
    m_magnifyAction->setCheckable(true);
    m_magnifyAction->setToolTip(QStringLiteral(
        "Move a magnifier over the folio. The wheel changes how much it enlarges."));

    m_transcribeAction = new QAction(QStringLiteral("Transcribe"), this);
    m_transcribeAction->setIcon(
        appIcon(QStringLiteral("text-recognise"), windowPalette));
    refreshTranscribeTooltip();

    m_overlayAction = new QAction(QStringLiteral("Show readings"), this);
    // Struck through, because it opens unchecked and the icon says what the
    // state is rather than what pressing it would do. A recognition then opens
    // it — see the recognitionApplied connection — and it stays wherever the
    // transcriber last put it until the next one.
    m_overlayAction->setIcon(appIcon(QStringLiteral("eye-off"), windowPalette));
    m_overlayAction->setCheckable(true);
    m_overlayAction->setToolTip(QStringLiteral(
        "Draws each recognised word over the ink it was read from, so you can "
        "see at a glance what was read where."));

    m_lineBoxesAction = new QAction(QStringLiteral("Show lines"), this);
    // Word boxes are the state it opens in, so the icon shows those — the same
    // bargain the eye makes, where the picture is what you are looking at rather
    // than what pressing it would give you.
    m_lineBoxesAction->setIcon(appIcon(QStringLiteral("boxes-word"), windowPalette));
    m_lineBoxesAction->setCheckable(true);
    m_lineBoxesAction->setToolTip(QStringLiteral(
        "Draws one box per line of the manuscript instead of one per word, with "
        "the line's whole reading written underneath the ink it was laid onto and "
        "the baseline the training strip is cut along.<p>What it is for: on a hand "
        "the recogniser was not trained for it groups the ink into lines wrongly — "
        "running two lines together, or cutting one in two — and nothing in the "
        "word boxes shows that, because each box looks right on its own. "
        "Right-click a line to say where it really ends, or to join it to the one "
        "below.</p><p>Read a line against the ink and press Space to accept it "
        "and move to the next — the number beside each line says how many of its "
        "words are still unread. Enter says the line ends before the selected "
        "word; Backspace pulls a line's first word up onto the line above.</p>"));

    // Through a lambda, not straight at the slot: triggered carries a bool, and
    // both of these now take an argument that a bool would quietly become —
    // a window pointer in one case and a line number in the other.
    connect(m_transcribeAction, &QAction::triggered, this, [this] {
        m_transcriptionController->transcribeFolio();
    });

    m_htrInstallAction = new QAction(this);
    connect(
        m_htrInstallAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::setUpKraken);

    m_manageModelsAction = new QAction(QStringLiteral("Manage models…"), this);
    m_manageModelsAction->setToolTip(QStringLiteral(
        "Add, remove, and choose which recognition model Transcribe runs."));
    connect(
        m_manageModelsAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::manageModels);

    m_lastRunAction = new QAction(QStringLiteral("Show last recognition…"), this);
    m_lastRunAction->setToolTip(QStringLiteral(
        "What Milah ran, what Kraken said, and the layout file that came back."));
    connect(
        m_lastRunAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::showLastRecognition);

    m_previewStripsAction =
        new QAction(QStringLiteral("See what training will be shown…"), this);
    connect(
        m_previewStripsAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::previewTrainingStrips);

    m_saveForTrainingAction =
        new QAction(QStringLiteral("Save this folio for HTR training"), this);
    connect(
        m_saveForTrainingAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::saveFolioForTraining);

    m_trainAction = new QAction(QStringLiteral("Train a model…"), this);
    connect(
        m_trainAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::showTraining);

    // No menu entry for the fill. Where a published transcription starts on the
    // leaf is a thing you point at, so the folio's own right-click is the whole
    // of it — a menu entry could only ever offer the same job with the pointing
    // left out.

    m_recoverReadingsAction =
        new QAction(QStringLiteral("Recover the machine's readings"), this);
    m_recoverReadingsAction->setToolTip(QStringLiteral(
        "Reads this folio again to fill in what the machine read at each box, "
        "and changes nothing else. For a folio read before Milah kept them, "
        "where holding a box out of the work has nothing to put back."));
    connect(
        m_recoverReadingsAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::recoverReadings);

    m_htrRemoveAction = new QAction(QStringLiteral("Remove Kraken…"), this);
    m_htrRemoveAction->setToolTip(QStringLiteral(
        "Deletes Kraken, its Python environment and its models. Your Linux "
        "distribution is left alone."));
    connect(
        m_htrRemoveAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::removeKraken);

    m_importLayoutAction =
        new QAction(QStringLiteral("Import recognised layout…"), this);
    m_importLayoutAction->setToolTip(QStringLiteral(
        "Reads an ALTO or PAGE file made elsewhere — by an institution's own "
        "eScriptorium, or on another machine — onto this folio."));
    connect(
        m_importLayoutAction,
        &QAction::triggered,
        m_transcriptionController,
        &TranscriptionController::importRecognisedLayout);
}

void MainWindow::buildMenuBar()
{
    // Five menus, made once. Which two the bar carries is the mode's business;
    // what is in them is not, so it is written out here and never touched
    // again. The menus are parented to the window rather than to the bar
    // because they come and go from it, and a menu the bar is not holding still
    // has to exist to go back in.
    //
    // The menus carry the very same QAction objects the toolbars do, so a
    // shortcut, an icon and an enabled state are each stated once and the two
    // cannot drift apart.
    m_editionFileMenu = new QMenu(QStringLiteral("&File"), this);
    m_editionFileMenu->addAction(m_openAction);
    m_openRecentMenu = m_editionFileMenu->addMenu(QStringLiteral("Open &Recent"));
    connect(m_openRecentMenu, &QMenu::aboutToShow, this, [this] {
        fillRecentMenu(
            m_openRecentMenu, QLatin1String(RecentProjectsKey), [this](const QString &path) {
                m_controller->openRecentProject(path);
            });
    });
    // Whether there is anything to offer is settled as the File menu opens
    // rather than by the update*Actions family: those run on a mode switch, and
    // the first project ever saved would otherwise leave this greyed out until
    // the reader happened to change tabs.
    connect(m_editionFileMenu, &QMenu::aboutToShow, this, [this] {
        m_openRecentMenu->menuAction()->setEnabled(
            !recentFiles(QLatin1String(RecentProjectsKey)).isEmpty());
    });
    m_editionFileMenu->addAction(m_saveAction);
    m_editionFileMenu->addAction(m_closeAction);
    m_editionFileMenu->addSeparator();
    // In the order they are used: fetch into the library, load from it, then
    // load from anywhere else.
    m_editionFileMenu->addAction(m_downloadAction);
    m_editionFileMenu->addAction(m_libraryAction);
    m_editionFileMenu->addAction(m_loadManuscriptsAction);
    m_editionFileMenu->addAction(m_loadTranslationsAction);
    m_editionFileMenu->addSeparator();
    m_editionFileMenu->addAction(m_exportAction);
    m_editionFileMenu->addAction(m_exportCollationWordAction);
    m_editionFileMenu->addSeparator();
    m_editionFileMenu->addAction(m_saveDictionaryAction);
    m_editionFileMenu->addAction(m_loadDictionaryAction);
    m_editionFileMenu->addSeparator();
    m_editionFileMenu->addAction(m_quitAction);
    updateDictionaryActions();

    m_editionEditMenu = new QMenu(QStringLiteral("&Edit"), this);
    m_editionEditMenu->addAction(m_undoAction);
    m_editionEditMenu->addAction(m_redoAction);
    m_editionEditMenu->addSeparator();
    m_editionEditMenu->addAction(m_splitAction);
    m_editionEditMenu->addAction(m_mergePreviousAction);
    m_editionEditMenu->addAction(m_mergeNextAction);
    m_editionEditMenu->addSeparator();
    m_editionEditMenu->addAction(m_dictionaryAction);

    m_transcriptionFileMenu = new QMenu(QStringLiteral("&File"), this);
    m_transcriptionFileMenu->addAction(m_openImageAction);
    m_transcriptionFileMenu->addAction(m_openScanAction);
    m_transcriptionFileMenu->addAction(m_openTranscriptionAction);
    m_openRecentTranscriptionMenu =
        m_transcriptionFileMenu->addMenu(QStringLiteral("Open &Recent"));
    connect(m_openRecentTranscriptionMenu, &QMenu::aboutToShow, this, [this] {
        fillRecentMenu(
            m_openRecentTranscriptionMenu,
            QLatin1String(RecentTranscriptionsKey),
            [this](const QString &path) {
                m_transcriptionController->openRecentTranscription(path);
            });
    });
    connect(m_transcriptionFileMenu, &QMenu::aboutToShow, this, [this] {
        m_openRecentTranscriptionMenu->menuAction()->setEnabled(
            !recentFiles(QLatin1String(RecentTranscriptionsKey)).isEmpty());
    });
    m_transcriptionFileMenu->addAction(m_saveTranscriptionAction);
    m_transcriptionFileMenu->addSeparator();
    m_transcriptionFileMenu->addAction(m_importLayoutAction);

    // Setting kraken up and taking it away again. Filled from its own
    // aboutToShow, the way Open Recent is, so the two entries are never
    // offering something the machine has stopped agreeing with — and so that
    // asking the machine, which on Windows costs a wsl.exe launch, happens when
    // somebody opens the menu rather than when Milah starts.
    m_htrMenu =
        m_transcriptionFileMenu->addMenu(QStringLiteral("&Handwriting recognition"));
    // The models first, because they are what changes: a new hand means a new
    // model, where Kraken is installed once and then forgotten about.
    m_modelMenu = m_htrMenu->addMenu(QStringLiteral("&Use model"));
    connect(m_modelMenu, &QMenu::aboutToShow, this, [this] {
        fillModelMenu(m_modelMenu);
    });
    m_htrMenu->addAction(m_manageModelsAction);
    m_htrMenu->addSeparator();
    m_htrMenu->addAction(m_lastRunAction);
    m_htrMenu->addSeparator();
    m_htrMenu->addAction(m_recoverReadingsAction);
    m_htrMenu->addAction(m_previewStripsAction);
    m_htrMenu->addAction(m_saveForTrainingAction);
    m_htrMenu->addAction(m_trainAction);
    m_htrMenu->addSeparator();
    m_htrMenu->addAction(m_htrInstallAction);
    m_htrMenu->addAction(m_htrRemoveAction);

    connect(m_htrMenu, &QMenu::aboutToShow, this, [this] {
        const KrakenEnvironment::State state = m_transcriptionController->krakenState();
        // "Install WSL2 and Kraken…" where WSL2 is missing, the same rule the
        // question itself follows, so the menu never promises less than it will
        // do.
        m_htrInstallAction->setText(
            state == KrakenEnvironment::State::NoSubsystem
                ? QStringLiteral("Install WSL2 and Kraken…")
                : QStringLiteral("Install Kraken…"));

        // Offered when Kraken is *absent*, not merely when the environment is
        // short of Ready. NoModel means Kraken is installed and a model is
        // wanted, and the answer to a missing model is a model — reinstalling
        // the runtime would be an odd thing to suggest and, when this action
        // also stood for "choose a model", an actively misleading one.
        const bool installed = state == KrakenEnvironment::State::NoModel
            || state == KrakenEnvironment::State::Ready;
        m_htrInstallAction->setEnabled(!installed);
        m_htrRemoveAction->setEnabled(m_transcriptionController->krakenInstalled());

        // Models need Kraken to download and verify them, so this waits on the
        // runtime — and says why rather than being mysteriously grey.
        m_manageModelsAction->setEnabled(installed);
        m_manageModelsAction->setToolTip(
            installed ? QStringLiteral("Add, remove, and choose which recognition "
                                       "model Transcribe runs.")
                      : QStringLiteral("Install Kraken first — models are "
                                       "downloaded and checked with it."));

        // Nothing to show before the first run, and saying so by being grey is
        // kinder than a window with three empty tabs in it.
        m_lastRunAction->setEnabled(HtrLastRunDialog::hasRun());

        // Nothing to teach a recogniser until a line has been checked all the
        // way through. Asked here rather than kept up to date because the
        // answer reads the folio, and a menu is opened far less often than a
        // word is committed.
        // Only where something was read: there is nothing to recover on a folio
        // nobody has run a recogniser over.
        m_recoverReadingsAction->setEnabled(
            m_transcriptionController->hasRecognisedWords());

        const int lines = m_transcriptionController->trainableLineCount();
        // The same gate the save has, because it previews the save. Its own
        // tooltip, though: the two answer different questions about the same
        // lines, and an entry that reads like its neighbour is one nobody
        // presses.
        m_previewStripsAction->setEnabled(lines > 0);
        m_previewStripsAction->setToolTip(
            lines > 0
                ? QStringLiteral("Cuts this folio's %1 finished line(s) the way "
                                 "training cuts them — straightened along the "
                                 "baseline and masked to the outline — and shows "
                                 "you the pictures a model would be taught from, "
                                 "with the lines Kraken refuses and why.")
                      .arg(lines)
                : QStringLiteral("No line of this folio has been checked all the "
                                 "way through yet, so there is nothing training "
                                 "would be shown."));
        m_saveForTrainingAction->setEnabled(lines > 0);
        m_saveForTrainingAction->setToolTip(
            lines > 0
                ? QStringLiteral("Puts this folio's %1 finished line(s) into this "
                                 "manuscript's training set.")
                      .arg(lines)
                : QStringLiteral("No line of this folio has been checked all the way "
                                 "through yet — a line counts once every word on it "
                                 "has been looked at."));

        // And nothing worth hours of processor until enough has been gathered.
        // The best set decides, because that is the one that would be trained
        // on; the dialog says the rest.
        int best = 0;
        QString bestLabel;
        for (const TrainingSet::Set &set : TrainingSet::known()) {
            if (set.lines > best) {
                best = set.lines;
                bestLabel = set.label;
            }
        }
        m_trainAction->setEnabled(best >= TrainingSet::EnoughLines);
        m_trainAction->setToolTip(
            best >= TrainingSet::EnoughLines
                ? QStringLiteral("Teaches a model this hand, from what you have "
                                 "saved. Hours, and worth them.")
                : best == 0
                ? QStringLiteral("Nothing saved for training yet.")
                : QStringLiteral("%1 has %2 of the %3 lines training needs.")
                      .arg(bestLabel)
                      .arg(best)
                      .arg(TrainingSet::EnoughLines));
    });

    m_transcriptionFileMenu->addSeparator();
    m_transcriptionFileMenu->addAction(m_exportOsisAction);
    m_transcriptionFileMenu->addAction(m_exportWordAction);
    m_transcriptionFileMenu->addAction(m_addToLibraryAction);
    m_transcriptionFileMenu->addAction(m_closeTranscriptionAction);
    m_transcriptionFileMenu->addSeparator();
    // The same object the edition's File menu offers, so Quit means one thing:
    // close the window, which is where the unsaved-work question lives.
    m_transcriptionFileMenu->addAction(m_quitAction);

    m_transcriptionEditMenu = new QMenu(QStringLiteral("&Edit"), this);
    m_transcriptionEditMenu->addAction(m_transcriptionUndoAction);
    m_transcriptionEditMenu->addAction(m_transcriptionRedoAction);
    m_transcriptionEditMenu->addSeparator();
    m_transcriptionEditMenu->addAction(m_newChapterAction);
    m_transcriptionEditMenu->addSeparator();
    // The dictionary is the editor's rather than the edition's, so defining a
    // word is offered on both sides.
    m_transcriptionEditMenu->addAction(m_dictionaryAction);

    // About is not a mode's business, so it stays in the bar throughout and the
    // mode's two menus are inserted before it.
    m_aboutMenu = new QMenu(QStringLiteral("&About"), this);
    m_aboutMenu->addAction(m_aboutAction);
    menuBar()->addMenu(m_aboutMenu);

    updateSelectionActions();
}

void MainWindow::fillRecentMenu(
    QMenu *menu, const QString &key, const std::function<void(const QString &)> &open)
{
    menu->clear();

    const QStringList paths = recentFiles(key);
    // A file's own name is what a reader recognises; the folder only where two
    // of them read alike. Both come from one place so the menu and the reason
    // for it stay together.
    const QStringList labels = recentFileLabels(paths);

    // The path is worth showing and too long to be a label, so it goes on the
    // tooltip — which a QMenu does not show unless asked.
    menu->setToolTipsVisible(true);

    for (int index = 0; index < paths.size(); ++index) {
        const QString &path = paths.at(index);
        // Numbered, so the list can be walked by keyboard while it is open. A
        // real shortcut would be wrong: a shortcut is armed by its action
        // whether or not its menu is in the bar, so five of them would open
        // editions from inside the transcription tab.
        auto *entry = menu->addAction(
            QStringLiteral("&%1  %2").arg(index + 1).arg(labels.value(index)));
        const QString native = QDir::toNativeSeparators(path);

        if (QFileInfo::exists(path)) {
            entry->setToolTip(native);
            connect(entry, &QAction::triggered, this, [open, path] { open(path); });
            continue;
        }
        // Still listed, and plainly not available. A transcription on a drive
        // that is not plugged in has not been abandoned — hiding it would say it
        // had, and offering it would waste a click on a file that cannot open.
        entry->setEnabled(false);
        entry->setToolTip(QStringLiteral("Not there at the moment: %1").arg(native));
    }

    if (!paths.isEmpty()) {
        menu->addSeparator();
        auto *forget = menu->addAction(QStringLiteral("&Clear list"));
        connect(forget, &QAction::triggered, this, [key] { clearRecentFiles(key); });
    }
}

void MainWindow::buildModeTabs()
{
    m_modeTabs = new MenuRowTabBar;
    m_modeTabs->setObjectName(QStringLiteral("modeTabs"));
    m_modeTabs->setDocumentMode(true); // No frame of its own.
    m_modeTabs->setDrawBase(false);    // No base line running under the menus.
    m_modeTabs->setExpanding(false);
    m_modeTabs->setUsesScrollButtons(false);
    // Left and Right inside the menu bar belong to the menus, and the tab chain
    // belongs to whatever is being typed in.
    m_modeTabs->setFocusPolicy(Qt::NoFocus);
    m_modeTabs->addTab(QStringLiteral("Textual criticism"));
    m_modeTabs->addTab(QStringLiteral("Transcription"));
    m_modeTabs->setTabToolTip(
        0, QStringLiteral("Compare manuscripts and build an edition (Ctrl+1)"));
    m_modeTabs->setTabToolTip(
        1, QStringLiteral("Read a folio and transcribe it (Ctrl+2)"));

    // The box is stated rather than left to the style's tab metrics, which are
    // built for a tab strip above a pane and are far taller than a menu row.
    // The height is capped in MenuRowTabBar as well, because QMenuBar reads its
    // corner widget's size hint and grows to it.
    //
    // The unchosen tab is drawn in the same muted ink the row labels use — a
    // fraction of the text colour, so it holds up in a light and a dark palette
    // alike, which palette(mid) does not: on a dark one it sinks into the bar.
    // The chosen tab is told apart by its underline more than by its colour.
    //
    // Nothing under :selected may change a tab's width — a bolder face would
    // shift the pair sideways every time the mode changed, and they are pinned
    // to the right-hand edge where that would be plain to see.
    m_modeTabs->setStyleSheet(QStringLiteral(R"CSS(
QTabBar#modeTabs { background: transparent; }
QTabBar#modeTabs::tab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 0px 12px;
    margin: 0px;
    color: %1;
}
QTabBar#modeTabs::tab:hover { color: palette(text); }
QTabBar#modeTabs::tab:selected {
    color: palette(text);
    border-bottom: 2px solid palette(highlight);
}
)CSS")
                                  .arg(acronymColor(palette())));

    connect(m_modeTabs, &QTabBar::currentChanged, this, [this](int index) {
        setMode(index == 0 ? Mode::TextualCriticism : Mode::Transcription);
    });
    menuBar()->setCornerWidget(m_modeTabs, Qt::TopRightCorner);

    // The tabs sit outside the menu bar's arrow-key walk and, by NoFocus,
    // outside the tab chain, so the keyboard needs its own way in. addAction is
    // not optional here: an action merely parented to a widget has no
    // associated widget of its own, and its shortcut never fires.
    auto *toEdition = new QAction(this);
    toEdition->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(toEdition, &QAction::triggered, this, [this] {
        setMode(Mode::TextualCriticism);
    });
    addAction(toEdition);

    auto *toTranscription = new QAction(this);
    toTranscription->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(toTranscription, &QAction::triggered, this, [this] {
        setMode(Mode::Transcription);
    });
    addAction(toTranscription);
}

void MainWindow::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    applyMode();
}

void MainWindow::applyMode()
{
    const bool editing = editingEdition();

    // The tabs may be what asked for this, or may not — a shortcut and the
    // opening position both come through here — so they are told either way,
    // silently, rather than being allowed to ask again.
    {
        const QSignalBlocker blocker(m_modeTabs);
        m_modeTabs->setCurrentIndex(editing ? 0 : 1);
    }

    // Taken out before the other goes in, and both inserted before About, so
    // the bar always reads File, Edit, About and never briefly holds two menus
    // called the same thing — a menu bar grabs Alt+F from every title it
    // carries, and two of them would make the key mean nothing. removeAction on
    // an action the bar does not hold is a no-op, so this states the opening
    // position as well as every change.
    QMenu *const outFile = editing ? m_transcriptionFileMenu : m_editionFileMenu;
    QMenu *const outEdit = editing ? m_transcriptionEditMenu : m_editionEditMenu;
    QMenu *const inFile = editing ? m_editionFileMenu : m_transcriptionFileMenu;
    QMenu *const inEdit = editing ? m_editionEditMenu : m_transcriptionEditMenu;
    menuBar()->removeAction(outFile->menuAction());
    menuBar()->removeAction(outEdit->menuAction());
    menuBar()->insertMenu(m_aboutMenu->menuAction(), inFile);
    menuBar()->insertMenu(m_aboutMenu->menuAction(), inEdit);

    // Each mode's toolbar and panel say nothing about the other's work, so they
    // go away entirely rather than sitting there greyed. What the reader had
    // done with a dock is remembered, so it comes back as they left it and not
    // as Milah first drew it.
    //
    // Only on a real switch: the first time through, the window has not been
    // shown yet and both docks report themselves hidden, which would record the
    // panel this mode is not showing as one the reader had closed — and it
    // would then never open.
    if (m_modeApplied) {
        if (editing) {
            m_metadataDockWasVisible = m_metadataDock->isVisible();
        } else {
            m_sourcesDockWasVisible = m_sourcesDock->isVisible();
        }
    }
    m_modeApplied = true;
    m_toolBar->setVisible(editing);
    m_sourcesDock->setVisible(editing && m_sourcesDockWasVisible);
    m_transcriptionToolBar->setVisible(!editing);
    m_metadataDock->setVisible(!editing && m_metadataDockWasVisible);

    m_pages->setCurrentWidget(
        editing ? static_cast<QWidget *>(m_verseArea)
                : static_cast<QWidget *>(m_transcription));

    // Every enabled state asked again rather than remembered: the refreshers
    // work them out from the work itself, so coming back is a question and not
    // a restore, and nothing can go stale in between.
    updateSourceActions();
    updateProjectActions();
    updateNavigationActions();
    updateHistoryActions();
    updateDictionaryActions();
    updateSelectionActions();
    updateTranscriptionActions();
}

void MainWindow::updateSourceActions()
{
    const bool editing = editingEdition();
    m_openAction->setEnabled(editing);
    m_downloadAction->setEnabled(editing);
    m_libraryAction->setEnabled(editing);
    m_loadManuscriptsAction->setEnabled(editing);
    m_loadTranslationsAction->setEnabled(editing);
    m_loadDictionaryAction->setEnabled(editing);
}

void MainWindow::updateProjectActions()
{
    const bool ready = editingEdition() && !m_controller->manuscripts().isEmpty();
    m_regenerateAction->setEnabled(ready);
    m_saveAction->setEnabled(ready);
    m_closeAction->setEnabled(ready);
    m_exportAction->setEnabled(ready);
    m_exportCollationWordAction->setEnabled(ready);
}

void MainWindow::updateNavigationActions()
{
    const std::optional<Location> current = m_controller->location();
    const int index =
        current.has_value() ? m_controller->indexOfLocation(*current) : -1;
    const int count = m_controller->locations().size();
    const bool editing = editingEdition();
    m_previousAction->setEnabled(editing && index > 0);
    m_nextAction->setEnabled(editing && index >= 0 && index < count - 1);
}

void MainWindow::updateTranscriptionActions()
{
    const bool transcribing = !editingEdition();
    const bool open = m_transcriptionController->hasDocument();

    m_openImageAction->setEnabled(transcribing);
    m_openScanAction->setEnabled(transcribing);
    m_openTranscriptionAction->setEnabled(transcribing);
    m_saveTranscriptionAction->setEnabled(transcribing && open);
    m_exportOsisAction->setEnabled(transcribing && open);
    m_exportWordAction->setEnabled(transcribing && open);
    m_addToLibraryAction->setEnabled(transcribing && open);
    m_closeTranscriptionAction->setEnabled(transcribing && open);
    m_magnifyAction->setEnabled(transcribing && open);
    m_transcribeAction->setEnabled(transcribing && open);
    // Cheap — two QSettings reads — and this is where every path that could
    // have changed the model already passes: the setup dialog reports a
    // document change on its way out.
    refreshTranscribeTooltip();
    m_importLayoutAction->setEnabled(transcribing && open);
    // Only when there is something to draw. An eye that toggles nothing is
    // worse than a grey one: it says the folio has readings on it and then
    // shows none, which reads as a recogniser that failed silently.
    m_overlayAction->setEnabled(
        transcribing && m_transcriptionController->hasRecognisedWords());
    // And only while the overlay is on: a switch between two views of something
    // that is not being drawn has nothing to switch.
    m_lineBoxesAction->setEnabled(
        transcribing && m_overlayAction->isChecked()
        && m_transcriptionController->hasRecognisedLines());

    m_transcriptionUndoAction->setEnabled(
        transcribing && m_transcriptionController->canUndo());
    m_transcriptionRedoAction->setEnabled(
        transcribing && m_transcriptionController->canRedo());
    m_newChapterAction->setEnabled(
        transcribing && m_transcriptionController->selectedVerse() >= 0);

    m_previousImageAction->setEnabled(
        transcribing && !m_transcriptionController->previousImageName().isEmpty());
    m_nextImageAction->setEnabled(
        transcribing && !m_transcriptionController->nextImageName().isEmpty());
    // The arrows name the folio they lead to, because the file names are the
    // only order a folder of scans has.
    const QString previous = m_transcriptionController->previousImageName();
    const QString next = m_transcriptionController->nextImageName();
    m_previousImageAction->setToolTip(previous.isEmpty()
        ? QStringLiteral("This is the first image in the folder")
        : QStringLiteral("Back to %1 (Alt+Left)").arg(previous));
    m_nextImageAction->setToolTip(next.isEmpty()
        ? QStringLiteral("This is the last image in the folder")
        : QStringLiteral("On to %1 (Alt+Right)").arg(next));

    m_bookField->setEnabled(transcribing && open);
    m_chapterField->setEnabled(transcribing && open);
}

void MainWindow::updateSelectionActions()
{
    const WordSelection selected = m_controller->selection();
    const QString word = m_controller->selectedWord();
    const bool editing = editingEdition();

    // Dividing is offered only where there is something to divide at, the same
    // question the context menu asks.
    m_splitAction->setEnabled(
        editing && selected.isValid() && dividedWords(word).size() > 1);
    m_mergePreviousAction->setEnabled(
        editing && selected.isValid()
        && m_controller->canMergeWithPrevious(selected.verseId, selected.columnIndex));
    m_mergeNextAction->setEnabled(
        editing && selected.isValid()
        && m_controller->canMergeWithNext(selected.verseId, selected.columnIndex));
    // Defining a word is offered on both sides, on whichever word the mode in
    // front has under the caret.
    m_dictionaryAction->setEnabled(editing
        ? !word.isEmpty()
        : !m_transcriptionController->selectedWord().isEmpty());
}

void MainWindow::buildToolBar()
{
    auto *toolBar = addToolBar(QStringLiteral("Main"));
    m_toolBar = toolBar;
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
    showArrowWithWord(toolBar, m_previousAction, QStringLiteral("prev"));
    toolBar->addAction(m_nextAction);
    showArrowWithWord(toolBar, m_nextAction, QStringLiteral("next"));

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

void MainWindow::fillModelMenu(QMenu *menu)
{
    menu->clear();

    // Set here as well as in the Handwriting recognition menu, because the
    // toolbar arrow reaches this without going through that one and would
    // otherwise show whatever state was last left behind.
    const KrakenEnvironment::State state = m_transcriptionController->krakenState();
    m_manageModelsAction->setEnabled(state == KrakenEnvironment::State::NoModel
                                     || state == KrakenEnvironment::State::Ready);

    const QString active = KrakenEnvironment::modelPath();
    const QList<KrakenEnvironment::InstalledModel> models =
        KrakenEnvironment::installedModels();

    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    for (const KrakenEnvironment::InstalledModel &model : models) {
        QAction *entry = menu->addAction(model.label);
        entry->setCheckable(true);
        entry->setChecked(model.path == active);
        entry->setToolTip(model.path);
        group->addAction(entry);
        connect(entry, &QAction::triggered, this, [this, path = model.path] {
            KrakenEnvironment::setModelPath(path);
            refreshTranscribeTooltip();
        });
    }

    if (models.isEmpty()) {
        // Not an empty menu. A menu with nothing in it says the feature is
        // broken; a menu saying there is nothing yet says what to do about it.
        QAction *none = menu->addAction(QStringLiteral("No models installed"));
        none->setEnabled(false);
    }

    // Only on the toolbar arrow, where this menu is the whole of what is behind
    // the button and there would otherwise be no way out of it. Under File ▸
    // Handwriting recognition the parent menu already carries Manage models…
    // one line above, and a submenu repeating its parent only makes the reader
    // wonder whether the two do different things.
    if (menu == m_toolbarModelMenu) {
        menu->addSeparator();
        menu->addAction(m_manageModelsAction);
    }
}

void MainWindow::refreshTranscribeTooltip()
{
    const QString active = KrakenEnvironment::modelPath();
    QString name;
    for (const KrakenEnvironment::InstalledModel &model :
         KrakenEnvironment::installedModels()) {
        if (model.path == active) {
            name = model.label;
            break;
        }
    }

    const QString explanation = QStringLiteral(
        "Reads this folio with a handwriting recogniser. What it reads arrives "
        "unchecked, for you to correct.");
    m_transcribeAction->setToolTip(
        name.isEmpty()
            ? explanation
            : QStringLiteral("%1\n\nUsing: %2").arg(explanation, name));
}

void MainWindow::buildTranscriptionToolBar()
{
    auto *toolBar = addToolBar(QStringLiteral("Transcription"));
    m_transcriptionToolBar = toolBar;
    toolBar->setObjectName(QStringLiteral("transcriptionToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    toolBar->addWidget(new QLabel(QStringLiteral(" Book ")));

    // Typed rather than chosen: a transcriber meets works Milah has never heard
    // of, and a list would refuse a folio the canon does not contain. The
    // completer assists without constraining.
    m_bookField = new QLineEdit;
    m_bookField->setPlaceholderText(QStringLiteral("Revelation"));
    m_bookField->setToolTip(QStringLiteral(
        "The book on this folio, by name — Genesis, Matthew, Revelation. What "
        "it is abbreviated to is shown beside it."));
    // A QLineEdit expands by default, and in a toolbar that means taking every
    // pixel the other controls have not claimed. Wide enough for the longest
    // book there is and no wider — measured rather than guessed, so a larger
    // interface font or a higher display scale does not clip it.
    m_bookField->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_bookField->setFixedWidth(fieldWidthFor(m_bookField, bookNames()));

    auto *completer = new QCompleter(bookNames(), m_bookField);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    // Contains rather than starts-with, so that "Chr" reaches both books of
    // Chronicles — their names open with a digit, which nobody types first.
    completer->setFilterMode(Qt::MatchContains);
    m_bookField->setCompleter(completer);
    connect(m_bookField, &QLineEdit::textEdited, this, &MainWindow::autofillBook);
    connect(m_bookField, &QLineEdit::editingFinished, this, &MainWindow::commitBook);
    toolBar->addWidget(m_bookField);

    toolBar->addWidget(new QLabel(QStringLiteral(" as ")));

    // What the verses will actually be addressed by. Derived and read-only for
    // a book the canon knows; the transcriber's own to write for anything else,
    // since nobody but them can say what an apocryphal work should be called.
    m_bookAcronymField = new QLineEdit;
    m_bookAcronymField->setAlignment(Qt::AlignCenter);
    m_bookAcronymField->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_bookAcronymField->setFixedWidth(fieldWidthFor(m_bookAcronymField, bookIds()));
    connect(m_bookAcronymField, &QLineEdit::editingFinished, this, [this] {
        commitBookAcronym();
    });
    toolBar->addWidget(m_bookAcronymField);

    toolBar->addWidget(new QLabel(QStringLiteral(" Chapter ")));

    m_chapterField = new QLineEdit;
    m_chapterField->setMaximumWidth(70);
    m_chapterField->setValidator(new QIntValidator(1, 999, m_chapterField));
    m_chapterField->setToolTip(QStringLiteral(
        "The chapter this folio opens in. Where a chapter begins partway down "
        "it, right-click that verse's number instead."));
    connect(m_chapterField, &QLineEdit::editingFinished, this, [this] {
        m_transcriptionController->setFirstChapter(m_chapterField->text().toInt());
    });
    toolBar->addWidget(m_chapterField);

    toolBar->addAction(m_previousImageAction);
    showArrowWithWord(toolBar, m_previousImageAction, QStringLiteral("prev"));
    toolBar->addAction(m_nextImageAction);
    showArrowWithWord(toolBar, m_nextImageAction, QStringLiteral("next"));

    toolBar->addSeparator();
    toolBar->addAction(m_magnifyAction);

    toolBar->addAction(m_transcribeAction);
    // Pressing the button transcribes; pressing its arrow says with what. The
    // choice belongs beside the thing it changes, which is this button and not
    // a dialog two menus away.
    if (auto *button = qobject_cast<QToolButton *>(
            toolBar->widgetForAction(m_transcribeAction))) {
        m_toolbarModelMenu = new QMenu(button);
        connect(m_toolbarModelMenu, &QMenu::aboutToShow, this, [this] {
            fillModelMenu(m_toolbarModelMenu);
        });
        button->setMenu(m_toolbarModelMenu);
        button->setPopupMode(QToolButton::MenuButtonPopup);
    }

    toolBar->addAction(m_overlayAction);
    toolBar->addAction(m_lineBoxesAction);
    showIconOnly(toolBar, m_overlayAction);
    // The magnifier is wired to the workspace in the constructor rather than
    // here: the toolbar is built before the page it acts on exists, and a
    // connection to a receiver that is still null is quietly dropped.

    refreshTranscriptionToolBar();
}

void MainWindow::autofillBook(const QString &typed)
{
    // Qt has no mode that pops up a list and fills the box at once, so the list
    // is the completer's and the filling is done here.
    const QString previous = m_bookTyped;
    m_bookTyped = typed;

    // Not while deleting. Backspace shortens the text, and putting back what was
    // just removed would make the field impossible to clear.
    if (previous.startsWith(typed)) {
        return;
    }
    // Not from the middle of a word either: an insertion before the end is a
    // correction, not the start of a name.
    if (typed.isEmpty() || m_bookField->cursorPosition() != typed.size()) {
        return;
    }

    QCompleter *completer = m_bookField->completer();
    if (!completer) {
        return;
    }
    completer->setCompletionPrefix(typed);

    // The completer matches anywhere in a name, so its first answer need not
    // begin with what was typed — and only something that does can be filled in
    // ahead of the caret.
    for (int index = 0; index < completer->completionCount(); ++index) {
        completer->setCurrentRow(index);
        const QString candidate = completer->currentCompletion();
        if (!candidate.startsWith(typed, Qt::CaseInsensitive)) {
            continue;
        }
        const QSignalBlocker blocker(m_bookField);
        m_bookField->setText(candidate);
        // The part they did not type is left selected, so carrying on typing
        // replaces it and the suggestion never has to be deleted.
        m_bookField->setSelection(typed.size(), candidate.size() - typed.size());
        m_bookTyped = candidate;
        return;
    }
}

void MainWindow::commitBook()
{
    const QString typed = m_bookField->text().trimmed();
    m_bookTyped = typed;
    if (typed.isEmpty()) {
        m_transcriptionController->setBook(QString(), QString());
        return;
    }

    const QString id = bookIdFor(typed);
    if (!id.isEmpty()) {
        // A book the canon knows names itself: whatever spelling got them here,
        // the field settles on the canonical one and the id follows from it.
        m_transcriptionController->setBook(id, QString());
        return;
    }

    // Outside the canon. What they wrote is the name, and the abbreviation is
    // now theirs to write — seeded from the name so there is something valid to
    // export with, and left editable so they can shorten it.
    const TranscribedPage *page = m_transcriptionController->currentPage();
    const bool alreadyCoined =
        page && !page->bookLabel.isEmpty() && bookIdFor(page->bookLabel).isEmpty();
    const QString id2 =
        alreadyCoined && page->bookLabel == typed ? page->book : sanitisedBookId(typed);
    m_transcriptionController->setBook(id2, typed);
}

void MainWindow::commitBookAcronym()
{
    if (m_bookAcronymField->isReadOnly()) {
        return;
    }
    const TranscribedPage *page = m_transcriptionController->currentPage();
    if (!page) {
        return;
    }
    const QString raw = m_bookAcronymField->text().trimmed();
    if (raw.isEmpty()) {
        return;
    }
    const QString id = sanitisedBookId(raw);
    if (id != raw) {
        statusBar()->showMessage(
            QStringLiteral("A book is written as %1: a verse is addressed as "
                           "book, chapter and number separated by dots, so the "
                           "book itself cannot carry one.")
                .arg(id));
    }
    m_transcriptionController->setBook(id, page->bookLabel);
}

void MainWindow::refreshTranscriptionToolBar()
{
    const TranscribedPage *page = m_transcriptionController->currentPage();

    // Blocked because filling a field is not an edit: without this, showing a
    // folio would write its own book back into the document as though the
    // transcriber had typed it.
    const QSignalBlocker blockBook(m_bookField);
    const QSignalBlocker blockAcronym(m_bookAcronymField);
    const QSignalBlocker blockChapter(m_chapterField);

    if (!page) {
        m_bookField->clear();
        m_bookAcronymField->clear();
        m_chapterField->clear();
        m_bookTyped.clear();
        return;
    }

    // The name as the transcriber wrote it, or the canonical one for the id
    // where they never had to write anything — which is also what a file
    // written before the book could be named reads back as.
    const QString label =
        page->bookLabel.isEmpty() ? bookName(page->book) : page->bookLabel;
    m_bookField->setText(label);
    m_bookTyped = label;

    m_bookAcronymField->setText(page->book);

    // Derived and untouchable for a book the canon knows; the transcriber's own
    // for anything else.
    const bool canonical = !page->book.isEmpty() && !bookIdFor(label).isEmpty();
    m_bookAcronymField->setReadOnly(canonical);
    m_bookAcronymField->setFocusPolicy(canonical ? Qt::NoFocus : Qt::StrongFocus);
    m_bookAcronymField->setToolTip(canonical
        ? QStringLiteral("How %1 is written in the exported file. Milah knows "
                         "this book, so it is not yours to change.")
              .arg(label)
        : QStringLiteral("Milah does not know this work, so what it is "
                         "abbreviated to is yours to decide. The verses will be "
                         "exported as %1.1.1 and so on.")
              .arg(page->book.isEmpty() ? QStringLiteral("…") : page->book));

    // The chapter of the verse being typed in, not the folio's first: a folio
    // that turns a chapter partway down has two, and the useful one is where
    // the caret is.
    const int chapter = m_transcriptionController->selectedChapter();
    m_chapterField->setText(
        QString::number(chapter > 0 ? chapter : page->firstChapter));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Both jobs are asked about, in the order they are offered by the tabs.
    // Either one changing their mind stops the close: the window is one, so a
    // half-shut Milah would take the other's work with it.
    if (!m_controller->confirmDiscard()) {
        event->ignore();
        return;
    }
    if (!m_transcriptionController->confirmDiscard()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::showAbout()
{
    AboutDialog(this).exec();
}

void MainWindow::updateDictionaryActions()
{
    if (m_saveDictionaryAction) {
        m_saveDictionaryAction->setEnabled(
            editingEdition() && !m_controller->dictionary().isEmpty());
    }
}

void MainWindow::undo()
{
    // Ctrl+Z reaches the window's action before the focus widget sees the key,
    // so a word being typed in would otherwise lose the whole edit instead of
    // the last few characters.
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (editor && editor->isUndoAvailable()) {
        editor->undo();
        return;
    }
    if (editingEdition()) {
        m_controller->undo();
    } else {
        m_transcriptionController->undo();
    }
}

void MainWindow::redo()
{
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (editor && editor->isRedoAvailable()) {
        editor->redo();
        return;
    }
    if (editingEdition()) {
        m_controller->redo();
    } else {
        m_transcriptionController->redo();
    }
}

void MainWindow::updateWindowTitle()
{
    // Either job having unsaved work marks the window, because the window is
    // what would take it away.
    const bool dirty = m_controller->isDirty() || m_transcriptionController->isDirty();
    setWindowModified(dirty);

    // Whichever job is in front. Several transcriptions of one codex are open
    // over an afternoon and they look identical from outside; the file is the
    // only thing that tells them apart without looking inside one.
    const bool editing = editingEdition();
    const QString path = editing ? m_controller->filePath()
                                 : m_transcriptionController->filePath();
    const bool open = editing ? !m_controller->sources().isEmpty()
                              : m_transcriptionController->hasDocument();
    if (!open) {
        setWindowTitle(QStringLiteral("Milah[*]"));
        return;
    }

    // [*] rather than a bullet written in by hand: it is the placeholder Qt
    // substitutes per platform, and it puts the mark where that platform's
    // users look for it.
    setWindowTitle(QStringLiteral("%1[*] — Milah")
                       .arg(path.isEmpty() ? QStringLiteral("Untitled")
                                           : QFileInfo(path).fileName()));
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
    const bool editing = editingEdition();
    m_undoAction->setEnabled(editing && m_controller->canUndo());
    m_redoAction->setEnabled(editing && m_controller->canRedo());
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

    updateNavigationActions();
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

    // The combo lives on the criticism toolbar, which is hidden wholesale in
    // the other mode, so it is not a mode's question — only the edition's.
    m_priorityCombo->setEnabled(!manuscripts.isEmpty());
    updateProjectActions();
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
