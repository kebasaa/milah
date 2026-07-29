export type SourceRole = "manuscript" | "translation" | "combined";

export interface WorkMetadata {
  workId: string;
  title: string;
  language: string;
  scope: string;
  identifiers: Record<string, string>;
}

export interface SourceNote {
  id: string;
  number: string;
  text: string;
  charOffset: number;
  tokenIndex: number;
}

export interface SourceToken {
  id: string;
  text: string;
  start: number;
  end: number;
  notes: SourceNote[];
}

export interface VerseReference {
  id: string;
  book: string;
  chapter: number;
  verse: string;
}

export interface SourceVerse {
  reference: VerseReference;
  label: string;
  text: string;
  tokens: SourceToken[];
  /** The manuscript's own verse number, when it differs from the canonical one. */
  altNumber: string | null;
}

/**
 * A heading that belongs to no verse: a manuscript incipit, a chapter title, or
 * a division heading. `chapter` is set when the title introduces one.
 */
export interface SourceTitle {
  id: string;
  type: string;
  canonical: boolean;
  text: string;
  book: string | null;
  chapter: number | null;
  notes: SourceNote[];
}

/**
 * A positioned marker such as a folio boundary (`pb`) or a manuscript verse
 * division. `verseId` is null when the marker falls outside any verse.
 */
export interface SourceMilestone {
  id: string;
  type: string;
  n: string;
  verseId: string | null;
  charOffset: number;
}

export interface SourceDocument {
  id: string;
  name: string;
  role: SourceRole;
  rawOsis: string;
  metadata: WorkMetadata;
  verses: Record<string, SourceVerse>;
  /** Headings and other non-verse text, in document order. */
  titles: SourceTitle[];
  milestones: SourceMilestone[];
  warnings: string[];
}

export interface AlignmentColumn {
  id: string;
  cells: Record<string, SourceToken | null>;
}

export interface AlignedVerse {
  reference: VerseReference;
  columns: AlignmentColumn[];
}

export interface ConsensusColumn {
  text: string | null;
  sourceId: string | null;
  needsReview: boolean;
}

export interface CombinedDraft {
  reference: VerseReference;
  columns: ConsensusColumn[];
  manualText: string | null;
}

export interface TranslationSpan {
  id: string;
  translationId: string;
  verseId: string;
  columnStart: number;
  columnEnd: number;
  tokenStart: number;
  tokenEnd: number;
  confidence: "high" | "low";
}

export interface Location {
  book: string;
  chapter: number;
}

export interface TranslationAssociation {
  translationId: string;
  manuscriptId: string;
}

export interface ProjectState {
  sources: SourceDocument[];
  associations: TranslationAssociation[];
  priorityManuscriptId: string;
  combined: Record<string, CombinedDraft>;
  translationSpans: TranslationSpan[];
  location: Location | null;
}

export interface ProjectFile {
  path: string;
  contentBase64: string;
}

export interface MilahProjectPayload {
  suggestedName?: string;
  manifest: Record<string, unknown>;
  files: ProjectFile[];
}
