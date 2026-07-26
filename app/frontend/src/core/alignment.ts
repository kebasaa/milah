import { comparisonKey, joinTokens } from "./tokenize";
import type {
  AlignedVerse,
  AlignmentColumn,
  CombinedDraft,
  ConsensusColumn,
  SourceDocument,
  SourceToken,
  TranslationSpan,
} from "./types";

function alignmentKey(token: SourceToken | null): string {
  if (!token) {
    return "";
  }
  return comparisonKey(token.text) || token.text.normalize("NFC");
}

function similarity(left: string, right: string): number {
  if (left === right) {
    return 4;
  }
  if (left && right && (left.includes(right) || right.includes(left))) {
    return 1;
  }
  return -2;
}

function representative(column: AlignmentColumn, sourceOrder: string[]): SourceToken | null {
  for (const sourceId of sourceOrder) {
    const token = column.cells[sourceId];
    if (token) {
      return token;
    }
  }
  return null;
}

export function alignVerse(
  verseId: string,
  manuscripts: SourceDocument[],
  priorityId: string,
): AlignedVerse {
  const sourceOrder = [
    priorityId,
    ...manuscripts.map((source) => source.id).filter((id) => id !== priorityId),
  ];
  const reference =
    manuscripts.map((source) => source.verses[verseId]).find(Boolean)?.reference;
  if (!reference) {
    throw new Error(`Cannot align unknown verse ${verseId}.`);
  }

  const firstTokens =
    manuscripts.find((source) => source.id === priorityId)?.verses[verseId]?.tokens
    ?? [];
  let columns: AlignmentColumn[] = firstTokens.map((token, index) => ({
    id: `${verseId}:c${index}`,
    cells: Object.fromEntries(
      manuscripts.map((source) => [
        source.id,
        source.id === priorityId ? token : null,
      ]),
    ),
  }));

  for (const source of manuscripts) {
    if (source.id === priorityId) {
      continue;
    }
    const tokens = source.verses[verseId]?.tokens ?? [];
    const rows = columns.length;
    const cols = tokens.length;
    const scores = Array.from({ length: rows + 1 }, () =>
      Array<number>(cols + 1).fill(0),
    );
    const moves = Array.from({ length: rows + 1 }, () =>
      Array<"d" | "u" | "l">(cols + 1).fill("d"),
    );
    for (let row = 1; row <= rows; row += 1) {
      scores[row][0] = -2 * row;
      moves[row][0] = "u";
    }
    for (let col = 1; col <= cols; col += 1) {
      scores[0][col] = -2 * col;
      moves[0][col] = "l";
    }
    for (let row = 1; row <= rows; row += 1) {
      for (let col = 1; col <= cols; col += 1) {
        const current = alignmentKey(representative(columns[row - 1], sourceOrder));
        const incoming = alignmentKey(tokens[col - 1]);
        const choices = [
          { score: scores[row - 1][col - 1] + similarity(current, incoming), move: "d" as const },
          { score: scores[row - 1][col] - 2, move: "u" as const },
          { score: scores[row][col - 1] - 2, move: "l" as const },
        ].sort((a, b) => b.score - a.score);
        scores[row][col] = choices[0].score;
        moves[row][col] = choices[0].move;
      }
    }

    const merged: AlignmentColumn[] = [];
    let row = rows;
    let col = cols;
    while (row > 0 || col > 0) {
      const move = moves[row][col];
      if (row > 0 && col > 0 && move === "d") {
        const existing = {
          ...columns[row - 1],
          cells: { ...columns[row - 1].cells, [source.id]: tokens[col - 1] },
        };
        merged.push(existing);
        row -= 1;
        col -= 1;
      } else if (row > 0 && (col === 0 || move === "u")) {
        merged.push({
          ...columns[row - 1],
          cells: { ...columns[row - 1].cells, [source.id]: null },
        });
        row -= 1;
      } else {
        merged.push({
          id: `${verseId}:insert:${source.id}:${col - 1}`,
          cells: Object.fromEntries(
            manuscripts.map((item) => [
              item.id,
              item.id === source.id ? tokens[col - 1] : null,
            ]),
          ),
        });
        col -= 1;
      }
    }
    columns = merged.reverse();
  }

  return { reference, columns };
}

function chooseConsensus(
  column: AlignmentColumn,
  manuscripts: SourceDocument[],
  priorityId: string,
): ConsensusColumn {
  const gap = "__gap__";
  const groups = new Map<string, string[]>();
  for (const source of manuscripts) {
    const token = column.cells[source.id];
    const key = token ? comparisonKey(token.text) || "__punctuation__" : gap;
    groups.set(key, [...(groups.get(key) ?? []), source.id]);
  }
  const winner = [...groups.entries()].sort(
    (left, right) => right[1].length - left[1].length,
  )[0];
  const hasMajority = winner[1].length > manuscripts.length / 2;
  const chosenKey = hasMajority
    ? winner[0]
    : (() => {
        const token = column.cells[priorityId];
        return token ? comparisonKey(token.text) || "__punctuation__" : gap;
      })();

  if (chosenKey === gap) {
    return { text: null, sourceId: null, needsReview: !hasMajority };
  }
  const preferred = [priorityId, ...manuscripts.map((source) => source.id)];
  const sourceId = preferred.find((id) => {
    const token = column.cells[id];
    return token
      && (comparisonKey(token.text) || "__punctuation__") === chosenKey;
  }) ?? null;
  return {
    text: sourceId ? column.cells[sourceId]?.text ?? null : null,
    sourceId,
    needsReview: !hasMajority,
  };
}

export function generateCombined(
  aligned: AlignedVerse,
  manuscripts: SourceDocument[],
  priorityId: string,
): CombinedDraft {
  return {
    reference: aligned.reference,
    columns: aligned.columns.map((column) =>
      chooseConsensus(column, manuscripts, priorityId),
    ),
    manualText: null,
  };
}

export function combinedText(draft: CombinedDraft): string {
  return draft.manualText ?? joinTokens(draft.columns.map((column) => column.text));
}

export function alignTranslation(
  translationId: string,
  verseId: string,
  tokenCount: number,
  columnCount: number,
): TranslationSpan[] {
  if (tokenCount === 0 || columnCount === 0) {
    return [];
  }
  return Array.from({ length: tokenCount }, (_, tokenIndex) => {
    const start = Math.min(
      columnCount - 1,
      Math.floor((tokenIndex * columnCount) / tokenCount),
    );
    const end = Math.min(
      columnCount,
      Math.max(start + 1, Math.floor(((tokenIndex + 1) * columnCount) / tokenCount)),
    );
    return {
      id: `${translationId}:${verseId}:${tokenIndex}`,
      translationId,
      verseId,
      columnStart: start,
      columnEnd: end,
      tokenStart: tokenIndex,
      tokenEnd: tokenIndex + 1,
      confidence: tokenCount === columnCount ? "high" : "low",
    };
  });
}
