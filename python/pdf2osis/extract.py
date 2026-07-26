from __future__ import annotations

from collections import Counter, defaultdict
from difflib import SequenceMatcher
import re
from pathlib import Path
from typing import Any, Iterable
import unicodedata

import fitz

from .models import Marker, VerseRecord
from .profiles import BookProfile

HEBREW_RE = re.compile(r"[\u0590-\u05ff\ufb1d-\ufb4e]")
HEBREW_TOKEN_RE = re.compile(r"[\u0590-\u05ff\ufb1d-\ufb4e]+")
ASCII_RE = re.compile(r"[A-Za-z]")
FOOTNOTE_START_RE = re.compile(r"^\s*\d+\s+[A-Z\"'“‘(]")
TRANSLATION_LABEL_RE = re.compile(
    r"(?:English\s+)?Translation(?:\s*#\d+)?:\s*",
    re.I,
)
XML_SPACE_RE = re.compile(r"\s+")
MARK_RATIO = 0.78


class ExtractionError(RuntimeError):
    pass


def has_hebrew(text: str) -> bool:
    return bool(HEBREW_RE.search(text))


def hebrew_char_count(text: str) -> int:
    return len(HEBREW_RE.findall(text))


def _spans(block: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        span
        for line in block.get("lines", [])
        for span in line.get("spans", [])
    ]


def _span_text(span: dict[str, Any]) -> str:
    if "text" in span:
        return span["text"]
    return "".join(char["c"] for char in span.get("chars", []))


def block_texts(block: dict[str, Any]) -> tuple[str, str]:
    spans = _spans(block)
    if not spans:
        return "", ""
    full = "".join(_span_text(span) for span in spans).strip()
    sizes = [span["size"] for span in spans if _span_text(span).strip()]
    if not sizes:
        return full, full
    dominant = max(sizes)
    main = "".join(
        _span_text(span)
        for span in spans
        if span["size"] >= dominant * 0.85
    ).strip()
    return full, main


def _reverse_hebrew_words(text: str) -> str:
    return HEBREW_TOKEN_RE.sub(lambda match: match.group(0)[::-1], text)


def _rtl_span_text(span: dict[str, Any]) -> str:
    chars = span.get("chars")
    if not chars:
        return _span_text(span)[::-1]
    runs: list[list[dict[str, Any]]] = []
    previous_x: float | None = None
    for char in chars:
        x0 = char["bbox"][0]
        if previous_x is not None and x0 < previous_x - 15:
            runs.append([])
        if not runs:
            runs.append([])
        runs[-1].append(char)
        previous_x = x0
    ordered = sorted(
        runs,
        key=lambda run: max(char["bbox"][0] for char in run),
        reverse=True,
    )
    text = " ".join(
        "".join(char["c"] for char in run)[::-1].strip()
        for run in ordered
        if "".join(char["c"] for char in run).strip()
    )
    return re.sub(r"\s+([:׃])", r"\1", text)


def _is_marker(span: dict[str, Any], max_size: float) -> bool:
    text = _span_text(span).strip()
    return (
        text.isdigit()
        and (
            span["size"] < max_size * MARK_RATIO
            or bool(span.get("flags", 0) & 1)
        )
    )


def _normalise_text_and_markers(
    text: str,
    markers: Iterable[Marker],
) -> tuple[str, list[Marker]]:
    events: dict[int, list[str]] = defaultdict(list)
    for marker in markers:
        events[max(0, min(marker.offset, len(text)))].append(marker.number)

    output: list[str] = []
    adjusted: list[Marker] = []
    pending_space = False
    for index in range(len(text) + 1):
        if index in events:
            if pending_space and output:
                output.append(" ")
                pending_space = False
            adjusted.extend(Marker(len(output), number) for number in events[index])
        if index == len(text):
            break
        char = text[index]
        if char.isspace():
            pending_space = True
        else:
            if pending_space and output:
                output.append(" ")
            pending_space = False
            output.append(char)

    cleaned = "".join(output).strip()
    leading = len("".join(output)) - len("".join(output).lstrip())
    if leading:
        adjusted = [
            Marker(max(0, marker.offset - leading), marker.number)
            for marker in adjusted
        ]
    return cleaned, adjusted


