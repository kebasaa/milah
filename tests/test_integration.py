from __future__ import annotations

from pathlib import Path
import re

from lxml import etree
import pytest

from pdf2osis.compare import compare_directories
from pdf2osis.converter import convert_pdf
from pdf2osis.extract import extract_pdf
from pdf2osis.osis import OSIS_NS
from pdf2osis.profiles import JAS, REV

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "00_source_files"
REFERENCE = ROOT / "data" / "01b_osis_reference"


@pytest.mark.parametrize(
    ("profile", "filename", "count", "chapters"),
    [
        (REV, REV.default_pdf, 404, 22),
        (JAS, JAS.default_pdf, 107, 5),
    ],
)
def test_full_pdf_record_coverage(
    profile,
    filename: str,
    count: int,
    chapters: int,
) -> None:
    records, definitions, anomalies = extract_pdf(SOURCE / filename, profile)
    assert len(records) == count
    assert len({record.chapter for record in records}) == chapters
    assert records[0].hebrew.startswith(profile.expected_hebrew_prefix)
    assert definitions
    assert all(
        marker.number in definitions
        for record in records
        for marker in record.hebrew_markers + record.english_markers
        if marker.number in record.notes
    )
    assert not any("markers without definitions" in item for item in anomalies)
    assert not any(re.search(r"[A-Za-z]", record.hebrew) for record in records)


def test_james_source_specific_structure() -> None:
    records, _, _ = extract_pdf(SOURCE / JAS.default_pdf, JAS)
    by_id = {(record.chapter, record.verse): record for record in records}
    assert by_id[(1, "21")].empty
    assert by_id[(2, "15")].alt_verse == "15-16"
    # The PDF combines KJV 2:15-16 and consequently has 25 source records
    # in chapter 2. Inventing or duplicating a Jas.2.26 record would alter it.
    assert (2, "26") not in by_id
    assert "The Covenant with Yehovah" not in by_id[(5, "20")].english


def test_revelation_source_specific_structure() -> None:
    records, definitions, anomalies = extract_pdf(SOURCE / REV.default_pdf, REV)
    by_id = {(record.chapter, record.verse): record for record in records}
    assert by_id[(14, "19")].source_verse == "19-20"
    assert by_id[(14, "19")].hebrew
    assert by_id[(14, "19")].english
    assert by_id[(22, "21")].english.startswith("May the grace")
    assert "Pa’al/Qal" not in by_id[(1, "1")].english
    assert by_id[(1, "1")].english.endswith("John.")
    assert not re.search(r"[A-Za-z]", by_id[(1, "1")].hebrew)
    assert by_id[(1, "6")].hebrew.startswith(
        "ועשה אותנו למלכים ולכהנים לפני ה֞"
    )
    assert {
        marker.number for marker in by_id[(1, "1")].hebrew_markers
    } == {"18", "19", "22", "23"}
    assert {
        marker.number for marker in by_id[(1, "1")].english_markers
    } == {"20"}
    assert [record.verse for record in records if record.chapter == 2 and record.verse.startswith("21")] == [
        "21a",
        "21b",
    ]
    assert definitions["90"].startswith("Pergamum is an older spelling")
    assert not any("footnote 90 has multiple definitions" in item for item in anomalies)
    assert not any("90" in record.notes for record in records)
    assert any(
        item.startswith("definitions without verse markers:") and "90" in item
        for item in anomalies
    )


def test_conversion_is_deterministic_and_variants_share_coverage(
    tmp_path: Path,
) -> None:
    first = tmp_path / "first"
    second = tmp_path / "second"
    report_one = convert_pdf(SOURCE / JAS.default_pdf, JAS, first)
    report_two = convert_pdf(SOURCE / JAS.default_pdf, JAS, second)
    assert report_one.verses == report_two.verses == 107
    namespace = {"osis": OSIS_NS}
    coverages = []
    for variant, name in JAS.output_names().items():
        first_bytes = (first / name).read_bytes()
        second_bytes = (second / name).read_bytes()
        assert first_bytes == second_bytes
        root = etree.fromstring(first_bytes)
        coverages.append(
            root.xpath("//osis:verse/@osisID", namespaces=namespace)
        )
    assert coverages[0] == coverages[1] == coverages[2]


def test_revelation_reference_audit_classifies_known_reference_defects(
    tmp_path: Path,
) -> None:
    generated = tmp_path / "generated"
    convert_pdf(SOURCE / REV.default_pdf, REV, generated)
    report = compare_directories(generated, REFERENCE, REV)
    assert report.generated_regressions == 0
    by_variant = {item.variant: item for item in report.variants}
    assert by_variant["hebrew"].generated_records == 404
    assert by_variant["hebrew"].reference_records == 406
    assert by_variant["hebrew"].reference_malformed_xml
    assert "Rev.2.26" in by_variant["hebrew"].only_reference
    assert "Rev.20.12" in by_variant["hebrew"].only_reference
    assert not by_variant["hebrew"].generated_latin_hebrew
    assert by_variant["hebrew"].exact_note_free_text >= 200
    assert by_variant["hebrew"].exact_hebrew_letters >= 280
    assert by_variant["translation"].reference_shifted
