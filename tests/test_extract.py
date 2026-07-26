from __future__ import annotations

from pdf2osis.extract import (
    _reconcile_hebrew,
    hebrew_text_and_markers,
    parse_header,
    suffix_repeated_verses,
)
from pdf2osis.models import Marker, VerseRecord
from pdf2osis.profiles import JAS, REV


def _span(text: str, x0: float, size: float = 14, flags: int = 4) -> dict:
    return {
        "text": text,
        "size": size,
        "flags": flags,
        "bbox": (x0, 0, x0 + 20, 15),
    }


def test_revelation_combined_header_preserves_source_range() -> None:
    record = parse_header(
        "Revelation 14:19-20 (Cochin 14:19)",
        REV,
        239,
    )
    assert record is not None
    assert record.chapter == 14
    assert record.verse == "19"
    assert record.source_verse == "19-20"
    assert (record.alt_chapter, record.alt_verse) == (14, "19")


def test_james_parenthetical_range_is_retained() -> None:
    record = parse_header("James 2:15 (KJV 2:15-16)", JAS, 33)
    assert record is not None
    assert record.verse == "15"
    assert record.alt_verse == "15-16"


def test_repeated_unsuffixed_verses_receive_stable_suffixes() -> None:
    records = [
        VerseRecord(2, "21", 50, source_verse="21"),
        VerseRecord(2, "21", 51, source_verse="21", alt_chapter=2, alt_verse="21"),
    ]
    suffix_repeated_verses(records)
    assert [record.verse for record in records] == ["21a", "21b"]
    assert [record.source_verse for record in records] == ["21a", "21b"]
    assert records[1].alt_verse == "21b"


def test_rtl_span_reconstruction_and_marker_position() -> None:
    block = {
        "lines": [
            {
                "spans": [
                    _span(" דבע בקעי", 100),
                    _span("22", 90, size=9, flags=5),
                ]
            }
        ]
    }
    text, markers = hebrew_text_and_markers(block, JAS)
    assert text == "יעקב עבד"
    assert [(marker.offset, marker.number) for marker in markers] == [(9, "22")]


def test_interlinear_corrects_equal_length_glyph_mapping() -> None:
    record = VerseRecord(
        chapter=1,
        verse="6",
        page=21,
        transcription_hebrew="לפני ח֞ ואביו",
        interlinear_hebrew="לפני ה֞ ואביו",
        transcription_markers=[Marker(4, "38")],
        interlinear_markers=[Marker(7, "39")],
    )
    _reconcile_hebrew(record)
    assert record.hebrew == "לפני ה֞ ואביו"
    assert {marker.number for marker in record.hebrew_markers} == {"38", "39"}


def test_interlinear_does_not_replace_unrelated_reordered_words() -> None:
    record = VerseRecord(
        chapter=17,
        verse="11",
        page=275,
        transcription_hebrew="והחיה שהיה דלא יש זה השמיני",
        interlinear_hebrew="דלא שהיה והחיה והוא השמיני זה",
    )
    _reconcile_hebrew(record)
    assert record.hebrew == "והחיה שהיה דלא יש זה השמיני"
    assert record.extraction_disagreements
