from __future__ import annotations

from dataclasses import asdict, dataclass
from pathlib import Path
import re
import unicodedata

from lxml import etree

from .osis import OSIS_NS
from .profiles import BookProfile
from .validate import FORBIDDEN_TEXT

NS = {"osis": OSIS_NS}
MARKER_DIGITS_RE = re.compile(
    r"(?<=[^\W\d_])\d{1,3}(?=\s|[.,;:!?\"'”’)]|$)",
    re.UNICODE,
)


@dataclass(frozen=True)
class VariantComparison:
    variant: str
    generated_records: int
    reference_records: int
    common_records: int
    exact_text: int
    exact_note_free_text: int
    exact_hebrew_letters: int
    only_generated: tuple[str, ...]
    only_reference: tuple[str, ...]
    generated_latin_hebrew: tuple[str, ...]
    reference_shifted: tuple[str, ...]
    reference_contaminated: tuple[str, ...]
    source_text_differences: tuple[str, ...]
    reference_malformed_xml: bool


@dataclass(frozen=True)
class ReferenceComparisonReport:
    book: str
    variants: tuple[VariantComparison, ...]
    generated_regressions: int
    reference_defects: int
    classified_text_differences: int

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class _VerseText:
    full: str
    note_free: str
    marker_stripped: str


def _normalise(text: str) -> str:
    return " ".join(text.split())


def _hebrew_letters(text: str) -> str:
    return "".join(
        char
        for char in text
        if "\u0590" <= char <= "\u05ff"
        and unicodedata.category(char) != "Mn"
    )


def _verse_text(verse: etree._Element) -> _VerseText:
    full = _normalise("".join(verse.itertext()))
    parts = [verse.text or ""]
    for child in verse:
        if etree.QName(child).localname != "note":
            parts.extend(child.itertext())
        parts.append(child.tail or "")
    note_free = _normalise("".join(parts))
    return _VerseText(
        full=full,
        note_free=note_free,
        marker_stripped=_normalise(MARKER_DIGITS_RE.sub("", note_free)),
    )


def _load(
    path: Path,
    *,
    recover: bool,
) -> tuple[list[str], dict[str, _VerseText], bool]:
    malformed = False
    if recover:
        try:
            etree.parse(
                str(path),
                etree.XMLParser(resolve_entities=False, no_network=True),
            )
        except etree.XMLSyntaxError:
            malformed = True
    parser = etree.XMLParser(
        resolve_entities=False,
        no_network=True,
        recover=recover,
    )
    root = etree.parse(str(path), parser)
    verses = root.xpath("//osis:verse", namespaces=NS)
    order = [verse.get("osisID") or "" for verse in verses]
    return order, {
        verse.get("osisID") or "": _verse_text(verse)
        for verse in verses
    }, malformed


def _nearby_reference_matches(
    generated_order: list[str],
    reference_order: list[str],
    generated: dict[str, _VerseText],
    reference: dict[str, _VerseText],
) -> set[str]:
    reference_positions = {
        verse_id: index for index, verse_id in enumerate(reference_order)
    }
    shifted: set[str] = set()
    for verse_id in set(generated) & set(reference):
        value = generated[verse_id].marker_stripped
        if not value or value == reference[verse_id].marker_stripped:
            continue
        position = reference_positions[verse_id]
        candidates = reference_order[max(0, position - 2): position + 3]
        if any(
            candidate != verse_id
            and reference[candidate].marker_stripped == value
            for candidate in candidates
        ):
            shifted.add(verse_id)
    return shifted


def compare_directories(
    generated_dir: str | Path,
    reference_dir: str | Path,
    profile: BookProfile,
) -> ReferenceComparisonReport:
    generated_root = Path(generated_dir)
    reference_root = Path(reference_dir)
    comparisons: list[VariantComparison] = []
    generated_regressions = 0
    reference_defects = 0
    classified_differences = 0

    for variant, filename in profile.output_names().items():
        generated_path = generated_root / filename
        reference_path = reference_root / filename
        if not generated_path.is_file():
            raise ValueError(f"Generated OSIS not found: {generated_path}")
        if not reference_path.is_file():
            raise ValueError(f"Reference OSIS not found: {reference_path}")

        generated_order, generated, _ = _load(generated_path, recover=False)
        reference_order, reference, malformed = _load(
            reference_path,
            recover=True,
        )
        common = set(generated) & set(reference)
        exact = {
            verse_id
            for verse_id in common
            if generated[verse_id].full == reference[verse_id].full
        }
        note_free_exact = {
            verse_id
            for verse_id in common
            if generated[verse_id].marker_stripped
            == reference[verse_id].marker_stripped
        }
        hebrew_letter_exact = {
            verse_id
            for verse_id in common
            if _hebrew_letters(generated[verse_id].note_free)
            == _hebrew_letters(reference[verse_id].note_free)
        }
        different = common - note_free_exact
        shifted = _nearby_reference_matches(
            generated_order,
            reference_order,
            generated,
            reference,
        )
        contaminated_reference = {
            verse_id
            for verse_id in different
            if any(
                phrase in reference[verse_id].full
                for phrase in FORBIDDEN_TEXT
            )
        }
        latin_hebrew = {
            verse_id
            for verse_id, text in generated.items()
            if variant.startswith("hebrew")
            and re.search(r"[A-Za-z]", text.note_free)
        }
        source_differences = (
            different - shifted - contaminated_reference - latin_hebrew
        )

        generated_regressions += len(latin_hebrew)
        reference_defects += (
            int(malformed)
            + len(set(reference) - set(generated))
            + len(shifted)
            + len(contaminated_reference)
        )
        classified_differences += len(different)
        comparisons.append(
            VariantComparison(
                variant=variant,
                generated_records=len(generated_order),
                reference_records=len(reference_order),
                common_records=len(common),
                exact_text=len(exact),
                exact_note_free_text=len(note_free_exact),
                exact_hebrew_letters=(
                    len(hebrew_letter_exact)
                    if variant.startswith("hebrew")
                    else 0
                ),
                only_generated=tuple(sorted(set(generated) - set(reference))),
                only_reference=tuple(sorted(set(reference) - set(generated))),
                generated_latin_hebrew=tuple(sorted(latin_hebrew)),
                reference_shifted=tuple(sorted(shifted)),
                reference_contaminated=tuple(sorted(contaminated_reference)),
                source_text_differences=tuple(sorted(source_differences)),
                reference_malformed_xml=malformed,
            )
        )

    return ReferenceComparisonReport(
        book=profile.key,
        variants=tuple(comparisons),
        generated_regressions=generated_regressions,
        reference_defects=reference_defects,
        classified_text_differences=classified_differences,
    )
