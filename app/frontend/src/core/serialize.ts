import { combinedText } from "./alignment";
import { compareBooks } from "./books";
import type { CombinedDraft, WorkMetadata } from "./types";

function escapeXml(value: string): string {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

export function serializeCombinedOsis(
  drafts: Record<string, CombinedDraft>,
  metadata: Partial<WorkMetadata> = {},
): string {
  const ordered = Object.values(drafts).sort((left, right) =>
    compareBooks(left.reference.book, right.reference.book)
    || left.reference.chapter - right.reference.chapter
    || left.reference.verse.localeCompare(right.reference.verse, undefined, {
      numeric: true,
    }),
  );
  const workId = metadata.workId || "Milah.Combined";
  const language = metadata.language || "he";
  let body = "";
  let currentBook = "";
  let currentChapter = -1;

  for (const draft of ordered) {
    const { reference } = draft;
    if (reference.book !== currentBook) {
      if (currentChapter >= 0) body += "      </chapter>\n";
      if (currentBook) body += "    </div>\n";
      currentBook = reference.book;
      currentChapter = -1;
      body += `    <div type="book" osisID="${escapeXml(currentBook)}">\n`;
    }
    if (reference.chapter !== currentChapter) {
      if (currentChapter >= 0) body += "      </chapter>\n";
      currentChapter = reference.chapter;
      body += `      <chapter osisID="${escapeXml(currentBook)}.${currentChapter}">\n`;
    }
    body += `        <verse osisID="${escapeXml(reference.id)}">${escapeXml(combinedText(draft))}</verse>\n`;
  }
  if (currentChapter >= 0) body += "      </chapter>\n";
  if (currentBook) body += "    </div>\n";

  return `<?xml version="1.0" encoding="UTF-8"?>
<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
  <osisText osisIDWork="${escapeXml(workId)}" osisRefWork="bible" xml:lang="${escapeXml(language)}">
    <header>
      <work osisWork="${escapeXml(workId)}">
        <title>${escapeXml(metadata.title || "Milah Combined Edition")}</title>
        <type type="x-bible">Edition</type>
        <identifier type="OSIS">${escapeXml(workId)}</identifier>
        <language>${escapeXml(language)}</language>
      </work>
      <work osisWork="bible">
        <title>Referenced versification</title>
        <identifier type="OSIS">bible</identifier>
        <refSystem>StandardV11N</refSystem>
      </work>
    </header>
${body}  </osisText>
</osis>
`;
}
