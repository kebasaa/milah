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

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Opens files named on the command line, so a session can be started
    /// without going through the file dialogs.
    void openFiles(const QStringList &manuscripts, const QStringList &translations);

private:
    void buildToolBar();
    void rebuildAll();
    void rebuildChapterList();
    void rebuildPriorityList();
    void rebuildVerseList();
    void refreshVerse(const QString &verseId);
    void updateHistoryActions();
    void updateWindowTitle();

    AppController *m_controller = nullptr;

    QComboBox *m_chapterCombo = nullptr;
    QComboBox *m_priorityCombo = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_regenerateAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_nextAction = nullptr;

    QScrollArea *m_verseArea = nullptr;
    QWidget *m_verseHost = nullptr;
    QVBoxLayout *m_verseLayout = nullptr;
    QLabel *m_emptyState = nullptr;
    QHash<QString, VerseGridWidget *> m_verseCards;

    SourceSettingsWidget *m_settings = nullptr;
};

} // namespace milah
