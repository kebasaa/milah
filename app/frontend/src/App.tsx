import { useEffect, useMemo, useRef, useState } from "react";

import {
  alignTranslation,
  alignVerse,
  generateCombined,
} from "./core/alignment";
import { commonLocations, verseIdsAtLocation } from "./core/coverage";
import { parseOsis } from "./core/osis";
import { projectPayload, restoreProject } from "./core/project";
import { serializeCombinedOsis } from "./core/serialize";
import type {
  CombinedDraft,
  Location,
  ProjectState,
  SourceDocument,
  TranslationAssociation,
  TranslationSpan,
} from "./core/types";
import {
  SourceSettings,
  type ReviewFilters,
} from "./components/SourceSettings";
import { Toolbar } from "./components/Toolbar";
import { VerseGrid } from "./components/VerseGrid";
import { connectPlatform, type PlatformGateway } from "./services/platform";

function sourceId(role: string, index: number): string {
  return `${role}-${Date.now().toString(36)}-${index}`;
}

function suggestAssociation(
  translation: SourceDocument,
  manuscripts: SourceDocument[],
): string {
  const shelfmark = translation.metadata.identifiers.shelfmark;
  const exact = manuscripts.find(
    (source) =>
      shelfmark && source.metadata.identifiers.shelfmark === shelfmark,
  );
  if (exact) return exact.id;
  return manuscripts
    .map((source) => ({
      id: source.id,
      overlap: Object.keys(translation.verses)
        .filter((id) => Boolean(source.verses[id])).length,
    }))
    .sort((left, right) => right.overlap - left.overlap)[0]?.id ?? "";
}

function buildCombined(
  manuscripts: SourceDocument[],
  priorityId: string,
): Record<string, CombinedDraft> {
  const result: Record<string, CombinedDraft> = {};
  for (const location of commonLocations(manuscripts)) {
    for (const verseId of verseIdsAtLocation(manuscripts, location)) {
      const aligned = alignVerse(verseId, manuscripts, priorityId);
      result[verseId] = generateCombined(aligned, manuscripts, priorityId);
    }
  }
  return result;
}

