from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re

from lxml import etree

from .models import VerseRecord
from .osis import OSIS_NS
from .profiles import BookProfile

FORBIDDEN_TEXT = (
    "The Return Letter of James",
    "The Scroll of Mysteries: Cochin Hebrew Revelation",
    "Image courtesy of MidJourney",
    "The Covenant with Yehovah",
    "Ketubah Covenant",
    "KJV:",
    "The Scriptures:",
    "Interlinear Chart",
)
INTERLINEAR_GLOSS_MARKERS = (
    "Pa’al/Qal",
    "pronom)",
    "(n ms)",
)
LOCAL_SCHEMA = (
    Path(__file__).with_name("schema")
    / "osisCore.2.1.1-project-subset.xsd"
)


@dataclass
class ValidationResult:
    verse_ids: list[str]
    notes: int


def validate_records(
    records: list[VerseRecord],
    profile: BookProfile,
) -> list[str]:
    errors: list[str] = []
    if len(records) != profile.expected_verses:
        errors.append(
            f"expected {profile.expected_verses} verses, found {len(records)}"
        )
    chapters = sorted({record.chapter for record in records})
    expected_chapters = list(range(1, profile.expected_chapters + 1))
    if chapters != expected_chapters:
        errors.append(f"unexpected chapters: {chapters}")
    ids = [
        f"{profile.osis_book}.{record.chapter}.{record.verse}"
        for record in records
    ]
    duplicates = [key for key, count in Counter(ids).items() if count > 1]
    if duplicates:
        errors.append("duplicate verse IDs: " + ", ".join(duplicates))
    first_text = next(
        (record.hebrew for record in records if record.hebrew),
        "",
    )
    if not first_text.startswith(profile.expected_hebrew_prefix):
        errors.append(
            "Hebrew RTL reconstruction failed: first text starts with "
            f"{first_text[:40]!r}"
        )
    for record in records:
        if record.empty:
            continue
        if not record.hebrew:
            errors.append(f"{profile.osis_book} {record.label} has no Hebrew")
        if re.search(r"[A-Za-z]", record.hebrew):
            errors.append(
                f"{profile.osis_book} {record.label} has Latin contamination "
                "in clean Hebrew"
            )
        if not record.english:
            errors.append(f"{profile.osis_book} {record.label} has no translation")
        combined = record.hebrew + " " + record.english
        for phrase in FORBIDDEN_TEXT:
            if phrase in combined:
                errors.append(
                    f"{profile.osis_book} {record.label} contains {phrase!r}"
                )
        for marker in INTERLINEAR_GLOSS_MARKERS:
            if marker in record.english:
                errors.append(
                    f"{profile.osis_book} {record.label} contains "
                    f"interlinear gloss marker {marker!r}"
                )
    return errors


def validate_osis(
    payload: bytes,
    profile: BookProfile,
    expected_ids: list[str],
) -> ValidationResult:
    parser = etree.XMLParser(resolve_entities=False, no_network=True)
    root = etree.fromstring(payload, parser)
    schema = etree.XMLSchema(etree.parse(str(LOCAL_SCHEMA)))
    if not schema.validate(root):
        error = schema.error_log.last_error
        raise ValueError(
            "local OSIS schema validation failed"
            + (f": {error.message}" if error is not None else "")
        )
    namespace = {"osis": OSIS_NS}
    osis_text = root.find("osis:osisText", namespace)
    if osis_text is None:
        raise ValueError("missing osisText")
    work_id = osis_text.get("osisIDWork")
    ref_work = osis_text.get("osisRefWork")
    declared = {
        work.get("osisWork")
        for work in root.xpath("//osis:header/osis:work", namespaces=namespace)
    }
    if work_id not in declared:
        raise ValueError(f"osisIDWork {work_id!r} is not declared")
    if ref_work not in declared:
        raise ValueError(f"osisRefWork {ref_work!r} is not declared")
    book = root.xpath(
        "//osis:div[@type='book' and @osisID=$book]",
        namespaces=namespace,
        book=profile.osis_book,
    )
    if len(book) != 1:
        raise ValueError("expected exactly one book div")
    verses = root.xpath("//osis:verse", namespaces=namespace)
    ids = [verse.get("osisID") for verse in verses]
    if ids != expected_ids:
        raise ValueError("OSIS verse coverage/order differs from parsed records")
    if len(ids) != len(set(ids)):
        raise ValueError("OSIS contains duplicate verse IDs")
    text = " ".join(root.itertext())
    contamination = [phrase for phrase in FORBIDDEN_TEXT if phrase in text]
    if contamination:
        raise ValueError("OSIS contains excluded text: " + ", ".join(contamination))
    notes = len(root.xpath("//osis:note", namespaces=namespace))
    return ValidationResult(ids, notes)
