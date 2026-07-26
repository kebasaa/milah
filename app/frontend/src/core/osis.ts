import { SaxesParser, type SaxesTagNS } from "saxes";

import { tokenize } from "./tokenize";
import type {
  SourceDocument,
  SourceNote,
  SourceRole,
  SourceVerse,
  VerseReference,
  WorkMetadata,
} from "./types";

const forbiddenDeclarations = /<!\s*(?:DOCTYPE|ENTITY)\b/i;
const supportedInline = new Set([
  "verse", "note", "w", "seg", "hi", "divineName", "name", "foreign",
  "transChange", "q", "reference", "lb",
]);

interface PendingNote extends Omit<SourceNote, "tokenIndex"> {}

interface PendingVerse {
  reference: VerseReference;
  label: string;
  text: string;
  notes: PendingNote[];
  milestone: boolean;
}

function attribute(tag: SaxesTagNS, name: string): string {
  const direct = tag.attributes[name];
  if (direct && typeof direct === "object" && "value" in direct) {
    return direct.value;
  }
  const found = Object.values(tag.attributes).find(
    (value) => typeof value === "object" && value.local === name,
  );
  return found && typeof found === "object" ? found.value : "";
}

function parseReference(id: string): VerseReference {
  const first = id.split(/\s+/)[0];
  const match = /^([^.]+)\.(\d+)\.(.+)$/.exec(first);
  if (!match) {
    throw new Error(`Invalid verse osisID: ${id}`);
  }
  return {
    id: first,
    book: match[1],
    chapter: Number(match[2]),
    verse: match[3],
  };
}

export function parseOsis(
  rawOsis: string,
  options: { id: string; name: string; role: SourceRole },
): SourceDocument {
  if (forbiddenDeclarations.test(rawOsis)) {
    throw new Error("OSIS files containing DTD or ENTITY declarations are not allowed.");
  }

  const metadata: WorkMetadata = {
    workId: "",
    title: options.name,
    language: "",
    scope: "",
    identifiers: {},
  };
  const warnings = new Set<string>();
  const verses: Record<string, SourceVerse> = {};
  const parser = new SaxesParser({ xmlns: true });
  const elementStack: string[] = [];
  let currentVerse: PendingVerse | null = null;
  let noteText = "";
  let noteNumber = "";
  let noteOffset = 0;
  let metadataField: "title" | "language" | "scope" | "identifier" | null = null;
  let metadataText = "";
  let identifierType = "";
  let parserError: Error | null = null;

  const finishVerse = () => {
    if (!currentVerse) {
      return;
    }
    if (verses[currentVerse.reference.id]) {
      throw new Error(`Duplicate verse ID: ${currentVerse.reference.id}`);
    }
    const rawText = currentVerse.text;
    const text = rawText.replace(/\s+/g, " ").trim();
    const adjustedNotes = currentVerse.notes.map((note) => ({
      ...note,
      charOffset: rawText
        .slice(0, note.charOffset)
        .replace(/\s+/g, " ")
        .trimStart()
        .length,
    }));
    verses[currentVerse.reference.id] = {
      reference: currentVerse.reference,
      label: currentVerse.label,
      text,
      tokens: tokenize(currentVerse.reference.id, text, adjustedNotes),
    };
    currentVerse = null;
  };

  parser.on("error", (error) => {
    parserError = error;
  });

  parser.on("opentag", (tag) => {
    const local = tag.local;
    elementStack.push(local);

    if (local === "osisText") {
      metadata.workId = attribute(tag, "osisIDWork");
      metadata.language = attribute(tag, "lang");
    }

    if (["title", "language", "scope", "identifier"].includes(local)
        && elementStack.includes("header")) {
      metadataField = local as typeof metadataField;
      metadataText = "";
      identifierType = attribute(tag, "type") || "unspecified";
    }

    if (local === "verse") {
      const endId = attribute(tag, "eID");
      if (endId) {
        if (!currentVerse || !currentVerse.milestone
            || currentVerse.reference.id !== endId) {
          throw new Error(`Unbalanced verse milestone: ${endId}`);
        }
        finishVerse();
        return;
      }
      const id = attribute(tag, "osisID") || attribute(tag, "sID");
      if (!id) {
        throw new Error("Verse is missing osisID or sID.");
      }
      if (currentVerse) {
        throw new Error(`Verse ${id} begins before the previous verse ends.`);
      }
      const reference = parseReference(id);
      currentVerse = {
        reference,
        label: attribute(tag, "n") || reference.verse,
        text: "",
        notes: [],
        milestone: Boolean(attribute(tag, "sID")),
      };
      return;
    }

    if (currentVerse && local === "note") {
      noteText = "";
      noteNumber = attribute(tag, "n");
      noteOffset = currentVerse.text.length;
      return;
    }

    if (currentVerse && local === "lb") {
      currentVerse.text += " ";
    } else if (currentVerse && !supportedInline.has(local)) {
      warnings.add(`Unsupported inline <${local}> markup was flattened.`);
    }
  });

  parser.on("text", (text) => {
    if (metadataField) {
      metadataText += text;
    }
    if (!currentVerse) {
      return;
    }
    if (elementStack.at(-1) === "note" || elementStack.includes("note")) {
      noteText += text;
    } else {
      currentVerse.text += text;
    }
  });

  parser.on("closetag", (tag) => {
    const local = tag.local;
    if (currentVerse && local === "note") {
      currentVerse.notes.push({
        id: `${currentVerse.reference.id}:n${currentVerse.notes.length}`,
        number: noteNumber,
        text: noteText.replace(/\s+/g, " ").trim(),
        charOffset: noteOffset,
      });
      noteText = "";
    }

    if (metadataField === local) {
      const value = metadataText.replace(/\s+/g, " ").trim();
      if (local === "identifier") {
        if (value && !(identifierType in metadata.identifiers)) {
          metadata.identifiers[identifierType] = value;
        }
      } else if (local === "title" && value && metadata.title === options.name) {
        metadata.title = value;
      } else if (local === "language" && value) {
        metadata.language = value;
      } else if (local === "scope" && value) {
        metadata.scope = value;
      }
      metadataField = null;
      metadataText = "";
    }

    if (local === "verse" && currentVerse && !currentVerse.milestone) {
      finishVerse();
    }
    elementStack.pop();
  });

  try {
    parser.write(rawOsis).close();
  } catch (error) {
    throw error instanceof Error ? error : new Error(String(error));
  }
  if (parserError) {
    throw parserError;
  }
  if (currentVerse) {
    throw new Error(`Verse milestone ${currentVerse.reference.id} was not closed.`);
  }
  if (Object.keys(verses).length === 0) {
    throw new Error("The OSIS file contains no usable verses.");
  }

  return {
    id: options.id,
    name: options.name,
    role: options.role,
    rawOsis,
    metadata,
    verses,
    warnings: [...warnings],
  };
}
