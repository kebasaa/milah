#pragma once

#include <QHash>
#include <QMainWindow>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QToolBar;
class QVBoxLayout;

namespace milah {

class AppController;
class SourceSettingsWidget;
class TranscriptionController;
class TranscriptionMetaWidget;
class TranscriptionWidget;
class VerseGridWidget;
struct Location;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Opens files named on the command line, so a session can be started
    /// without going through the file dialogs.
    void openFiles(const QStringList &manuscripts, const QStringList &translations);

protected:
    /// Every ordinary way out of Milah ends here — the title-bar X, Alt+F4,
    /// Ctrl+Q and File ▸ Quit, the last two because the quit action is wired to
    /// QWidget::close rather than to qApp->quit(). So this is the one place
    /// that can stand between the editor and unsaved work of either kind.
    void closeEvent(QCloseEvent *event) override;

private:
    /// What the window is doing. Two unrelated jobs share one window rather
    /// than two, so that a reader keeps their place in both, and so that the
    /// title bar, Quit and the unsaved-work question stay one thing each.
    enum class Mode { TextualCriticism, Transcription };

    /// Every command the window offers, made once. The menus and the toolbar
    /// then show the same objects, so a shortcut or an enabled state is stated
    /// in one place and the two cannot disagree.
    void createActions();
    void createTranscriptionActions();
    void buildMenuBar();
    void buildToolBar();
    void buildTranscriptionToolBar();
    /// The two tabs, in the menu bar's right-hand corner.
    void buildModeTabs();

    void setMode(Mode mode);
    /// Puts the window into whatever m_mode says: which two menus the bar
    /// carries, which toolbar and which dock are on screen, which page the
    /// centre shows, and which commands are live. Idempotent, so it states the
    /// opening position as well as every change.
    void applyMode();

    /// True while the textual-criticism side is on screen.
    ///
    /// Every enabled state below is two statements at once: what the edition
    /// allows, and what the mode allows. Both have to hold. An action the
    /// edition would happily run must still be dead while its menu is not even
    /// in the bar, because a shortcut is armed by its action and not by its
    /// menu, and would otherwise fire into a window showing a folio.
    bool editingEdition() const { return m_mode == Mode::TextualCriticism; }
    /// The commands that ask nothing of the edition — an empty window can
    /// always be loaded into. Gated for one reason only: so Ctrl+O, Ctrl+M and
    /// Ctrl+T cannot reach in from the transcription side.
    void updateSourceActions();
    /// The four commands that need an edition to act on. Lifted out of the
    /// priority rebuild so the mode switch can ask for them without rebuilding
    /// a combo it has not changed.
    void updateProjectActions();
    /// Chapter navigation, lifted out of the chapter rebuild for the same
    /// reason.
    void updateNavigationActions();
    /// What the transcription side may do, which turns on whether a folio is
    /// open and whether anything has been typed on it.
    void updateTranscriptionActions();
    /// Fills the Book, abbreviation and Chapter fields from the folio on screen.
    void refreshTranscriptionToolBar();
    /// Completes a book name into the field as it is typed. Qt offers a popup
    /// list or an inline fill but not both, so the list is the completer's and
    /// this is the fill.
    void autofillBook(const QString &typed);
    /// Settles what book the folio is: the id the verses will be addressed by,
    /// and — where the canon has never heard of the work — the name as written.
    void commitBook();
    /// Takes an abbreviation the transcriber coined for a work Milah does not
    /// know. Does nothing for a canonical book, whose id is not theirs to set.
    void commitBookAcronym();
    /// Enables the word-level Edit entries for whichever Combined word has the
    /// focus, and greys them out when none has.
    void updateSelectionActions();
    /// Undo and redo carry window-wide shortcuts, which reach them before the
    /// focus widget sees the key. When the caret is inside a Combined word,
    /// that word's own history is what the reader means.
    void undo();
    void redo();
    /// What this build is: its version, and the toolchain and Qt it was made
    /// with. Milah is handed round as a portable folder rather than installed,
    /// so a copy has nothing else to say which one it is.
    void showAbout();
    /// There is nothing to back up until a word has been accepted.
    void updateDictionaryActions();
    void rebuildAll();
    void rebuildBookList();
    void rebuildChapterList();
    /// Draws a combo entry bold where every loaded manuscript reaches the book
    /// or chapter, italic where they do not.
    static void markCoverage(
        QComboBox *combo,
        int item,
        bool complete,
        const QString &reason);
    /// Names the manuscripts with nothing at this location, for the tooltip on
    /// an italicised chapter.
    QString missingFrom(const Location &location) const;
    void rebuildPriorityList();
    void rebuildVerseList();
    void refreshVerse(const QString &verseId);
    void updateHistoryActions();
    void updateWindowTitle();

