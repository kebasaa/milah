#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace milah {

/// What one manuscript's training set holds, folio by folio.
///
/// **A set could only be inspected with a file manager.** It accumulates over
/// weeks, from several transcriptions, and what is in it decides what a model
/// learns — yet the only things Milah ever said about it were two totals. A
/// folio saved by mistake, or one whose transcription has since been thrown
/// away, could only be found by opening the application's data folder and
/// guessing at file names.
///
/// So: every folio, its lines, when it was saved and whether the transcription
/// open now says something different about it — and the two things worth doing
/// to one, looking at what training is shown of it, and taking it out.
class TrainingSetDialog final : public QDialog
{
    Q_OBJECT

public:
    /// `staleness` is what the open transcription says of the folios it shares
    /// with the set, keyed by TrainingSet::stemFor(): true for one corrected
    /// since it was saved. A folio absent from it belongs to some other
    /// transcription, and nothing is claimed about it.
    TrainingSetDialog(
        const QString &slug,
        const QString &label,
        const QHash<QString, bool> &staleness,
        QWidget *parent = nullptr);

    /// Whether anything was taken out, so the caller knows to say so.
    bool changed() const { return m_changed; }

signals:
    /// Show the strips training would cut from this folio of the set.
    void stripsWanted(const QString &slug, const QString &stem, const QString &label);

private:
    void reload();
    void removeChosen();
    void updateButtons();

    QString m_slug;
    QString m_label;
    QHash<QString, bool> m_staleness;
    QLabel *m_heading = nullptr;
    QTreeWidget *m_folios = nullptr;
    QPushButton *m_strips = nullptr;
    QPushButton *m_remove = nullptr;
    bool m_changed = false;
};

} // namespace milah
