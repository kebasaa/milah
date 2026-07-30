#include "core/alignment.h"

#include "core/tokenize.h"

#include <algorithm>

namespace milah {
namespace {

const QString &gapKey()
{
    static const QString key = QStringLiteral("__gap__");
    return key;
}

const QString &punctuationKey()
{
    static const QString key = QStringLiteral("__punctuation__");
    return key;
}

QString alignmentKey(const SourceToken *token)
{
    if (!token) {
        return QString();
    }
    const QString key = comparisonKey(token->text);
    if (!key.isEmpty()) {
        return key;
    }
    return token->text.normalized(QString::NormalizationForm_C);
}

int similarity(const QString &left, const QString &right)
{
    if (left == right) {
        return 4;
    }
    if (!left.isEmpty() && !right.isEmpty()
        && (left.contains(right) || right.contains(left))) {
        return 1;
    }
    return -2;
}

const SourceToken *representative(
    const AlignmentColumn &column,
    const QStringList &sourceOrder)
{
    for (const QString &sourceId : sourceOrder) {
        if (const SourceToken *token = column.cell(sourceId)) {
            return token;
        }
    }
    return nullptr;
}

/// The key a reading is grouped under when looking for a majority. Punctuation
/// normalises away to nothing, so it gets a bucket of its own rather than
/// being lumped in with genuine gaps.
QString consensusKey(const SourceToken *token)
{
    if (!token) {
        return gapKey();
    }
    const QString key = comparisonKey(token->text);
    return key.isEmpty() ? punctuationKey() : key;
}

ConsensusColumn chooseConsensus(
    const AlignmentColumn &column,
    const DocumentRefs &manuscripts,
    const QString &priorityId)
{
    // Insertion-ordered, because an even split has to fall to the first
    // manuscript encountered rather than to an arbitrary one.
    QStringList groupKeys;
    QList<int> groupCounts;

    for (const SourceDocument *source : manuscripts) {
        const QString key = consensusKey(column.cell(source->id));
        const int existing = int(groupKeys.indexOf(key));
        if (existing >= 0) {
            groupCounts[existing] += 1;
        } else {
            groupKeys.append(key);
            groupCounts.append(1);
        }
    }

    int winnerIndex = 0;
    for (int index = 1; index < groupCounts.size(); ++index) {
        if (groupCounts.at(index) > groupCounts.at(winnerIndex)) {
            winnerIndex = index;
        }
    }

    const bool hasMajority =
        !groupCounts.isEmpty()
        && groupCounts.at(winnerIndex) * 2 > int(manuscripts.size());

    const QString chosenKey = hasMajority
        ? groupKeys.at(winnerIndex)
        : consensusKey(column.cell(priorityId));

    ConsensusColumn result;
    result.needsReview = !hasMajority;

    if (chosenKey == gapKey()) {
        return result;
    }

    QStringList preferred;
    preferred.append(priorityId);
    for (const SourceDocument *source : manuscripts) {
        preferred.append(source->id);
    }

    for (const QString &sourceId : preferred) {
        const SourceToken *token = column.cell(sourceId);
        if (token && consensusKey(token) == chosenKey) {
            result.sourceId = sourceId;
            result.text = token->text;
            break;
        }
    }

    return result;
}

} // namespace

AlignedVerse alignVerse(
    const QString &verseId,
    const DocumentRefs &manuscripts,
    const QString &priorityId)
{
    QStringList sourceOrder;
    sourceOrder.append(priorityId);
    for (const SourceDocument *source : manuscripts) {
        if (source->id != priorityId) {
            sourceOrder.append(source->id);
        }
    }

    const SourceVerse *referenceVerse = nullptr;
    for (const SourceDocument *source : manuscripts) {
        if (const SourceVerse *verse = source->verse(verseId)) {
            referenceVerse = verse;
            break;
        }
    }
    if (!referenceVerse) {
        throw AlignmentError(
            QStringLiteral("Cannot align unknown verse %1.").arg(verseId));
    }

    AlignedVerse aligned;
    aligned.reference = referenceVerse->reference;

    QList<SourceToken> firstTokens;
    for (const SourceDocument *source : manuscripts) {
        if (source->id == priorityId) {
            if (const SourceVerse *verse = source->verse(verseId)) {
                firstTokens = verse->tokens;
            }
            break;
        }
    }

    QList<AlignmentColumn> columns;
    columns.reserve(firstTokens.size());
    for (int index = 0; index < firstTokens.size(); ++index) {
        AlignmentColumn column;
        column.id = QStringLiteral("%1:c%2").arg(verseId).arg(index);
        column.cells.insert(priorityId, firstTokens.at(index));
        columns.append(column);
    }

    for (const SourceDocument *source : manuscripts) {
        if (source->id == priorityId) {
            continue;
        }

        QList<SourceToken> tokens;
        if (const SourceVerse *verse = source->verse(verseId)) {
            tokens = verse->tokens;
        }

        const int rows = int(columns.size());
        const int cols = int(tokens.size());

        QList<QList<int>> scores(rows + 1, QList<int>(cols + 1, 0));
        QList<QList<char>> moves(rows + 1, QList<char>(cols + 1, 'd'));

        for (int row = 1; row <= rows; ++row) {
            scores[row][0] = -2 * row;
            moves[row][0] = 'u';
        }
        for (int col = 1; col <= cols; ++col) {
            scores[0][col] = -2 * col;
            moves[0][col] = 'l';
        }

        for (int row = 1; row <= rows; ++row) {
            const QString current =
                alignmentKey(representative(columns.at(row - 1), sourceOrder));
            for (int col = 1; col <= cols; ++col) {
                const QString incoming = alignmentKey(&tokens[col - 1]);
                const int diagonal =
                    scores.at(row - 1).at(col - 1) + similarity(current, incoming);
                const int up = scores.at(row - 1).at(col) - 2;
                const int left = scores.at(row).at(col - 1) - 2;

                // Ties resolve diagonal, then up, then left: the JavaScript this
                // replaces relied on a stable sort of the three candidates, and
                // a different order changes the alignment.
                const int best = std::max({diagonal, up, left});
                scores[row][col] = best;
                if (diagonal == best) {
                    moves[row][col] = 'd';
                } else if (up == best) {
                    moves[row][col] = 'u';
                } else {
                    moves[row][col] = 'l';
                }
            }
        }

        QList<AlignmentColumn> merged;
        int row = rows;
        int col = cols;
        while (row > 0 || col > 0) {
            const char move = moves.at(row).at(col);
            if (row > 0 && col > 0 && move == 'd') {
                AlignmentColumn existing = columns.at(row - 1);
                existing.cells.insert(source->id, tokens.at(col - 1));
                merged.append(existing);
                row -= 1;
                col -= 1;
            } else if (row > 0 && (col == 0 || move == 'u')) {
                AlignmentColumn existing = columns.at(row - 1);
                existing.cells.remove(source->id);
                merged.append(existing);
                row -= 1;
            } else {
                AlignmentColumn inserted;
                inserted.id = QStringLiteral("%1:insert:%2:%3")
                                  .arg(verseId, source->id)
                                  .arg(col - 1);
                inserted.cells.insert(source->id, tokens.at(col - 1));
                merged.append(inserted);
                col -= 1;
            }
        }

        std::reverse(merged.begin(), merged.end());
        columns = merged;
    }

    aligned.columns = columns;
    return aligned;
}

AlignedVerse applyColumnSplits(AlignedVerse aligned, const QList<int> &splits)
{
    if (splits.isEmpty()) {
        return aligned;
    }

    QHash<int, int> extra;
    for (const int column : splits) {
        if (column >= 0 && column < aligned.columns.size()) {
            extra[column] += 1;
        }
    }
    if (extra.isEmpty()) {
        return aligned;
    }

    QList<AlignmentColumn> widened;
    widened.reserve(aligned.columns.size() + splits.size());
    for (int index = 0; index < aligned.columns.size(); ++index) {
        widened.append(aligned.columns.at(index));
        for (int copy = 1; copy <= extra.value(index); ++copy) {
            AlignmentColumn inserted;
            // No cells: no witness reads anything here. The id stays derived
            // from its original so it survives a realignment recognisably.
            inserted.id = QStringLiteral("%1:split%2")
                              .arg(aligned.columns.at(index).id)
                              .arg(copy);
            widened.append(inserted);
        }
    }

    aligned.columns = widened;
    return aligned;
}

CombinedDraft generateCombined(
    const AlignedVerse &aligned,
    const DocumentRefs &manuscripts,
    const QString &priorityId)
{
    CombinedDraft draft;
    draft.reference = aligned.reference;
    draft.columns.reserve(aligned.columns.size());
    for (const AlignmentColumn &column : aligned.columns) {
        draft.columns.append(chooseConsensus(column, manuscripts, priorityId));
    }
    return draft;
}

QString combinedText(const CombinedDraft &draft)
{
    if (draft.manualText.has_value()) {
        return *draft.manualText;
    }

    QList<std::optional<QString>> texts;
    texts.reserve(draft.columns.size());
    for (const ConsensusColumn &column : draft.columns) {
        texts.append(column.text);
    }
    return joinTokens(texts);
}

QList<TranslationSpan> alignTranslation(
    const QString &translationId,
    const QString &verseId,
    int tokenCount,
    int columnCount)
{
    QList<TranslationSpan> spans;
    if (tokenCount == 0 || columnCount == 0) {
        return spans;
    }

    spans.reserve(tokenCount);
    for (int tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex) {
        const int start =
            std::min(columnCount - 1, (tokenIndex * columnCount) / tokenCount);
        const int end = std::min(
            columnCount,
            std::max(start + 1, ((tokenIndex + 1) * columnCount) / tokenCount));

        TranslationSpan span;
        span.id = QStringLiteral("%1:%2:%3").arg(translationId, verseId).arg(tokenIndex);
        span.translationId = translationId;
        span.verseId = verseId;
        span.columnStart = start;
        span.columnEnd = end;
        span.tokenStart = tokenIndex;
        span.tokenEnd = tokenIndex + 1;
        span.confidence = tokenCount == columnCount
            ? SpanConfidence::High
            : SpanConfidence::Low;
        spans.append(span);
    }

    return spans;
}

} // namespace milah
