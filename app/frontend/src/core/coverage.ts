import { compareBooks } from "./books";
import type { Location, SourceDocument } from "./types";

function locationKey(location: Location): string {
  return `${location.book}.${location.chapter}`;
}

export function commonLocations(manuscripts: SourceDocument[]): Location[] {
  if (manuscripts.length === 0) {
    return [];
  }
  const perSource = manuscripts.map((source) => {
    const locations = new Map<string, Location>();
    for (const verse of Object.values(source.verses)) {
      const location = {
        book: verse.reference.book,
        chapter: verse.reference.chapter,
      };
      locations.set(locationKey(location), location);
    }
    return locations;
  });
  return [...perSource[0].values()]
    .filter((location) =>
      perSource.slice(1).every((locations) => locations.has(locationKey(location))),
    )
    .sort((left, right) =>
      compareBooks(left.book, right.book) || left.chapter - right.chapter,
    );
}

export function verseIdsAtLocation(
  manuscripts: SourceDocument[],
  location: Location,
): string[] {
  const ids = new Set<string>();
  for (const source of manuscripts) {
    for (const verse of Object.values(source.verses)) {
      if (
        verse.reference.book === location.book
        && verse.reference.chapter === location.chapter
      ) {
        ids.add(verse.reference.id);
      }
    }
  }
  return [...ids].sort((left, right) => {
    const a = manuscripts.find((source) => source.verses[left])?.verses[left];
    const b = manuscripts.find((source) => source.verses[right])?.verses[right];
    return verseOrder(a?.reference.verse ?? left, b?.reference.verse ?? right);
  });
}

function verseOrder(left: string, right: string): number {
  const parse = (value: string) => {
    const match = /^(\d+)(.*)$/.exec(value);
    return match ? [Number(match[1]), match[2]] as const : [Infinity, value] as const;
  };
  const a = parse(left);
  const b = parse(right);
  return a[0] - b[0] || a[1].localeCompare(b[1]);
}
