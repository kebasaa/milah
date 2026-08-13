#pragma once

#include "ui/kraken_environment.h"

#include <QDialog>
#include <QString>

class QLabel;
class QListWidget;
class QPushButton;

namespace milah {

/// The models installed, and what can be done to them.
///
/// Separate from HtrSetupDialog, and that separation is the point. Installing
/// Kraken and installing a model are different jobs on different schedules —
/// Kraken once per machine, a model whenever a new hand turns up — and having
/// them behind one control produced a dead end: the action that reached the
/// chooser was the same action that installs Kraken, and it greyed itself out
/// once Kraken was working. With one model installed there was then no way
/// anywhere in Milah to add a second.
///
/// This dialog is about what is here. HtrRepositoryDialog, which it opens, is
/// about what could be had.
class HtrModelsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit HtrModelsDialog(KrakenEnvironment *environment, QWidget *parent = nullptr);

private:
    void refresh();
    /// The path of the selected row, empty when nothing is selected.
    QString selectedPath() const;

    void useSelected();
    void addFromRepository();
    void addFromFile();
    /// Deletes a model Milah downloaded; forgets one the transcriber owns.
    /// Which of the two is about to happen is in the confirmation, because they
    /// are not the same act and only one of them is reversible by downloading
    /// again.
    void removeSelected();
    /// Deletes downloads that no installed model points at.
    ///
    /// A download interrupted by a kill or a crash leaves its folder behind and
    /// never reaches the list, which is how several hundred megabytes came to
    /// sit where nothing in Milah could see them, let alone remove them.
    void cleanUpUnused();

    void updateButtons();

    KrakenEnvironment *m_environment = nullptr;

    QLabel *m_summary = nullptr;
    /// Shown only when there is something to say: downloads taking up room and
    /// doing nothing.
    QLabel *m_unused = nullptr;
    QPushButton *m_cleanUpButton = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_useButton = nullptr;
    QPushButton *m_repositoryButton = nullptr;
    QPushButton *m_fileButton = nullptr;
    QPushButton *m_removeButton = nullptr;
};

} // namespace milah
