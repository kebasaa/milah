import type { Location, SourceDocument } from "../core/types";

interface ToolbarProps {
  manuscripts: SourceDocument[];
  priorityId: string;
  locations: Location[];
  location: Location | null;
  dirty: boolean;
  canUndo: boolean;
  canRedo: boolean;
  onLoadManuscripts(): void;
  onLoadTranslations(): void;
  onOpenProject(): void;
  onSaveProject(): void;
  onExport(): void;
  onPriorityChange(id: string): void;
  onLocationChange(location: Location): void;
  onRegenerate(): void;
  onUndo(): void;
  onRedo(): void;
  onPrevious(): void;
  onNext(): void;
}

export function Toolbar(props: ToolbarProps) {
  const locationValue = props.location
    ? `${props.location.book}.${props.location.chapter}`
    : "";
  const locationIndex = props.locations.findIndex(
    (item) =>
      item.book === props.location?.book
      && item.chapter === props.location?.chapter,
  );
  return (
    <header className="toolbar">
      <div className="brand">
        <span className="brand-mark" aria-hidden="true">מ</span>
        <strong>Milah</strong>
        {props.dirty && <span className="dirty" title="Unsaved changes">●</span>}
      </div>
      <div className="toolbar-actions">
        <button onClick={props.onLoadManuscripts}>Load manuscripts</button>
        <button onClick={props.onLoadTranslations}>Load translations</button>
        <button onClick={props.onOpenProject}>Open project</button>
        <button onClick={props.onSaveProject} disabled={!props.manuscripts.length}>
          Save project
        </button>
        <button onClick={props.onExport} disabled={!props.manuscripts.length}>
          Export Combined
        </button>
      </div>
      <div className="toolbar-settings">
        <label>
          Chapter
          <select
            value={locationValue}
            disabled={!props.locations.length}
            onChange={(event) => {
              const selected = props.locations.find(
                (item) => `${item.book}.${item.chapter}` === event.target.value,
              );
              if (selected) props.onLocationChange(selected);
            }}
          >
            {props.locations.map((item) => (
              <option
                key={`${item.book}.${item.chapter}`}
                value={`${item.book}.${item.chapter}`}
              >
                {item.book} {item.chapter}
              </option>
            ))}
          </select>
        </label>
        <button
          onClick={props.onPrevious}
          disabled={!props.location || locationIndex <= 0}
          aria-label="Previous common chapter"
        >‹</button>
        <button
          onClick={props.onNext}
          disabled={
            !props.location
            || locationIndex >= props.locations.length - 1
          }
          aria-label="Next common chapter"
        >›</button>
        <label>
          Priority
          <select
            value={props.priorityId}
            disabled={!props.manuscripts.length}
            onChange={(event) => props.onPriorityChange(event.target.value)}
          >
            {props.manuscripts.map((source) => (
              <option key={source.id} value={source.id}>
                {source.metadata.title || source.name}
              </option>
            ))}
          </select>
        </label>
        <button onClick={props.onRegenerate} disabled={!props.manuscripts.length}>
          Regenerate
        </button>
        <button onClick={props.onUndo} disabled={!props.canUndo} aria-label="Undo">
          ↶
        </button>
        <button onClick={props.onRedo} disabled={!props.canRedo} aria-label="Redo">
          ↷
        </button>
      </div>
    </header>
  );
}