def hebrew_text_and_markers(
    block: dict[str, Any],
    profile: BookProfile,
    *,
    rtl_mode: str | None = None,
) -> tuple[str, list[Marker]]:
    result = ""
    markers: list[Marker] = []
    content_lines = [
        line
        for line in block.get("lines", [])
        if any(has_hebrew(_span_text(span)) for span in line.get("spans", []))
    ]
    for line_index, line in enumerate(content_lines):
        spans = line.get("spans", [])
        if not spans:
            continue
        max_size = max(
            (span["size"] for span in spans if _span_text(span).strip()),
            default=0,
        )
        ordered = sorted(spans, key=lambda span: span["bbox"][0], reverse=True)
        line_text = ""
        line_markers: list[Marker] = []
        after_marker = False
        for span in ordered:
            raw = _span_text(span)
            if _is_marker(span, max_size):
                line_markers.append(Marker(len(line_text), raw.strip()))
                after_marker = True
                continue
            # A translation/comparison span can contain one Hebrew name.
            # Reversing that entire mixed span was the source of the Latin
            # contamination in the first implementation.
            if ASCII_RE.search(raw):
                continue
            mode = rtl_mode or profile.rtl_mode
            if mode == "word":
                transformed = _reverse_hebrew_words(raw)
            elif mode == "span":
                transformed = _rtl_span_text(span)
            else:
                raise ExtractionError(f"Unknown RTL mode: {mode}")
            if after_marker and line_text and transformed:
                line_text += " "
            after_marker = False
            line_text += transformed

        if line_index and result:
            result += " "
        shift = len(result)
        result += line_text
        markers.extend(
            Marker(marker.offset + shift, marker.number)
            for marker in line_markers
        )
    return _normalise_text_and_markers(result, markers)


def english_text_and_markers(
    block: dict[str, Any],
) -> tuple[str, list[Marker]]:
    spans = _spans(block)
    sizes = [span["size"] for span in spans if _span_text(span).strip()]
    if not sizes:
        return "", []
    max_size = max(sizes)
    text = ""
    markers: list[Marker] = []
    for span in spans:
        if _is_marker(span, max_size):
            markers.append(Marker(len(text), _span_text(span).strip()))
        else:
            text += _span_text(span)
    return _normalise_text_and_markers(text, markers)


def marker_numbers(block: dict[str, Any]) -> list[str]:
    spans = _spans(block)
    sizes = [span["size"] for span in spans if _span_text(span).strip()]
    if not sizes:
        return []
    max_size = max(sizes)
    return [
        _span_text(span).strip()
        for span in spans
        if _is_marker(span, max_size)
    ]


def _strip_prefix(
    text: str,
    markers: list[Marker],
    pattern: re.Pattern[str],
) -> tuple[str, list[Marker]]:
    match = pattern.match(text)
    if not match:
        return text, markers
    cut = match.end()
    return (
        text[cut:].lstrip(),
        [
            Marker(max(0, marker.offset - cut), marker.number)
            for marker in markers
            if marker.offset >= cut
        ],
    )


def _translation_from_block(
    block: dict[str, Any],
    profile: BookProfile,
) -> tuple[str, list[Marker]] | None:
    text, markers = english_text_and_markers(block)
    match = TRANSLATION_LABEL_RE.search(text)
    if not match:
        return None
    cut = match.end()
    text = text[cut:].lstrip()
    markers = [
        Marker(max(0, marker.offset - cut), marker.number)
        for marker in markers
        if marker.offset >= cut
    ]
    return _truncate(text, markers, profile.translation_stops)


def _truncate(
    text: str,
    markers: list[Marker],
    stops: Iterable[str],
) -> tuple[str, list[Marker]]:
    indexes = [text.find(stop) for stop in stops if text.find(stop) >= 0]
    if not indexes:
        return text.strip(), markers
    cut = min(indexes)
    return (
        text[:cut].strip(),
        [marker for marker in markers if marker.offset < cut],
    )


def is_footnote_block(text: str, profile: BookProfile) -> bool:
    if re.match(r"^\d+\s+of\s+\d+$", text.strip(), re.I):
        return False
    if FOOTNOTE_START_RE.match(text):
        pass
    else:
        return False
    ratio = hebrew_char_count(text) / max(1, len(text))
    return ratio < (0.15 if profile.key == "rev" else 0.05)


