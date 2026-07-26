from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import tempfile

from .extract import ExtractionError, extract_pdf
from .osis import build_osis
from .profiles import BookProfile
from .validate import validate_osis, validate_records


class ConversionError(RuntimeError):
    pass


@dataclass(frozen=True)
class ConversionReport:
    book: str
    input_path: Path
    output_paths: dict[str, Path]
    verses: int
    chapters: int
    empty_verses: tuple[str, ...]
    alternate_verses: int
    note_definitions: int
    emitted_notes: dict[str, int]
    transcription_interlinear_disagreements: int
    excluded_markers: tuple[str, ...]
    contamination_failures: tuple[str, ...]
    reference_comparison: dict[str, int] | None
    anomalies: tuple[str, ...]


def convert_pdf(
    input_path: str | Path,
    book_profile: BookProfile,
    output_dir: str | Path,
) -> ConversionReport:
    source = Path(input_path).resolve()
    destination = Path(output_dir).resolve()
    if not source.is_file():
        raise ConversionError(f"Input PDF not found: {source}")
    destination.mkdir(parents=True, exist_ok=True)

    try:
        records, definitions, anomalies = extract_pdf(source, book_profile)
    except ExtractionError as exc:
        raise ConversionError(str(exc)) from exc
    errors = validate_records(records, book_profile)
    if errors:
        raise ConversionError("Record validation failed:\n- " + "\n- ".join(errors))

    expected_ids = [
        f"{book_profile.osis_book}.{record.chapter}.{record.verse}"
        for record in records
    ]
    payloads: dict[str, bytes] = {}
    emitted_notes: dict[str, int] = {}
    for variant in book_profile.output_names():
        payload = build_osis(records, book_profile, variant)
        try:
            validation = validate_osis(
                payload,
                book_profile,
                expected_ids,
            )
        except (ValueError, TypeError) as exc:
            raise ConversionError(
                f"{variant} OSIS validation failed: {exc}"
            ) from exc
        payloads[variant] = payload
        emitted_notes[variant] = validation.notes

    output_paths = {
        variant: destination / filename
        for variant, filename in book_profile.output_names().items()
    }
    temporary_paths: dict[str, Path] = {}
    try:
        for variant, payload in payloads.items():
            handle, temporary_name = tempfile.mkstemp(
                prefix=f".{output_paths[variant].name}.",
                suffix=".tmp",
                dir=destination,
            )
            temporary = Path(temporary_name)
            temporary_paths[variant] = temporary
            with os.fdopen(handle, "wb") as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
        for variant, temporary in temporary_paths.items():
            os.replace(temporary, output_paths[variant])
    finally:
        for temporary in temporary_paths.values():
            if temporary.exists():
                temporary.unlink()

    return ConversionReport(
        book=book_profile.key,
        input_path=source,
        output_paths=output_paths,
        verses=len(records),
        chapters=len({record.chapter for record in records}),
        empty_verses=tuple(record.label for record in records if record.empty),
        alternate_verses=sum(
            record.alt_verse is not None
            for record in records
        ),
        note_definitions=len(definitions),
        emitted_notes=emitted_notes,
        transcription_interlinear_disagreements=sum(
            len(record.extraction_disagreements)
            for record in records
        ),
        excluded_markers=tuple(
            f"{book_profile.osis_book}.{record.chapter}.{record.verse}: {item}"
            for record in records
            for item in record.excluded_markers
        ),
        contamination_failures=(),
        reference_comparison=None,
        anomalies=tuple(anomalies),
    )