export default function App() {
  const [gateway, setGateway] = useState<PlatformGateway | null>(null);
  const [sources, setSources] = useState<SourceDocument[]>([]);
  const [associations, setAssociations] = useState<TranslationAssociation[]>([]);
  const [priorityId, setPriorityId] = useState("");
  const [combined, setCombined] = useState<Record<string, CombinedDraft>>({});
  const [translationSpans, setTranslationSpans] = useState<TranslationSpan[]>([]);
  const [location, setLocation] = useState<Location | null>(null);
  const [dirty, setDirty] = useState(false);
  const [message, setMessage] = useState("Load two or more manuscript OSIS files to begin.");
  const [undoStack, setUndoStack] = useState<Record<string, CombinedDraft>[]>([]);
  const [redoStack, setRedoStack] = useState<Record<string, CombinedDraft>[]>([]);
  const [filters, setFilters] = useState<ReviewFilters>({
    ties: false,
    manual: false,
    missing: false,
    uncertain: false,
  });
  const combinedRef = useRef(combined);
  combinedRef.current = combined;

  useEffect(() => {
    void connectPlatform().then(setGateway);
  }, []);

  const manuscripts = useMemo(
    () => sources.filter((source) => source.role === "manuscript"),
    [sources],
  );
  const translations = useMemo(
    () => sources.filter((source) => source.role === "translation"),
    [sources],
  );
  const locations = useMemo(() => commonLocations(manuscripts), [manuscripts]);
  const associationMap = useMemo(
    () => Object.fromEntries(
      associations.map((item) => [item.translationId, item.manuscriptId]),
    ),
    [associations],
  );
  const verseIds = useMemo(
    () => location ? verseIdsAtLocation(manuscripts, location) : [],
    [location, manuscripts],
  );
  const alignedVerses = useMemo(
    () => verseIds.map((id) => alignVerse(id, manuscripts, priorityId)),
    [verseIds, manuscripts, priorityId],
  );

  const reportError = async (error: unknown) => {
    const nativeError = await gateway?.lastError();
    setMessage(nativeError || (error instanceof Error ? error.message : String(error)));
  };

  const loadSources = async (role: "manuscript" | "translation") => {
    if (!gateway) return;
    try {
      const opened = await gateway.openOsisFiles(role);
      if (!opened.length) return;
      if (
        role === "manuscript"
        && Object.values(combined).some((draft) => draft.manualText !== null)
        && !window.confirm(
          "Adding manuscripts regenerates Combined and replaces manual edits. Continue?",
        )
      ) return;
      const parsed = opened.map((file, index) =>
        parseOsis(file.content, {
          id: sourceId(role, index),
          name: file.name,
          role,
        }),
      );
      const nextSources = [...sources, ...parsed];
      const nextManuscripts = nextSources.filter((source) => source.role === "manuscript");
      let nextPriority = priorityId;
      let nextCombined = combined;
      let nextAssociations = associations;
      if (role === "manuscript") {
        nextPriority ||= nextManuscripts[0]?.id ?? "";
        nextCombined = buildCombined(nextManuscripts, nextPriority);
        nextAssociations = associations.map((association) => {
          if (nextManuscripts.some(
            (source) => source.id === association.manuscriptId,
          )) return association;
          const translation = nextSources.find(
            (source) => source.id === association.translationId,
          );
          return {
            ...association,
            manuscriptId: translation
              ? suggestAssociation(translation, nextManuscripts)
              : "",
          };
        });
      } else {
        nextAssociations = [
          ...associations,
          ...parsed.map((translation) => ({
            translationId: translation.id,
            manuscriptId: suggestAssociation(translation, nextManuscripts),
          })),
        ];
      }
      const nextLocations = commonLocations(nextManuscripts);
      const stillCommon = location && nextLocations.some(
        (item) => item.book === location.book && item.chapter === location.chapter,
      );
      setSources(nextSources);
      setPriorityId(nextPriority);
      setCombined(nextCombined);
      setAssociations(nextAssociations);
      if (role === "manuscript") {
        setTranslationSpans([]);
      }
      setLocation(stillCommon ? location : nextLocations[0] ?? null);
      setDirty(true);
      setMessage(nextLocations.length
        ? `${nextManuscripts.length} manuscript(s) loaded.`
        : "The loaded manuscripts do not share a book and chapter.");
    } catch (error) {
      await reportError(error);
    }
  };

  useEffect(() => {
    if (!location || !translations.length || !alignedVerses.length) return;
    setTranslationSpans((current) => {
      const keep = current.filter((span) =>
        !verseIds.includes(span.verseId)
        && translations.some((translation) => translation.id === span.translationId),
      );
      const generated = translations.flatMap((translation) =>
        alignedVerses.flatMap((aligned) => {
          const existing = current.filter(
            (span) =>
              span.translationId === translation.id
              && span.verseId === aligned.reference.id,
          );
          return existing.length
            ? existing
            : alignTranslation(
                translation.id,
                aligned.reference.id,
                translation.verses[aligned.reference.id]?.tokens.length ?? 0,
                aligned.columns.length,
              );
        }),
      );
      return [...keep, ...generated];
    });
  }, [location, translations, alignedVerses, verseIds]);

  const commitCombined = (next: Record<string, CombinedDraft>) => {
    setUndoStack((stack) => [...stack.slice(-49), combinedRef.current]);
    setRedoStack([]);
    setCombined(next);
    setDirty(true);
  };

  const regenerate = (nextPriority = priorityId): boolean => {
    if (
      Object.values(combined).some((draft) => draft.manualText !== null)
      && !window.confirm("Regenerate Combined and replace manual edits?")
    ) return false;
    commitCombined(buildCombined(manuscripts, nextPriority));
    return true;
  };

  const openProject = async () => {
    if (!gateway) return;
    try {
      const payload = await gateway.openProject();
      if (!payload) return;
      const state = restoreProject(payload);
      setSources(state.sources);
      setAssociations(state.associations);
      setPriorityId(state.priorityManuscriptId);
      setCombined(state.combined);
      setTranslationSpans(state.translationSpans);
      setLocation(state.location);
      setUndoStack([]);
      setRedoStack([]);
      setDirty(false);
      setMessage("Milah project opened.");
    } catch (error) {
      await reportError(error);
    }
  };

  const saveProject = async () => {
    if (!gateway) return;
    try {
      const osis = serializeCombinedOsis(combined);
      const state: ProjectState = {
        sources,
        associations,
        priorityManuscriptId: priorityId,
        combined,
        translationSpans,
        location,
      };
      if (await gateway.saveProject(projectPayload(state, osis))) {
        setDirty(false);
        setMessage("Milah project saved.");
      } else {
        await reportError("Project save was cancelled.");
      }
    } catch (error) {
      await reportError(error);
    }
  };

  const exportCombined = async () => {
    if (!gateway) return;
    try {
      if (await gateway.exportCombinedOsis(serializeCombinedOsis(combined))) {
        setMessage("Combined OSIS exported.");
      } else {
        await reportError("Export was cancelled.");
      }
    } catch (error) {
      await reportError(error);
    }
  };

  return (
    <div className="app-shell">
      <Toolbar
        manuscripts={manuscripts}
        priorityId={priorityId}
        locations={locations}
        location={location}
        dirty={dirty}
        canUndo={undoStack.length > 0}
        canRedo={redoStack.length > 0}
        onLoadManuscripts={() => void loadSources("manuscript")}
        onLoadTranslations={() => void loadSources("translation")}
        onOpenProject={() => void openProject()}
        onSaveProject={() => void saveProject()}
        onExport={() => void exportCombined()}
        onLocationChange={setLocation}
        onPrevious={() => {
          if (!location) return;
          const index = locations.findIndex(
            (item) =>
              item.book === location.book && item.chapter === location.chapter,
          );
          if (index > 0) setLocation(locations[index - 1]);
        }}
        onNext={() => {
          if (!location) return;
          const index = locations.findIndex(
            (item) =>
              item.book === location.book && item.chapter === location.chapter,
          );
          if (index >= 0 && index < locations.length - 1) {
            setLocation(locations[index + 1]);
          }
        }}
        onRegenerate={() => regenerate()}
        onPriorityChange={(id) => {
          if (id !== priorityId && regenerate(id)) {
            setPriorityId(id);
            setTranslationSpans([]);
          }
        }}
        onUndo={() => {
          const previous = undoStack.at(-1);
          if (!previous) return;
          setRedoStack((stack) => [...stack, combined]);
          setCombined(previous);
          setUndoStack((stack) => stack.slice(0, -1));
          setDirty(true);
        }}
        onRedo={() => {
          const next = redoStack.at(-1);
          if (!next) return;
          setUndoStack((stack) => [...stack, combined]);
          setCombined(next);
          setRedoStack((stack) => stack.slice(0, -1));
          setDirty(true);
        }}
      />

      <main>
        <div className="status" role="status">{message}</div>
        {(translations.length > 0 || manuscripts.length > 0) && (
          <SourceSettings
            manuscripts={manuscripts}
            translations={translations}
            associations={associations}
            filters={filters}
            onAssociationChange={(translationId, manuscriptId) => {
              setAssociations((current) => [
                ...current.filter(
                  (item) => item.translationId !== translationId,
                ),
                { translationId, manuscriptId },
              ]);
              setDirty(true);
            }}
            onFiltersChange={setFilters}
          />
        )}
        {!location && manuscripts.length > 0 && (
          <section className="empty-state">
            <h1>No common chapter</h1>
            <p>These manuscripts do not currently share a book and chapter.</p>
          </section>
        )}
        {!manuscripts.length && (
          <section className="empty-state">
            <h1>Compare manuscript witnesses</h1>
            <p>Load OSIS manuscripts to create an editable Combined edition.</p>
          </section>
        )}
        {location && (
          <div className="chapter">
            <h1 className="chapter-heading">{location.book} {location.chapter}</h1>
            {alignedVerses.filter((aligned) => {
              const draft = combined[aligned.reference.id];
              const active = Object.values(filters).some(Boolean);
              if (!active) return true;
              return (
                (filters.ties && draft?.columns.some((column) => column.needsReview))
                || (filters.manual && draft !== undefined && draft.manualText !== null)
                || (filters.missing && aligned.columns.some((column) =>
                  manuscripts.some((source) => !column.cells[source.id])))
                || (filters.uncertain && translationSpans.some((span) =>
                  span.verseId === aligned.reference.id
                  && span.confidence === "low"))
              );
            }).map((aligned) => {
              const draft = combined[aligned.reference.id]
                ?? generateCombined(aligned, manuscripts, priorityId);
              return (
                <VerseGrid
                  key={aligned.reference.id}
                  aligned={aligned}
                  manuscripts={manuscripts}
                  translations={translations}
                  associations={associationMap}
                  draft={draft}
                  spans={translationSpans}
                  onChoose={(columnIndex, sourceId) => {
                    if (draft.manualText !== null
                        && !window.confirm("Replace the manual text with aligned token choices?")) {
                      return;
                    }
                    const token = aligned.columns[columnIndex].cells[sourceId];
                    const columns = draft.columns.map((column, index) =>
                      index === columnIndex
                        ? {
                            text: token?.text ?? null,
                            sourceId,
                            needsReview: false,
                          }
                        : column,
                    );
                    commitCombined({
                      ...combined,
                      [aligned.reference.id]: { ...draft, columns, manualText: null },
                    });
                  }}
                  onCombinedChange={(text) =>
                    commitCombined({
                      ...combined,
                      [aligned.reference.id]: { ...draft, manualText: text },
                    })
                  }
                  onMoveSpan={(spanId, delta) => {
                    setTranslationSpans((spans) => spans.map((span) => {
                      if (span.id !== spanId) return span;
                      const width = span.columnEnd - span.columnStart;
                      const start = Math.max(
                        0,
                        Math.min(aligned.columns.length - width, span.columnStart + delta),
                      );
                      return { ...span, columnStart: start, columnEnd: start + width };
                    }));
                    setDirty(true);
                  }}
                  onResizeSpan={(spanId, delta) => {
                    setTranslationSpans((spans) => spans.map((span) =>
                      span.id === spanId
                        ? {
                            ...span,
                            columnEnd: Math.max(
                              span.columnStart + 1,
                              Math.min(aligned.columns.length, span.columnEnd + delta),
                            ),
                          }
                        : span,
                    ));
                    setDirty(true);
                  }}
                  onMergeSpan={(spanId) => {
                    setTranslationSpans((spans) => {
                      const selected = spans.find((span) => span.id === spanId);
                      if (!selected) return spans;
                      const next = spans
                        .filter((span) =>
                          span.translationId === selected.translationId
                          && span.verseId === selected.verseId
                          && span.tokenStart >= selected.tokenEnd)
                        .sort((left, right) => left.tokenStart - right.tokenStart)[0];
                      if (!next) return spans;
                      return spans
                        .filter((span) => span.id !== next.id)
                        .map((span) => span.id === selected.id
                          ? {
                              ...span,
                              tokenEnd: next.tokenEnd,
                              columnEnd: Math.max(span.columnEnd, next.columnEnd),
                              confidence: "low",
                            }
                          : span);
                    });
                    setDirty(true);
                  }}
                  onSplitSpan={(spanId) => {
                    setTranslationSpans((spans) => {
                      const selected = spans.find((span) => span.id === spanId);
                      if (!selected || selected.tokenEnd - selected.tokenStart < 2) {
                        return spans;
                      }
                      const tokenMiddle = Math.ceil(
                        (selected.tokenStart + selected.tokenEnd) / 2,
                      );
                      const hasColumnRoom =
                        selected.columnEnd - selected.columnStart >= 2;
                      const columnMiddle = hasColumnRoom
                        ? Math.ceil(
                            (selected.columnStart + selected.columnEnd) / 2,
                          )
                        : selected.columnStart;
                      return spans.flatMap((span) => span.id === selected.id
                        ? [
                            {
                              ...span,
                              tokenEnd: tokenMiddle,
                              columnEnd: hasColumnRoom
                                ? columnMiddle
                                : span.columnEnd,
                              confidence: "low" as const,
                            },
                            {
                              ...span,
                              id: `${span.id}:split:${tokenMiddle}`,
                              tokenStart: tokenMiddle,
                              columnStart: hasColumnRoom
                                ? columnMiddle
                                : span.columnStart,
                              confidence: "low" as const,
                            },
                          ]
                        : [span]);
                    });
                    setDirty(true);
                  }}
                />
              );
            })}
          </div>
        )}
      </main>
    </div>
  );
}
