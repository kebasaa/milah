import type { SourceNote, SourceToken } from "./types";

const tokenPattern =
  /[\p{L}\p{M}\p{N}]+(?:['’׳״-][\p{L}\p{M}\p{N}]+)*|[^\s]/gu;
const hebrewMarks = /[\u0591-\u05BD\u05BF-\u05C7]/gu;
const punctuation = /[\p{P}\p{S}]/gu;

export function comparisonKey(text: string): string {
  return text
    .normalize("NFD")
    .replace(hebrewMarks, "")
    .replace(punctuation, "")
    .normalize("NFC")
    .replace(/\s+/g, " ")
    .trim()
    .toLocaleLowerCase();
}

export function tokenize(
  verseId: string,
  text: string,
  notes: Omit<SourceNote, "tokenIndex">[],
): SourceToken[] {
  const tokens: SourceToken[] = [];
  for (const match of text.matchAll(tokenPattern)) {
    const start = match.index;
    const value = match[0];
    tokens.push({
      id: `${verseId}:t${tokens.length}`,
      text: value,
      start,
      end: start + value.length,
      notes: [],
    });
  }

  for (const note of notes) {
    let tokenIndex = 0;
    for (let index = 0; index < tokens.length; index += 1) {
      if (tokens[index].end <= note.charOffset) {
        tokenIndex = index;
      } else {
        break;
      }
    }
    const anchored: SourceNote = { ...note, tokenIndex };
    if (tokens[tokenIndex]) {
      tokens[tokenIndex].notes.push(anchored);
    }
  }
  return tokens;
}

export function joinTokens(tokens: Array<string | null>): string {
  const visible = tokens.filter((token): token is string => Boolean(token));
  return visible
    .join(" ")
    .replace(/\s+([,.;:!?׃])/gu, "$1")
    .replace(/([־-])\s+/gu, "$1")
    .trim();
}
