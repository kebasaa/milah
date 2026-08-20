#pragma once

#include <QHash>
#include <QMainWindow>

#include <functional>

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
    /// Fills `menu` with the files remembered under `key`, each entry calling
    /// `open` with its path.
    ///
    /// Called from the menu's own aboutToShow, which is the whole of the refresh
    /// story: a save, an open, a mode switch and a cleared list all reach the
    /// menu because it is rebuilt the moment before it is seen, and nothing
    /// anywhere has to remember to say so.
    void fillRecentMenu(
        QMenu *menu,
        const QString &key,
        const std::function<void(const QString &)> &open);
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
    /// Fills `menu` with the installed models, the active one checked, and
    /// "Choose a model…" at the foot. Called from each menu's own aboutToShow,
    /// so neither can offer one that has since been removed.
    ///
    /// Reads QSettings and nothing else — deliberately, because asking the
    /// environment would start WSL and a dropdown that hesitates before opening
    /// is one nobody uses twice.
    void fillModelMenu(QMenu *menu);
    /// Names the model Transcribe would use, in its tooltip. A button whose
    /// behaviour depends on a setting kept elsewhere should say what it is.
    void refreshTranscribeTooltip();
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

    /// The two Open Recent submenus, one per tab. Filled the moment before they
    /// are shown rather than kept up to date — see fillRecentMenu.
    QMenu *m_openRecentMenu = nullptr;
    QMenu *m_openRecentTranscriptionMenu = nullptr;

    QComboBox *m_bookCombo = nullptr;
    QComboBox *m_chapterCombo = nullptr;
    QComboBox *m_priorityCombo = nullptr;
    QAction *m_loadManuscriptsAction = nullptr;
    QAction *m_loadTranslationsAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_downloadAction = nullptr;
    QAction *m_libraryAction = nullptr;
    QAction *m_exportAction = nullptr;
    /// Named apart from m_exportWordAction, which is the transcription tab's.
    QAction *m_exportCollationWordAction = nullptr;
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
    QAction *m_exportWordAction = nullptr;
    QAction *m_addToLibraryAction = nullptr;
    QAction *m_closeTranscriptionAction = nullptr;
    QAction *m_transcriptionUndoAction = nullptr;
    QAction *m_transcriptionRedoAction = nullptr;
    QAction *m_previousImageAction = nullptr;
    QAction *m_nextImageAction = nullptr;
    QAction *m_magnifyAction = nullptr;
    /// Reads the folio with a handwriting recogniser, and draws what it read
    /// over the ink it read it from. The eye is checkable and swaps its own
    /// icon, which is what puts the stroke through it.
    QAction *m_transcribeAction = nullptr;
    QAction *m_overlayAction = nullptr;
    /// Takes an ALTO or PAGE file made elsewhere. Lives in the File menu rather
    /// than on the toolbar: it is the occasional way in, not the daily one.
    QAction *m_importLayoutAction = nullptr;
    /// File ▸ Handwriting recognition. Exactly one of the two is live at any
    /// moment, which is the whole of the state the menu has to convey.
    QMenu *m_htrMenu = nullptr;
    QAction *m_htrInstallAction = nullptr;
    /// Reads a folio again for the machine's own readings alone, leaving the
    /// text alone. See TranscriptionController::recoverReadings().
    QAction *m_recoverReadingsAction = nullptr;
    QAction *m_htrRemoveAction = nullptr;
    /// Its own action, and that is the fix rather than a tidying. The way to
    /// the model chooser used to *be* m_htrInstallAction, which the menu greys
    /// out once Kraken is installed — so having one model made it impossible to
    /// add a second from anywhere in Milah.
    QAction *m_manageModelsAction = nullptr;
    /// What the last run did. Live only once there has been one.
    QAction *m_lastRunAction = nullptr;
    /// This folio's corrected lines, into its manuscript's training set. Live
    /// only once a line has been checked all the way through.
    QAction *m_saveForTrainingAction = nullptr;
    /// And training a model on what has been gathered. Live only once some set
    /// has enough in it to be worth the hours.
    QAction *m_trainAction = nullptr;
    /// The installed models, offered in two places at once: under the Transcribe
    /// button's own arrow, where somebody about to press it is already looking,
    /// and in the File menu beside the rest of the recognition settings. One
    /// menu each, filled by one function.
    QMenu *m_modelMenu = nullptr;
    QMenu *m_toolbarModelMenu = nullptr;
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
