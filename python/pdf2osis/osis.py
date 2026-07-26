from __future__ import annotations

from collections.abc import Iterable

from lxml import etree

from .models import Marker, VerseRecord
from .profiles import BookProfile

OSIS_NS = "http://www.bibletechnologies.net/2003/OSIS/namespace"
XSI_NS = "http://www.w3.org/2001/XMLSchema-instance"
XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"
SCHEMA_LOCATION = (
    f"{OSIS_NS} http://www.bibletechnologies.net/osisCore.2.1.1.xsd"
)


def _tag(name: str) -> str:
    return f"{{{OSIS_NS}}}{name}"


def _work(
    header: etree._Element,
    work_id: str,
    title: str,
    language: str,
    profile: BookProfile,
    *,
    translation: bool = False,
) -> None:
    work = etree.SubElement(header, _tag("work"), osisWork=work_id)
    etree.SubElement(work, _tag("title")).text = title
    etree.SubElement(work, _tag("scope")).text = profile.scope
    etree.SubElement(
        work,
        _tag("type"),
        type="x-bible" if translation else "x-manuscript",
    ).text = "Edition" if translation else "Manuscript"
    etree.SubElement(work, _tag("identifier"), type="OSIS").text = work_id
    etree.SubElement(work, _tag("identifier"), type="shelfmark").text = (
        profile.manuscript
    )
    etree.SubElement(work, _tag("identifier"), type="URI").text = (
        profile.alt_namespace
    )
    etree.SubElement(work, _tag("publisher")).text = "Project Truth Ministries"
    if translation:
        etree.SubElement(work, _tag("creator"), role="trl").text = (
            "Project Truth Ministries"
        )
        etree.SubElement(
            work,
            _tag("contributor"),
            role="trc",
            **{"file-as": "Baca, Janice F."},
        ).text = "Janice F. Baca"
        etree.SubElement(work, _tag("date"), event="eversion", type="ISO").text = (
            "2024"
        )
        etree.SubElement(work, _tag("rights")).text = (
            "© copyright 2024 Janice F. Baca"
        )
    else:
        etree.SubElement(work, _tag("date"), event="original", type="ISO").text = (
            "ca. 1730"
        )
    etree.SubElement(work, _tag("language")).text = language
    etree.SubElement(work, _tag("description")).text = profile.description


def _header(
    osis_text: etree._Element,
    profile: BookProfile,
    work_id: str,
    language: str,
    *,
    translation: bool,
) -> None:
    header = etree.SubElement(osis_text, _tag("header"))
    title = profile.translation_title if translation else profile.title
    _work(
        header,
        work_id,
        title,
        language,
        profile,
        translation=translation,
    )
    bible = etree.SubElement(header, _tag("work"), osisWork="bible")
    etree.SubElement(bible, _tag("title")).text = (
        "Referenced versification (standard)"
    )
    etree.SubElement(bible, _tag("identifier"), type="OSIS").text = "bible"
    etree.SubElement(bible, _tag("refSystem")).text = "StandardV11N"
    etree.SubElement(bible, _tag("language")).text = language


def _interleave_notes(
    verse: etree._Element,
    text: str,
    markers: Iterable[Marker],
    notes: dict[str, str],
) -> None:
    previous = 0
    last: etree._Element | None = None
    emitted: set[tuple[int, str]] = set()
    for marker in sorted(markers, key=lambda item: item.offset):
        key = (marker.offset, marker.number)
        if key in emitted or marker.number not in notes:
            continue
        emitted.add(key)
        offset = max(previous, min(marker.offset, len(text)))
        chunk = text[previous:offset]
        if last is None:
            verse.text = (verse.text or "") + chunk
        else:
            last.tail = (last.tail or "") + chunk
        last = etree.SubElement(
            verse,
            _tag("note"),
            type="footnote",
            n=marker.number,
        )
        last.text = notes[marker.number]
        previous = offset
    remainder = text[previous:]
    if last is None:
        verse.text = (verse.text or "") + remainder
    else:
        last.tail = (last.tail or "") + remainder


def _alt_reference(profile: BookProfile, chapter: int, verse: str) -> str:
    if "-" not in verse:
        return f"{profile.osis_book}.{chapter}.{verse}"
    start, end = verse.split("-", 1)
    return (
        f"{profile.osis_book}.{chapter}.{start}-"
        f"{profile.osis_book}.{chapter}.{end}"
    )


def build_osis(
    records: list[VerseRecord],
    profile: BookProfile,
    variant: str,
) -> bytes:
    if variant not in {"hebrew", "hebrew_commented", "translation"}:
        raise ValueError(f"Unknown OSIS variant: {variant}")
    translation = variant == "translation"
    commented = variant == "hebrew_commented"
    work_id = (
        profile.translation_work
        if translation
        else profile.hebrew_work + ("_Commented" if commented else "")
    )
    language = "en" if translation else "he"
    nsmap = {
        None: OSIS_NS,
        "xsi": XSI_NS,
        "alt": profile.alt_namespace,
    }
    root = etree.Element(_tag("osis"), nsmap=nsmap)
    root.set(f"{{{XSI_NS}}}schemaLocation", SCHEMA_LOCATION)
    osis_text = etree.SubElement(
        root,
        _tag("osisText"),
        osisIDWork=work_id,
        osisRefWork="bible",
    )
    osis_text.set(XML_LANG, language)
    _header(
        osis_text,
        profile,
        work_id,
        language,
        translation=translation,
    )
    book = etree.SubElement(
        osis_text,
        _tag("div"),
        type="book",
        osisID=profile.osis_book,
    )

    chapter_number: int | None = None
    chapter: etree._Element | None = None
    alt_attribute = f"{{{profile.alt_namespace}}}num"
    for record in records:
        if record.chapter != chapter_number:
            chapter_number = record.chapter
            chapter = etree.SubElement(
                book,
                _tag("chapter"),
                osisID=f"{profile.osis_book}.{record.chapter}",
            )
        assert chapter is not None
        attributes = {
            "osisID": f"{profile.osis_book}.{record.chapter}.{record.verse}",
            "n": record.source_verse or record.verse,
        }
        if record.alt_chapter is not None and record.alt_verse is not None:
            attributes[alt_attribute] = _alt_reference(
                profile,
                record.alt_chapter,
                record.alt_verse,
            )
        verse = etree.SubElement(chapter, _tag("verse"), **attributes)
        if record.empty:
            continue
        if translation:
            text = record.english
            markers = record.english_markers
            include_notes = True
        else:
            text = record.hebrew
            markers = record.hebrew_markers
            include_notes = commented
        if include_notes:
            marker_numbers = {marker.number for marker in markers}
            notes = {
                number: note
                for number, note in record.notes.items()
                if number in marker_numbers
            }
            _interleave_notes(verse, text, markers, notes)
        else:
            verse.text = text

    return etree.tostring(
        root,
        xml_declaration=True,
        encoding="UTF-8",
        pretty_print=True,
    )