    AppController *m_controller = nullptr;
    TranscriptionController *m_transcriptionController = nullptr;

    Mode m_mode = Mode::TextualCriticism;
    QTabBar *m_modeTabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    TranscriptionWidget *m_transcription = nullptr;
    TranscriptionMetaWidget *m_metadata = nullptr;

    /// Held because each mode takes the other's chrome off the screen and gives
    /// it back.
    QToolBar *m_toolBar = nullptr;
    QToolBar *m_transcriptionToolBar = nullptr;
    QDockWidget *m_sourcesDock = nullptr;
    QDockWidget *m_metadataDock = nullptr;
    /// Whether each dock was on screen when the other mode took over. A reader
    /// who had closed one themselves should not find it reopened on the way
    /// back.
    bool m_sourcesDockWasVisible = true;
    bool m_metadataDockWasVisible = true;
    /// Whether applyMode() has run before. The first run is stating the opening
    /// position, not leaving a mode, so it has no dock state worth recording.
    bool m_modeApplied = false;

    /// Made once each; which two the bar carries is the mode's business.
    QMenu *m_editionFileMenu = nullptr;
    QMenu *m_editionEditMenu = nullptr;
    QMenu *m_transcriptionFileMenu = nullptr;
    QMenu *m_transcriptionEditMenu = nullptr;
    QMenu *m_aboutMenu = nullptr;

    QComboBox *m_bookCombo = nullptr;
    QComboBox *m_chapterCombo = nullptr;
    QComboBox *m_priorityCombo = nullptr;
    QAction *m_loadManuscriptsAction = nullptr;
    QAction *m_loadTranslationsAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_downloadAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_regenerateAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_aboutAction = nullptr;
    /// Act on the Combined word that has the focus.
    QAction *m_splitAction = nullptr;
    QAction *m_mergePreviousAction = nullptr;
    QAction *m_mergeNextAction = nullptr;
    QAction *m_dictionaryAction = nullptr;
    /// Backing the dictionary up, and taking one back in. Held because saving
    /// is offered only once there is something to save.
    QAction *m_saveDictionaryAction = nullptr;
    QAction *m_loadDictionaryAction = nullptr;

    /// Transcription's own commands. Quit and About are not among them: they
    /// mean the same thing whichever job is being done, so both modes offer the
    /// very same objects.
    QAction *m_openImageAction = nullptr;
    QAction *m_openScanAction = nullptr;
    QAction *m_openTranscriptionAction = nullptr;
    QAction *m_saveTranscriptionAction = nullptr;
    QAction *m_exportOsisAction = nullptr;
    QAction *m_addToLibraryAction = nullptr;
    QAction *m_closeTranscriptionAction = nullptr;
    QAction *m_transcriptionUndoAction = nullptr;
    QAction *m_transcriptionRedoAction = nullptr;
    QAction *m_previousImageAction = nullptr;
    QAction *m_nextImageAction = nullptr;
    QAction *m_magnifyAction = nullptr;
    QAction *m_newChapterAction = nullptr;
    QLineEdit *m_bookField = nullptr;
    /// The id the verses are exported under. Read-only where Milah knows the
    /// book, the transcriber's own where it does not.
    QLineEdit *m_bookAcronymField = nullptr;
    QLineEdit *m_chapterField = nullptr;
    /// What the Book field said last, so the completer can tell a keystroke
    /// that adds from one that deletes — a suggestion put back over a backspace
    /// would make the field impossible to clear.
    QString m_bookTyped;

    QScrollArea *m_verseArea = nullptr;
    QWidget *m_verseHost = nullptr;
    QVBoxLayout *m_verseLayout = nullptr;
    QLabel *m_emptyState = nullptr;
    QHash<QString, VerseGridWidget *> m_verseCards;

    SourceSettingsWidget *m_settings = nullptr;
};

} // namespace milah
