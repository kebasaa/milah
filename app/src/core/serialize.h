#pragma once

#include "core/types.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>

namespace milah {

/// Material that belongs to no verse, carried through from the sources so that
/// an exported edition keeps its headings, folio boundaries and notes.
struct CombinedApparatus
{
    QList<SourceTitle> titles;
    QList<SourceMilestone> milestones;
    /// Notes to anchor inside a verse, keyed by verse id.
    QHash<QString, QList<SourceNote>> notes;
};

/// Renders the Combined edition as a standalone OSIS document.
QString serializeCombinedOsis(
    const QMap<QString, CombinedDraft> &drafts,
    const WorkMetadata &metadata = WorkMetadata(),
    const CombinedApparatus &apparatus = CombinedApparatus());

} // namespace milah
