#pragma once

#include "core/alignment.h"
#include "core/types.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

class QWidget;

namespace milah {

/// Which verses the reader wants to see. With nothing ticked every verse is
/// shown.
struct ReviewFilters
{
    bool ties = false;
    bool manual = false;
    bool missing = false;
    bool uncertain = false;

    bool any() const { return ties || manual || missing || uncertain; }
};

/// Holds the whole editing session: the loaded witnesses, the Combined drafts,
/// translation alignment, undo history, and the file dialogs that read and
/// write them.
class AppController final : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QWidget *dialogParent, QObject *parent = nullptr);

    const QList<SourceDocument> &sources() const { return m_sources; }
    DocumentRefs manuscripts() const;
    DocumentRefs translations() const;
    const QList<TranslationAssociation> &associations() const { return m_associations; }
    QHash<QString, QString> associationMap() const;
    /// Short labels naming each source in the verse view, keyed by source id.
    QHash<QString, QString> acronyms() const { return sourceAcronyms(m_sources); }
    /// Which way the edition reads, taken from the reference manuscript.
    Qt::LayoutDirection readingDirection() const;
    /// Whether the interlinear Strong's line is drawn. A view preference, so
    /// it is not part of the project.
    bool strongsVisible() const { return m_strongsVisible; }
    void setStrongsVisible(bool visible);
    QString priorityId() const { return m_priorityId; }
    const QList<TranslationSpan> &translationSpans() const { return m_translationSpans; }
    const QList<Location> &locations() const { return m_locations; }
    std::optional<Location> location() const { return m_location; }
    const QList<AlignedVerse> &alignedVerses() const { return m_alignedVerses; }

    bool isDirty() const { return m_dirty; }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    QString message() const { return m_message; }

    ReviewFilters filters() const { return m_filters; }
    void setFilters(const ReviewFilters &filters);

    /// The Combined draft for an aligned verse, generating one on the fly when
    /// the verse has never been committed.
    CombinedDraft draftFor(const AlignedVerse &aligned) const;

    /// True when this verse passes the active review filters.
    bool matchesFilters(const AlignedVerse &aligned) const;

public slots:
    /// Asks for OSIS files and loads them.
    void loadSources(milah::SourceRole role);
    /// Loads OSIS files that have already been chosen. Also how files named on
    /// the command line get in.
    void loadPaths(milah::SourceRole role, const QStringList &paths);
    void openProject();
    void saveProject();
    void exportCombined();

    void setLocation(const milah::Location &location);
    void goToPreviousLocation();
    void goToNextLocation();
    void setPriorityId(const QString &id);
    void regenerate();

    void undo();
    void redo();

    void setAssociation(const QString &translationId, const QString &manuscriptId);
    void chooseToken(const QString &verseId, int columnIndex, const QString &sourceId);
    /// Sets one Combined word by hand. An empty text drops the word.
    void setColumnText(const QString &verseId, int columnIndex, const QString &text);
    void setManualText(const QString &verseId, const QString &text);

    void moveSpan(const QString &spanId, int delta);
    void resizeSpan(const QString &spanId, int delta);
    void mergeSpan(const QString &spanId);
    void splitSpan(const QString &spanId);

signals:
    /// The set of witnesses, the priority witness or the chapter list changed:
    /// the whole window needs rebuilding.
    void sourcesChanged();
    /// What the verse cards show changed, but not what they hold.
    void displayOptionsChanged();
    /// A different chapter is on screen.
    void locationChanged();
    /// One verse card needs refreshing.
    void verseChanged(const QString &verseId);
    void messageChanged(const QString &message);
    void dirtyChanged(bool dirty);
    void historyChanged();

private:
    void setMessage(const QString &message);
    void setDirty(bool dirty);
    void reportError(const QString &fallback);

    void rebuildLocations();
    void rebuildAlignedVerses();
    void refreshTranslationSpans();
    void commitCombined(const QMap<QString, CombinedDraft> &next);
    bool regenerateWith(const QString &nextPriority);
    void applyColumn(
        const QString &verseId,
        int columnIndex,
        const ConsensusColumn &column);
    QMap<QString, CombinedDraft> buildCombined(
        const DocumentRefs &manuscripts,
        const QString &priorityId) const;
    QString suggestAssociation(
        const SourceDocument &translation,
        const DocumentRefs &manuscripts) const;
    bool confirm(const QString &question);
    bool hasManualEdits() const;

    /// The folder last used in a load/open dialog, remembered across runs via
    /// QSettings. Empty until the user opens something.
    QString lastDirectory() const;
    /// Records the folder containing filePath as the last used directory.
    void rememberDirectory(const QString &filePath);

    QWidget *m_dialogParent = nullptr;

    QList<SourceDocument> m_sources;
    QList<TranslationAssociation> m_associations;
    QString m_priorityId;
    bool m_strongsVisible = true;
    QMap<QString, CombinedDraft> m_combined;
    QList<TranslationSpan> m_translationSpans;
    std::optional<Location> m_location;

    QList<Location> m_locations;
    QList<AlignedVerse> m_alignedVerses;

    QList<QMap<QString, CombinedDraft>> m_undoStack;
    QList<QMap<QString, CombinedDraft>> m_redoStack;

    ReviewFilters m_filters;
    bool m_dirty = false;
    QString m_message;
    QString m_lastError;
};

} // namespace milah
