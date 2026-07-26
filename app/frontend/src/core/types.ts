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
}

export interface SourceDocument {
  id: string;
  name: string;
  role: SourceRole;
  rawOsis: string;
  metadata: WorkMetadata;
  verses: Record<string, SourceVerse>;
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
