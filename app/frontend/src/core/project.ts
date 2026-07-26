import { parseOsis } from "./osis";
import type {
  MilahProjectPayload,
  ProjectState,
  SourceDocument,
  SourceRole,
} from "./types";

interface SourceManifest {
  id: string;
  name: string;
  role: SourceRole;
  entry: string;
}

interface ProjectManifest {
  format: "milah-project";
  version: 1;
  savedAt: string;
  sources: SourceManifest[];
  associations: ProjectState["associations"];
  priorityManuscriptId: string;
  combined: ProjectState["combined"];
  translationSpans: ProjectState["translationSpans"];
  location: ProjectState["location"];
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunkSize = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(offset, offset + chunkSize));
  }
  return btoa(binary);
}

function base64ToText(value: string): string {
  const binary = atob(value);
  const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
  return new TextDecoder().decode(bytes);
}

export function projectPayload(
  state: ProjectState,
  combinedOsis: string,
): MilahProjectPayload {
  const sources: SourceManifest[] = state.sources.map((source, index) => ({
    id: source.id,
    name: source.name,
    role: source.role,
    entry: `sources/${String(index + 1).padStart(3, "0")}-${source.id}.osis`,
  }));
  const manifest: ProjectManifest = {
    format: "milah-project",
    version: 1,
    savedAt: new Date().toISOString(),
    sources,
    associations: state.associations,
    priorityManuscriptId: state.priorityManuscriptId,
    combined: state.combined,
    translationSpans: state.translationSpans,
    location: state.location,
  };
  const encoder = new TextEncoder();
  return {
    suggestedName: "Milah_Project.milah",
    manifest: manifest as unknown as Record<string, unknown>,
    files: [
      ...sources.map((source) => {
        const document = state.sources.find((item) => item.id === source.id);
        return {
          path: source.entry,
          contentBase64: bytesToBase64(encoder.encode(document?.rawOsis ?? "")),
        };
      }),
      {
        path: "combined/combined.osis",
        contentBase64: bytesToBase64(encoder.encode(combinedOsis)),
      },
    ],
  };
}

export function restoreProject(payload: MilahProjectPayload): ProjectState {
  const manifest = payload.manifest as unknown as ProjectManifest;
  if (manifest.format !== "milah-project" || manifest.version !== 1) {
    throw new Error("This Milah project version is not supported.");
  }
  const files = new Map(payload.files.map((file) => [file.path, file.contentBase64]));
  const sources: SourceDocument[] = manifest.sources.map((source) => {
    const encoded = files.get(source.entry);
    if (!encoded) {
      throw new Error(`Project source is missing: ${source.entry}`);
    }
    return parseOsis(base64ToText(encoded), {
      id: source.id,
      name: source.name,
      role: source.role,
    });
  });
  return {
    sources,
    associations: manifest.associations ?? [],
    priorityManuscriptId: manifest.priorityManuscriptId,
    combined: manifest.combined ?? {},
    translationSpans: manifest.translationSpans ?? [],
    location: manifest.location ?? null,
  };
}
