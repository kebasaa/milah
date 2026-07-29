import { SaxesParser, type SaxesTagNS } from "saxes";

import { tokenize } from "./tokenize";
import type {
  SourceDocument,
  SourceMilestone,
  SourceNote,
  SourceRole,
  SourceTitle,
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
  altNumber: string | null;
}

interface PendingTitle {
  type: string;
  canonical: boolean;
  text: string;
  notes: PendingNote[];
}

/**
 * `subType="x-alt-14"` carries the source's own reference for a verse, where
 * that differs from the canonical one — a manuscript's Hebrew letter-numeral,
 * or another edition's chapter and verse.
 */
const altNumberPattern = /^x-alt-(.+)$/;

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
  const titles: SourceTitle[] = [];
  const milestones: SourceMilestone[] = [];
  const parser = new SaxesParser({ xmlns: true });
  const elementStack: string[] = [];
  let currentVerse: PendingVerse | null = null;
  let currentTitle: PendingTitle | null = null;
  let currentBook: string | null = null;
  let currentChapter: number | null = null;
  /** Titles seen while no chapter is open; they introduce the next one. */
  let awaitingChapter: SourceTitle[] = [];
  let verseMilestones: SourceMilestone[] = [];
  const divTypes: string[] = [];
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
    const normalise = (offset: number) =>
      rawText.slice(0, offset).replace(/\s+/g, " ").trimStart().length;
    const adjustedNotes = currentVerse.notes.map((note) => ({
      ...note,
      charOffset: normalise(note.charOffset),
    }));
    // Milestone offsets were taken against the raw text and need the same
    // whitespace adjustment as notes.
    for (const milestone of verseMilestones) {
      milestone.charOffset = normalise(milestone.charOffset);
    }
    verseMilestones = [];
    verses[currentVerse.reference.id] = {
      reference: currentVerse.reference,
      label: currentVerse.label,
      text,
      tokens: tokenize(currentVerse.reference.id, text, adjustedNotes),
      altNumber: currentVerse.altNumber,
    };
    currentVerse = null;
  };

  const finishTitle = () => {
    if (!currentTitle) {
      return;
    }
    const text = currentTitle.text.replace(/\s+/g, " ").trim();
    if (text) {
      const title: SourceTitle = {
        id: `title-${titles.length}`,
        type: currentTitle.type,
        canonical: currentTitle.canonical,
        text,
        book: currentBook,
        chapter: currentChapter,
        notes: currentTitle.notes.map((note, index) => ({
          ...note,
          id: `title-${titles.length}:n${index}`,
          tokenIndex: 0,
        })),
      };
      titles.push(title);
      // A heading printed between chapters introduces the next one. A heading
      // inside front matter titles the book instead, so it keeps no chapter.
      const enclosing = divTypes.at(-1) ?? "";
      const frontMatter = ["introduction", "titlePage", "preface"].includes(
        enclosing,
      );
      if (currentChapter === null && !frontMatter) {
        awaitingChapter.push(title);
      }
    }
    currentTitle = null;
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

    if (local === "div") {
      const divType = attribute(tag, "type");
      divTypes.push(divType);
      if (divType === "book") {
        currentBook = attribute(tag, "osisID") || currentBook;
      }
    }

    if (local === "chapter") {
      if (attribute(tag, "eID") && !attribute(tag, "osisID")) {
        currentChapter = null;
      } else {
        const chapterId = attribute(tag, "osisID") || attribute(tag, "sID");
        const parts = chapterId.split(".");
        if (parts.length >= 2 && /^\d+$/.test(parts[1])) {
          currentBook = parts[0];
          currentChapter = Number(parts[1]);
          for (const title of awaitingChapter) {
            title.chapter = currentChapter;
            title.book = currentBook;
          }
          awaitingChapter = [];
        }
      }
    }

    // A heading outside the header belongs to the text: a manuscript incipit,
    // a chapter title, or a division heading. It is not part of any verse.
    if (local === "title" && !elementStack.includes("header") && !currentVerse) {
      currentTitle = {
        type: attribute(tag, "type") || "main",
        canonical: attribute(tag, "canonical") === "true",
        text: "",
        notes: [],
      };
      return;
    }

    if (local === "milestone") {
      const type = attribute(tag, "type");
      if (type) {
        const milestone: SourceMilestone = {
          id: `milestone-${milestones.length}`,
          type,
          n: attribute(tag, "n"),
          verseId: currentVerse ? currentVerse.reference.id : null,
          charOffset: currentVerse ? currentVerse.text.length : 0,
        };
        milestones.push(milestone);
        if (currentVerse) {
          verseMilestones.push(milestone);
        }
      }
      return;
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
      const subType = altNumberPattern.exec(attribute(tag, "subType"));
      currentVerse = {
        reference,
        label: attribute(tag, "n") || reference.verse,
        text: "",
        notes: [],
        milestone: Boolean(attribute(tag, "sID")),
        altNumber: subType ? subType[1] : null,
      };
      return;
    }

    if (local === "note") {
      noteText = "";
      noteNumber = attribute(tag, "n");
      noteOffset = currentVerse
        ? currentVerse.text.length
        : currentTitle
          ? currentTitle.text.length
          : 0;
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
      return;
    }
    if (elementStack.includes("note")) {
      if (currentVerse || currentTitle) {
        noteText += text;
      }
      return;
    }
    if (currentVerse) {
      currentVerse.text += text;
    } else if (currentTitle) {
      currentTitle.text += text;
    }
  });

  parser.on("closetag", (tag) => {
    const local = tag.local;
    if (local === "note" && (currentVerse || currentTitle)) {
      const note = {
        number: noteNumber,
        text: noteText.replace(/\s+/g, " ").trim(),
        charOffset: noteOffset,
      };
      if (currentVerse) {
        currentVerse.notes.push({
          id: `${currentVerse.reference.id}:n${currentVerse.notes.length}`,
          ...note,
        });
      } else if (currentTitle) {
        currentTitle.notes.push({ id: "", ...note });
      }
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
    if (local === "title" && currentTitle) {
      finishTitle();
    }
    if (local === "div") {
      divTypes.pop();
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
    titles,
    milestones,
    warnings: [...warnings],
  };
}
