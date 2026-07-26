import { combinedText } from "../core/alignment";
import type {
  AlignedVerse,
  CombinedDraft,
  SourceDocument,
  TranslationSpan,
} from "../core/types";

interface VerseGridProps {
  aligned: AlignedVerse;
  manuscripts: SourceDocument[];
  translations: SourceDocument[];
  associations: Record<string, string>;
  draft: CombinedDraft;
  spans: TranslationSpan[];
  onChoose(columnIndex: number, sourceId: string): void;
  onCombinedChange(text: string): void;
  onMoveSpan(spanId: string, delta: number): void;
  onResizeSpan(spanId: string, delta: number): void;
  onMergeSpan(spanId: string): void;
  onSplitSpan(spanId: string): void;
}

function tokenTitle(source: SourceDocument, verseId: string, tokenIndex: number) {
  const token = source.verses[verseId]?.tokens[tokenIndex];
  if (!token?.notes.length) return undefined;
  return token.notes
    .map((note) => `${note.number ? `${note.number}. ` : ""}${note.text}`)
    .join("\n\n");
}

export function VerseGrid(props: VerseGridProps) {
  const verseId = props.aligned.reference.id;
  const columnStyle = {
    gridTemplateColumns: `repeat(${Math.max(1, props.aligned.columns.length)}, minmax(5.2rem, 1fr))`,
  };

  return (
    <section className="verse-card">
      <h2>{props.aligned.reference.book} {props.aligned.reference.chapter}.{props.aligned.reference.verse}</h2>
      <div className="comparison-scroll">
        {props.manuscripts.map((source) => (
          <div className="source-group" key={source.id}>
            <div className="aligned-row manuscript-row" style={columnStyle} dir="rtl">
              {props.aligned.columns.map((column, columnIndex) => {
                const token = column.cells[source.id];
                const tokenIndex = token
                  ? source.verses[verseId]?.tokens.findIndex((item) => item.id === token.id)
                  : -1;
                const differs =
                  token?.text !== props.draft.columns[columnIndex]?.text;
                return (
                  <button
                    className={`token ${differs ? "variant" : "agrees"} ${token?.notes.length ? "has-note" : ""}`}
                    disabled={!token}
                    key={column.id}
                    title={tokenIndex >= 0 ? tokenTitle(source, verseId, tokenIndex) : undefined}
                    data-tooltip={
                      tokenIndex >= 0 ? tokenTitle(source, verseId, tokenIndex) : undefined
                    }
                    aria-label={token?.notes.length
                      ? `${token.text}. Comments: ${token.notes
                          .map((note) => note.text).join("; ")}`
                      : token?.text ?? "Missing reading"}
                    onClick={() => token && props.onChoose(columnIndex, source.id)}
                  >
                    {token?.text ?? <span className="gap">∅</span>}
                    {token?.notes.length ? <sup>●</sup> : null}
                  </button>
                );
              })}
            </div>
            <div className="row-label" title={source.warnings.join("\n")}>
              {source.metadata.title || source.name}
              {source.warnings.length ? " ⚠" : ""}
            </div>
            {props.translations
              .filter((translation) => props.associations[translation.id] === source.id)
              .map((translation) => {
                const tokens = translation.verses[verseId]?.tokens ?? [];
                return (
                  <div className="translation-wrap" key={translation.id}>
                    <div
                      className="aligned-row translation-row"
                      style={columnStyle}
                      dir="rtl"
                    >
                      {props.spans
                        .filter(
                          (span) =>
                            span.translationId === translation.id
                            && span.verseId === verseId,
                        )
                        .map((span) => (
                          <span
                            className={`translation-token ${span.confidence === "low" ? "uncertain" : ""}`}
                            key={span.id}
                            style={{
                              gridColumn: `${span.columnStart + 1} / ${span.columnEnd + 1}`,
                            }}
                            title={span.confidence === "low"
                              ? "Automatic alignment—use the controls to correct it."
                              : "Aligned translation"}
                          >
                            {tokens.slice(span.tokenStart, span.tokenEnd)
                              .map((token) => token.text).join(" ")}
                            <span className="span-controls">
                              <button
                                aria-label="Move translation span left"
                                onClick={() => props.onMoveSpan(span.id, -1)}
                              >←</button>
                              <button
                                aria-label="Move translation span right"
                                onClick={() => props.onMoveSpan(span.id, 1)}
                              >→</button>
                              <button
                                aria-label="Narrow translation span"
                                onClick={() => props.onResizeSpan(span.id, -1)}
                              >−</button>
                              <button
                                aria-label="Widen translation span"
                                onClick={() => props.onResizeSpan(span.id, 1)}
                              >+</button>
                              <button
                                aria-label="Merge with the next translation span"
                                onClick={() => props.onMergeSpan(span.id)}
                              >⧉</button>
                              <button
                                aria-label="Split this translation span"
                                onClick={() => props.onSplitSpan(span.id)}
                              >⋮</button>
                            </span>
                          </span>
                        ))}
                    </div>
                    <div className="row-label translation-label">
                      {translation.metadata.title || translation.name}
                    </div>
                  </div>
                );
              })}
          </div>
        ))}
        <div className="combined-wrap">
          <div
            className="aligned-row combined-token-row"
            style={columnStyle}
            dir="rtl"
          >
            {props.draft.columns.map((column, index) => (
              <span
                className={`combined-token ${column.needsReview ? "needs-review" : ""}`}
                key={`${verseId}:combined:${index}`}
              >
                {column.text ?? <span className="gap">∅</span>}
              </span>
            ))}
          </div>
          <div className="row-label">Combined</div>
        </div>
        <div className="combined-editor">
          <textarea
            dir="auto"
            value={combinedText(props.draft)}
            onChange={(event) => props.onCombinedChange(event.target.value)}
            aria-label={`Combined text for ${verseId}`}
          />
          {props.draft.columns.some((column) => column.needsReview) && (
            <span className="review-flag">Review tie</span>
          )}
          {props.draft.manualText !== null && (
            <span className="manual-flag">Manually edited</span>
          )}
        </div>
      </div>
    </section>
  );
}