def looks_like_footnote_block(
    block: dict[str, Any],
    text: str,
    profile: BookProfile,
) -> bool:
    if is_footnote_block(text, profile):
        return True
    # Wrapped footnote blocks use the same small type as definitions but may
    # not begin with a number. Interlinear glosses also contain small
    # superscript numbers, so only apply this fallback in the page's footnote
    # region.
    if block["bbox"][1] < 480:
        return False
    spans = _spans(block)
    sizes = [span["size"] for span in spans if _span_text(span).strip()]
    if not sizes or max(sizes) > 9.5:
        return False
    max_size = max(sizes)
    return any(_is_marker(span, max_size) for span in spans)


def extract_footnote_definitions(
    block: dict[str, Any],
) -> tuple[str, dict[str, str]]:
    spans = _spans(block)
    sizes = [span["size"] for span in spans if _span_text(span).strip()]
    if not sizes:
        return "", {}
    max_size = max(sizes)
    notes: dict[str, str] = {}
    current: str | None = None
    chunks: list[str] = []
    leading: list[str] = []

    def store() -> None:
        if current is not None:
            text = XML_SPACE_RE.sub(" ", "".join(chunks)).strip()
            if text:
                notes[current] = text

    for span in spans:
        stripped = _span_text(span).strip()
        if stripped.isdigit() and span["size"] < max_size * MARK_RATIO:
            store()
            current = stripped
            chunks = []
        elif current is not None:
            chunks.append(_span_text(span))
        else:
            leading.append(_span_text(span))
    store()
    return XML_SPACE_RE.sub(" ", "".join(leading)).strip(), notes


def parse_header(text: str, profile: BookProfile, page: int) -> VerseRecord | None:
    match = profile.header_pattern.match(text.strip())
    if not match:
        return None
    chapter = int(match.group(1))
    source_verse = match.group(2).lower()
    verse = source_verse.split("-", 1)[0]
    parenthetical = match.group(3) or ""
    alt_chapter: int | None = None
    alt_verse: str | None = None
    alt_match = profile.parenthetical_pattern.search(parenthetical)
    if alt_match:
        alt_chapter = int(alt_match.group(1))
        alt_verse = alt_match.group(2).lower()
    empty = any(phrase in parenthetical.lower() for phrase in profile.empty_phrases)
    return VerseRecord(
        chapter=chapter,
        verse=verse,
        page=page,
        source_verse=source_verse,
        alt_chapter=alt_chapter,
        alt_verse=alt_verse,
        empty=empty,
    )


def _append_text(
    record: VerseRecord,
    field: str,
    marker_field: str,
    text: str,
    markers: list[Marker],
) -> None:
    current = getattr(record, field)
    separator = " " if current and text else ""
    shift = len(current) + len(separator)
    setattr(record, field, current + separator + text)
    target: list[Marker] = getattr(record, marker_field)
    target.extend(
        Marker(marker.offset + shift, marker.number)
        for marker in markers
    )


def _token_key(token: str) -> str:
    return "".join(
        char
        for char in token
        if has_hebrew(char) and unicodedata.category(char) != "Mn"
    )


def _token_spans(text: str) -> list[tuple[int, int, str]]:
    return [
        (match.start(), match.end(), match.group(0))
        for match in re.finditer(r"\S+", text)
    ]


def _nearest_token_index(
    spans: list[tuple[int, int, str]],
    offset: int,
) -> int | None:
    if not spans:
        return None
    return min(
        range(len(spans)),
        key=lambda index: min(
            abs(offset - spans[index][0]),
            abs(offset - spans[index][1]),
        ),
    )


def _remap_markers(
    markers: Iterable[Marker],
    source_text: str,
    target_text: str,
    token_map: dict[int, int],
    *,
    zone: str,
    record: VerseRecord,
) -> list[Marker]:
    source_spans = _token_spans(source_text)
    target_spans = _token_spans(target_text)
    result: list[Marker] = []
    for marker in markers:
        source_index = _nearest_token_index(source_spans, marker.offset)
        target_index = token_map.get(source_index) if source_index is not None else None
        if target_index is None or target_index >= len(target_spans):
            record.excluded_markers.append(
                f"{zone} marker {marker.number} could not be aligned"
            )
            continue
        source_start, source_end, _ = source_spans[source_index]
        target_start, target_end, _ = target_spans[target_index]
        use_end = marker.offset >= (source_start + source_end) // 2
        result.append(Marker(target_end if use_end else target_start, marker.number))
    return result


