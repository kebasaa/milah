#pragma once

#include "core/types.h"

#include <QList>
#include <QString>

#include <stdexcept>

namespace milah {

/// Manuscripts are referred to rather than copied: a SourceDocument carries the
/// whole raw OSIS text with it.
using DocumentRefs = QList<const SourceDocument *>;

class AlignmentError : public std::runtime_error
{
public:
    explicit AlignmentError(const QString &message)
        : std::runtime_error(message.toStdString())
        , m_message(message)
    {
    }

    QString message() const { return m_message; }

private:
    QString m_message;
};

/// Aligns one verse across every manuscript, starting from the priority
/// witness and merging each remaining witness in with a Needleman-Wunsch pass.
AlignedVerse alignVerse(
    const QString &verseId,
    const DocumentRefs &manuscripts,
    const QString &priorityId);

/// Widens the columns the editor has divided, so the Combined edition can read
/// two words where a witness writes one — `אֲשֶׁר־בָּהּ` as `אֲשֶׁר בָּהּ`.
///
/// `splits` holds indices into the columns `alignVerse` produced, once per
/// extra column wanted; an index repeated twice yields two extra. The inserted
/// columns follow their original and carry no witness reading, because the
/// division belongs to the edition and not to the manuscripts.
///
/// The alignment is recomputed whenever the sources, the chapter or the
/// reference witness change, so this has to be reapplied every time or the
/// Combined draft stops lining up with the columns it describes.
AlignedVerse applyColumnSplits(AlignedVerse aligned, const QList<int> &splits);

/// Picks a reading for each column: the majority where there is one, otherwise
/// the priority witness, flagging the column for review.
CombinedDraft generateCombined(
    const AlignedVerse &aligned,
    const DocumentRefs &manuscripts,
    const QString &priorityId);

/// The Combined reading as running text: the manual edit when there is one,
/// otherwise the chosen tokens joined up.
QString combinedText(const CombinedDraft &draft);

/// Spreads a translation's tokens evenly across the aligned columns. Marked
/// high confidence only when the counts match exactly.
QList<TranslationSpan> alignTranslation(
    const QString &translationId,
    const QString &verseId,
    int tokenCount,
    int columnCount);

} // namespace milah
