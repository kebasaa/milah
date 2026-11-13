#pragma once

#include "core/serialize.h"
#include "core/transcription.h"

#include <QMap>
#include <QString>

namespace milah {

/// A transcription shaped into the same drafts the edition exports, so the OSIS
/// a folio produces and the OSIS an edition produces are written by one
/// function and cannot drift apart.
struct TranscriptionDrafts
{
    QMap<QString, CombinedDraft> drafts;
    InterlinearGlosses glosses;
    CombinedApparatus apparatus;
    /// Verses with no book, chapter or number, which OSIS cannot address.
    int unnamed = 0;
    /// Notes on those verses, which go nowhere with them.
    int strandedNotes = 0;
};

/// Every addressable verse of `document` as a draft, with its glosses and
/// notes. Marginalia are left out of the running text.
///
/// A verse a folio ends in the middle of is continued on the next one under
/// the same id, and comes out as one verse: the second folio's words follow
/// the first's, and the glosses and notes are numbered across both.
TranscriptionDrafts transcriptionDrafts(const TranscriptionDocument &document);

} // namespace milah
