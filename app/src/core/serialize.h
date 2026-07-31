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

/// A gloss per Combined column, keyed by verse id. A column nothing is aligned
/// to simply has none.
using InterlinearGlosses = QHash<QString, QMap<int, QString>>;

/// The Combined edition with each word marked up separately and carrying its
/// gloss: `<w gloss="to-be">word</w>`. The words are the same as the plain
/// edition's; only the markup around them differs.
QString serializeInterlinearOsis(
    const QMap<QString, CombinedDraft> &drafts,
    const InterlinearGlosses &glosses,
    const WorkMetadata &metadata = WorkMetadata());

} // namespace milah