def _consolidate_fragments(
    record: VerseRecord,
    fragments: list[tuple[int, float, float, str, list[Marker]]],
    text_field: str,
    marker_field: str,
) -> None:
    if not fragments:
        return
    rows: list[list[tuple[int, float, float, str, list[Marker]]]] = []
    for fragment in sorted(
        fragments,
        key=lambda item: (item[0], item[1], item[2]),
    ):
        if (
            not rows
            or rows[-1][0][0] != fragment[0]
            or abs(rows[-1][0][1] - fragment[1]) > 3
        ):
            rows.append([fragment])
        else:
            rows[-1].append(fragment)

    for row in rows:
        for _, _, _, text, markers in sorted(
            row,
            key=lambda item: item[2],
            reverse=True,
        ):
            _append_text(
                record,
                text_field,
                marker_field,
                text,
                markers,
            )


def _reconcile_hebrew(record: VerseRecord) -> None:
    _consolidate_fragments(
        record,
        record.transcription_fragments,
        "transcription_hebrew",
        "transcription_markers",
    )
    _consolidate_fragments(
        record,
        record.interlinear_fragments,
        "interlinear_hebrew",
        "interlinear_markers",
    )
    transcription = record.transcription_hebrew
    interlinear = record.interlinear_hebrew
    if not transcription:
        record.hebrew = interlinear
        record.hebrew_markers = list(record.interlinear_markers)
        return
    if not interlinear:
        record.hebrew = transcription
        record.hebrew_markers = list(record.transcription_markers)
        return

    transcription_tokens = transcription.split()
    interlinear_tokens = interlinear.split()
    transcription_keys = [_token_key(token) for token in transcription_tokens]
    interlinear_keys = [_token_key(token) for token in interlinear_tokens]

    # A few PDF transcription spans expose their entire visual line in the
    # wrong direction, which is detectable when punctuation lands at the
    # beginning. The interlinear row contains the identical word multiset in
    # logical order and is safe to use in that narrowly defined case.
    use_interlinear_order = (
        transcription[:1] in {":", "׃"}
        and interlinear[:1] not in {":", "׃"}
        and Counter(transcription_keys) == Counter(interlinear_keys)
    )
    if use_interlinear_order:
        final = interlinear
        interlinear_to_final = {
            index: index for index in range(len(interlinear_tokens))
        }
        available: dict[str, list[int]] = defaultdict(list)
        for index, key in enumerate(interlinear_keys):
            available[key].append(index)
        transcription_to_final: dict[int, int] = {}
        for index, key in enumerate(transcription_keys):
            if available[key]:
                transcription_to_final[index] = available[key].pop(0)
        record.extraction_disagreements.append(
            "used interlinear order to repair leading RTL punctuation"
        )
        markers = _remap_markers(
            record.transcription_markers,
            transcription,
            final,
            transcription_to_final,
            zone="transcription",
            record=record,
        )
        markers.extend(
            _remap_markers(
                record.interlinear_markers,
                interlinear,
                final,
                interlinear_to_final,
                zone="interlinear",
                record=record,
            )
        )
        record.hebrew = final
        record.hebrew_markers = sorted(
            {
                (marker.offset, marker.number): marker
                for marker in markers
            }.values(),
            key=lambda marker: (marker.offset, int(marker.number)),
        )
        return

    matcher = SequenceMatcher(
        None,
        transcription_keys,
        interlinear_keys,
        autojunk=False,
    )
    final_tokens = list(transcription_tokens)
    interlinear_to_final: dict[int, int] = {}
    for tag, a0, a1, b0, b1 in matcher.get_opcodes():
        if tag == "equal":
            for shift in range(a1 - a0):
                interlinear_to_final[b0 + shift] = a0 + shift
        elif tag == "replace" and (a1 - a0) == (b1 - b0):
            for shift in range(a1 - a0):
                source_index = a0 + shift
                interlinear_index = b0 + shift
                source_key = transcription_keys[source_index]
                interlinear_key = interlinear_keys[interlinear_index]
                similarity = SequenceMatcher(
                    None,
                    source_key,
                    interlinear_key,
                    autojunk=False,
                ).ratio()
                safe_substitution = (
                    len(source_key) == len(interlinear_key) == 1
                    or (
                        min(len(source_key), len(interlinear_key)) >= 2
                        and similarity >= 0.65
                    )
                )
                if safe_substitution:
                    final_tokens[source_index] = interlinear_tokens[interlinear_index]
                    interlinear_to_final[interlinear_index] = source_index
                else:
                    record.extraction_disagreements.append(
                        f"rejected unrelated token substitution "
                        f"{transcription_tokens[source_index]!r} -> "
                        f"{interlinear_tokens[interlinear_index]!r}"
                    )
        else:
            record.extraction_disagreements.append(
                f"unaligned tokens transcription[{a0}:{a1}] "
                f"interlinear[{b0}:{b1}]"
            )

    final = " ".join(final_tokens)
    identity_map = {index: index for index in range(len(transcription_tokens))}
    markers = _remap_markers(
        record.transcription_markers,
        transcription,
        final,
        identity_map,
        zone="transcription",
        record=record,
    )
    markers.extend(
        _remap_markers(
            record.interlinear_markers,
            interlinear,
            final,
            interlinear_to_final,
            zone="interlinear",
            record=record,
        )
    )
    record.hebrew = final
    record.hebrew_markers = sorted(
        {
            (marker.offset, marker.number): marker
            for marker in markers
        }.values(),
        key=lambda marker: (marker.offset, int(marker.number)),
    )


