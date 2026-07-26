from __future__ import annotations

from lxml import etree

from pdf2osis.models import Marker, VerseRecord
from pdf2osis.osis import OSIS_NS, build_osis
from pdf2osis.profiles import REV
from pdf2osis.validate import validate_osis


def test_osis_escapes_text_declares_work_and_interleaves_note() -> None:
    record = VerseRecord(
        chapter=1,
        verse="1",
        source_verse="1",
        page=16,
        hebrew="אלה & הסודות",
        english="These & mysteries",
        hebrew_markers=[Marker(4, "20")],
        english_markers=[Marker(6, "20")],
        notes={"20": "A & B"},
    )
    payload = build_osis([record], REV, "hebrew_commented")
    result = validate_osis(payload, REV, ["Rev.1.1"])
    assert result.notes == 1
    root = etree.fromstring(payload)
    namespace = {"osis": OSIS_NS}
    verse = root.xpath("//osis:verse", namespaces=namespace)[0]
    assert "".join(verse.itertext()) == "אלה A & B& הסודות"
    osis_text = root.find(f"{{{OSIS_NS}}}osisText")
    assert osis_text is not None
    assert osis_text.get("osisIDWork") == "MS.Oo.1.16.2_REV_Hebrew_Commented"


def test_combined_source_range_uses_stable_id_and_range_label() -> None:
    record = VerseRecord(
        chapter=14,
        verse="19",
        source_verse="19-20",
        page=239,
        alt_chapter=14,
        alt_verse="19",
        hebrew="והיקב",
        english="And the winepress",
    )
    payload = build_osis([record], REV, "hebrew")
    root = etree.fromstring(payload)
    namespace = {"osis": OSIS_NS}
    verse = root.xpath("//osis:verse", namespaces=namespace)[0]
    assert verse.get("osisID") == "Rev.14.19"
    assert verse.get("n") == "19-20"
    assert verse.get(f"{{{REV.alt_namespace}}}num") == "Rev.14.19"


def test_note_without_a_marker_is_not_guessed_onto_verse() -> None:
    record = VerseRecord(
        chapter=1,
        verse="1",
        source_verse="1",
        page=16,
        hebrew="אלה הסודות",
        english="These mysteries",
        notes={"20": "Interlinear source note"},
    )
    payload = build_osis([record], REV, "hebrew_commented")
    root = etree.fromstring(payload)
    namespace = {"osis": OSIS_NS}
    verse = root.xpath("//osis:verse", namespaces=namespace)[0]
    assert verse.xpath("./osis:note/@n", namespaces=namespace) == []
    assert "".join(verse.itertext()) == "אלה הסודות"
