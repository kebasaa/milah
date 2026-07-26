const CANONICAL_BOOKS = [
  "Gen", "Exod", "Lev", "Num", "Deut", "Josh", "Judg", "Ruth",
  "1Sam", "2Sam", "1Kgs", "2Kgs", "1Chr", "2Chr", "Ezra", "Neh",
  "Esth", "Job", "Ps", "Prov", "Eccl", "Song", "Isa", "Jer", "Lam",
  "Ezek", "Dan", "Hos", "Joel", "Amos", "Obad", "Jonah", "Mic",
  "Nah", "Hab", "Zeph", "Hag", "Zech", "Mal", "Matt", "Mark",
  "Luke", "John", "Acts", "Rom", "1Cor", "2Cor", "Gal", "Eph",
  "Phil", "Col", "1Thess", "2Thess", "1Tim", "2Tim", "Titus",
  "Phlm", "Heb", "Jas", "1Pet", "2Pet", "1John", "2John", "3John",
  "Jude", "Rev",
] as const;

const order = new Map<string, number>(
  CANONICAL_BOOKS.map((book, index) => [book, index]),
);

export function compareBooks(left: string, right: string): number {
  const leftOrder = order.get(left) ?? Number.MAX_SAFE_INTEGER;
  const rightOrder = order.get(right) ?? Number.MAX_SAFE_INTEGER;
  return leftOrder - rightOrder || left.localeCompare(right);
}