def _alpha_suffix(index: int) -> str:
    result = ""
    value = index + 1
    while value:
        value, remainder = divmod(value - 1, 26)
        result = chr(ord("a") + remainder) + result
    return result


def suffix_repeated_verses(records: list[VerseRecord]) -> None:
    positions: dict[tuple[int, str], list[int]] = defaultdict(list)
    for index, record in enumerate(records):
        if record.verse.isdigit():
            positions[(record.chapter, record.verse)].append(index)
    for indexes in positions.values():
        if len(indexes) < 2:
            continue
        for occurrence, index in enumerate(indexes):
            record = records[index]
            suffix = _alpha_suffix(occurrence)
            record.verse += suffix
            record.source_verse = record.verse
            if record.alt_verse and record.alt_verse.isdigit():
                record.alt_verse += suffix


def extract_pdf(
    pdf_path: Path,
    profile: BookProfile,
) -> tuple[list[VerseRecord], dict[str, str], list[str]]:
    document = fitz.open(str(pdf_path))
    if len(document) <= profile.last_page:
        raise ExtractionError(
            f"{pdf_path.name} has {len(document)} pages; "
            f"{profile.name} requires page {profile.last_page + 1}"
        )

    records: list[VerseRecord] = []
    definitions: dict[str, str] = {}
    anomalies: list[str] = []
    current: VerseRecord | None = None
    state = "SEEK"
    last_definition_number: str | None = None

    for page_index in range(profile.first_page, profile.last_page + 1):
        page = document[page_index]
        blocks = sorted(
            page.get_text("rawdict")["blocks"],
            key=lambda block: (block["bbox"][1], block["bbox"][0]),
        )
        for block in blocks:
            if block.get("type") != 0:
                continue
            x0, y0, x1, y1 = block["bbox"]
            if y1 < profile.header_y1 or y0 > profile.footer_y0:
                continue
            full_text, main_text = block_texts(block)
            if not full_text:
                continue

            if looks_like_footnote_block(block, full_text, profile):
                leading, block_definitions = extract_footnote_definitions(block)
                if leading and last_definition_number in definitions:
                    definitions[last_definition_number] = (
                        definitions[last_definition_number] + " " + leading
                    ).strip()
                for number, note in block_definitions.items():
                    previous = definitions.get(number)
                    if previous and previous != note:
                        anomalies.append(
                            f"footnote {number} has multiple definitions; "
                            "kept the first"
                        )
                    else:
                        definitions[number] = note
                    last_definition_number = number
                continue

            parsed = parse_header(main_text, profile, page_index + 1)
            if parsed:
                if current is not None:
                    records.append(current)
                current = parsed
                state = "TRANSCRIPTION"
                continue
            if current is None:
                continue

            lower = full_text.lower()
            if (
                "-" not in (current.source_verse or current.verse)
                and any(phrase in lower for phrase in profile.empty_phrases)
            ):
                if state == "TRANSCRIPTION":
                    current.empty = True

            if full_text.strip() == "Interlinear Chart":
                state = "AFTER_INTERLINEAR"
                continue
            if profile.transcription_pattern.match(full_text):
                # Revelation puts this label after the transcription; James
                # puts it before. In either layout the label remains part of
                # the transcription zone and contributes no text itself.
                if (
                    has_hebrew(full_text)
                    and not current.empty
                ):
                    text, markers = hebrew_text_and_markers(block, profile)
                    if text:
                        current.transcription_fragments.append(
                            (page_index, y0, x0, text, markers)
                        )
                translation = _translation_from_block(block, profile)
                if translation is not None:
                    text, markers = translation
                    if text and not current.empty:
                        _append_text(
                            current,
                            "english",
                            "english_markers",
                            text,
                            markers,
                        )
                    state = (
                        "INTERLINEAR"
                        if "aramaic:" in lower
                        else "AFTER_TRANSLATION"
                    )
                continue
            if TRANSLATION_LABEL_RE.search(full_text):
                translation = _translation_from_block(block, profile)
                if translation is None:
                    continue
                text, markers = translation
                if text and not current.empty:
                    _append_text(
                        current,
                        "english",
                        "english_markers",
                        text,
                        markers,
                    )
                state = (
                    "INTERLINEAR"
                    if "aramaic:" in lower
                    else "AFTER_TRANSLATION"
                )
                continue

            if lower.startswith("aramaic:"):
                state = "INTERLINEAR"
                continue

            if (
                state == "TRANSCRIPTION"
                and has_hebrew(full_text)
                and not ASCII_RE.search(full_text)
            ):
                text, markers = hebrew_text_and_markers(block, profile)
                if text and not current.empty:
                    current.transcription_fragments.append(
                        (page_index, y0, x0, text, markers)
                    )
                continue

            if (
                state == "INTERLINEAR"
                and has_hebrew(full_text)
                and not ASCII_RE.search(full_text)
            ):
                text, markers = hebrew_text_and_markers(
                    block,
                    profile,
                    rtl_mode="word",
                )
                if text and not current.empty:
                    current.interlinear_fragments.append(
                        (page_index, y0, x0, text, markers)
                    )

    if current is not None:
        records.append(current)
    document.close()

    suffix_repeated_verses(records)
    for record in records:
        if not record.empty:
            _reconcile_hebrew(record)
    if not records:
        raise ExtractionError(f"No {profile.name} verses found in {pdf_path}")
    first = (records[0].chapter, records[0].verse)
    last = (records[-1].chapter, records[-1].verse)
    if first != profile.expected_first or last != profile.expected_last:
        raise ExtractionError(
            f"Unexpected verse range {first!r}–{last!r}; expected "
            f"{profile.expected_first!r}–{profile.expected_last!r}"
        )

    all_markers = {
        marker.number
        for record in records
        for marker in record.hebrew_markers + record.english_markers
    }
    # Small digits used as grammatical indices in the interlinear tables have
    # the same typography as superscripts. Source footnote numbering starts
    # at the lowest definition found in the content range, so lower values
    # are not footnote markers.
    lowest_definition = min(
        (int(number) for number in definitions),
        default=0,
    )
    all_markers = {
        number
        for number in all_markers
        if int(number) >= lowest_definition
    }
    for record in records:
        retained_hebrew: list[Marker] = []
        retained_english: list[Marker] = []
        for zone, source, target in (
            ("Hebrew", record.hebrew_markers, retained_hebrew),
            ("translation", record.english_markers, retained_english),
        ):
            for marker in source:
                if int(marker.number) < lowest_definition:
                    record.excluded_markers.append(
                        f"{zone} marker {marker.number} is below footnote range"
                    )
                elif marker.number not in definitions:
                    record.excluded_markers.append(
                        f"{zone} marker {marker.number} has no definition"
                    )
                else:
                    target.append(marker)
        record.hebrew_markers = retained_hebrew
        record.english_markers = retained_english
        used = {
            marker.number
            for marker in record.hebrew_markers + record.english_markers
        }
        record.notes = {
            number: definitions[number]
            for number in used
            if number in definitions
        }
    missing = sorted(
        all_markers - definitions.keys(),
        key=lambda value: int(value),
    )
    unused = sorted(
        definitions.keys() - all_markers,
        key=lambda value: int(value),
    )
    if missing:
        anomalies.append("markers without definitions: " + ", ".join(missing))
    if unused:
        anomalies.append("definitions without verse markers: " + ", ".join(unused))
    return records, definitions, anomalies
