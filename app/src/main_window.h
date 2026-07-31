#pragma once

#include <QHash>
#include <QMainWindow>

class QAction;
class QComboBox;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace milah {

class AppController;
class SourceSettingsWidget;
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

private:
    /// Every command the window offers, made once. The menus and the toolbar
    /// then show the same objects, so a shortcut or an enabled state is stated
    /// in one place and the two cannot disagree.
    void createActions();
    void buildMenuBar();
    void buildToolBar();
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

    QComboBox *m_bookCombo = nullptr;
    QComboBox *m_chapterCombo = nullptr;
    QComboBox *m_priorityCombo = nullptr;
    QAction *m_loadManuscriptsAction = nullptr;
    QAction *m_loadTranslationsAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
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

    QScrollArea *m_verseArea = nullptr;
    QWidget *m_verseHost = nullptr;
    QVBoxLayout *m_verseLayout = nullptr;
    QLabel *m_emptyState = nullptr;
    QHash<QString, VerseGridWidget *> m_verseCards;

    SourceSettingsWidget *m_settings = nullptr;
};

} // namespace milah
