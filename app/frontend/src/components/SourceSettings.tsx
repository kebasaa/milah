import type {
  SourceDocument,
  TranslationAssociation,
} from "../core/types";

export interface ReviewFilters {
  ties: boolean;
  manual: boolean;
  missing: boolean;
  uncertain: boolean;
}

interface SourceSettingsProps {
  manuscripts: SourceDocument[];
  translations: SourceDocument[];
  associations: TranslationAssociation[];
  filters: ReviewFilters;
  onAssociationChange(translationId: string, manuscriptId: string): void;
  onFiltersChange(filters: ReviewFilters): void;
}

export function SourceSettings(props: SourceSettingsProps) {
  return (
    <aside className="workspace-settings">
      <div className="source-summary" aria-label="Loaded manuscript coverage">
        {props.manuscripts.map((manuscript) => {
          const locations = new Set(
            Object.values(manuscript.verses).map(
              (verse) => `${verse.reference.book}.${verse.reference.chapter}`,
            ),
          );
          return (
            <span key={manuscript.id} title={manuscript.warnings.join("\n")}>
              {manuscript.metadata.title || manuscript.name}:{" "}
              {Object.keys(manuscript.verses).length} verses,{" "}
              {locations.size} chapters
              {manuscript.warnings.length ? " ⚠" : ""}
            </span>
          );
        })}
      </div>
      {props.translations.map((translation) => (
        <label key={translation.id}>
          <span title={translation.warnings.join("\n")}>
            {translation.metadata.title || translation.name}
            {translation.warnings.length ? " ⚠" : ""}
          </span>
          <select
            aria-label={`Manuscript for ${translation.name}`}
            value={
              props.associations.find(
                (item) => item.translationId === translation.id,
              )?.manuscriptId ?? ""
            }
            onChange={(event) =>
              props.onAssociationChange(translation.id, event.target.value)
            }
          >
            <option value="">Not displayed</option>
            {props.manuscripts.map((manuscript) => (
              <option key={manuscript.id} value={manuscript.id}>
                {manuscript.metadata.title || manuscript.name}
              </option>
            ))}
          </select>
        </label>
      ))}
      <fieldset>
        <legend>Review filters</legend>
        {(Object.keys(props.filters) as Array<keyof ReviewFilters>).map((key) => (
          <label key={key}>
            <input
              type="checkbox"
              checked={props.filters[key]}
              onChange={(event) =>
                props.onFiltersChange({
                  ...props.filters,
                  [key]: event.target.checked,
                })
              }
            />
            {key === "ties" ? "Consensus ties" : key}
          </label>
        ))}
      </fieldset>
    </aside>
  );
}
