"""Build the Hebrew lexicon index Milah ships with.

Two openly licensed sources are folded into one compact JSON file:

* Strong's Hebrew Dictionary (public domain, 1890), as published by the
  OpenScriptures project, for the entry text of each number.
* The Westminster Leningrad Codex with morphology (OpenScriptures morphhb,
  CC BY 4.0), for the inflected forms each number actually appears as.

The result is committed as ``app/data/hebrew_lexicon.json`` and compiled into
the binary as a Qt resource; the raw downloads are not kept. Run it again only
when the upstream data changes.

Usage::

    python python/tools/build_lexicon.py \\
        --strongs  <dir>/strongs-hebrew-dictionary.js \\
        --wlc      <dir>/wlc \\
        --out      app/data/hebrew_lexicon.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import unicodedata
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path

# Hebrew points and accents, matching hebrewMarks() in app/src/core/tokenize.cpp.
# 05BE (maqaf) is left out there because it is punctuation, and the punctuation
# pass below removes it anyway.
_MARKS = [(0x0591, 0x05BD), (0x05BF, 0x05C7)]
# Cantillation only, so a pointed form keeps its niqqud but loses the accents
# that manuscript text almost never carries.
_ACCENTS = [(0x0591, 0x05AF)]

_WHITESPACE = re.compile(r"\s+")
_OSIS_NS = "{http://www.bibletechnologies.net/2003/OSIS/namespace}"


def _in_ranges(code: int, ranges: list[tuple[int, int]]) -> bool:
    return any(low <= code <= high for low, high in ranges)


def comparison_key(text: str) -> str:
    """The consonantal skeleton, character for character what
    ``comparisonKey()`` in app/src/core/tokenize.cpp produces. The two must
    agree or nothing the app looks up will be found."""
    decomposed = unicodedata.normalize("NFD", text)
    kept = [
        ch
        for ch in decomposed
        if not _in_ranges(ord(ch), _MARKS)
        and not unicodedata.category(ch).startswith(("P", "S"))
    ]
    recomposed = unicodedata.normalize("NFC", "".join(kept))
    return _WHITESPACE.sub(" ", recomposed).strip().lower()


def pointed_key(text: str) -> str:
    """The form with its niqqud but without cantillation."""
    decomposed = unicodedata.normalize("NFD", text)
    kept = [
        ch
        for ch in decomposed
        if not _in_ranges(ord(ch), _ACCENTS)
        and not unicodedata.category(ch).startswith(("P", "S"))
    ]
    return unicodedata.normalize("NFC", "".join(kept)).strip()


def load_strongs(path: Path) -> dict[str, dict[str, str]]:
    """Reads the dictionary out of its JavaScript wrapper."""
    text = path.read_text(encoding="utf-8")
    start = text.index("{")
    end = text.rindex("}")
    raw = json.loads(text[start : end + 1])

    entries: dict[str, dict[str, str]] = {}
    for number, record in raw.items():
        gloss = (record.get("strongs_def") or record.get("kjv_def") or "").strip()
        # The dictionary brackets uncertain glosses and marks idioms; neither
        # reads well in a one-line interlinear cell.
        gloss = gloss.replace("{", "").replace("}", "").replace("[idiom]", "").strip()
        gloss = _WHITESPACE.sub(" ", gloss)
        if len(gloss) > 90:
            gloss = gloss[:87].rstrip(" ,;") + "…"
        entries[number] = {
            "l": record.get("lemma", ""),
            "x": record.get("xlit", ""),
            "g": gloss,
        }
    return entries


def strongs_numbers(lemma: str) -> list[str]:
    """Pulls the Strong's numbers out of a morphhb lemma attribute.

    ``c/1961`` is a conjunction prefixed to H1961; ``l/1481 a`` is a preposition
    on homonym *a* of H1481; ``1035+`` marks a compound name. Only the numeric
    parts name a dictionary entry, so the single-letter prefix codes are
    dropped.
    """
    numbers: list[str] = []
    for part in lemma.split("/"):
        digits = re.match(r"\d+", part.strip())
        if digits:
            numbers.append("H" + str(int(digits.group())))
    return numbers


def word_segments(word: str) -> list[str]:
    """morphhb separates prefixes from the stem with a slash."""
    return [segment for segment in word.split("/") if segment]


def index_wlc(directory: Path) -> tuple[dict[str, Counter], dict[str, Counter]]:
    forms: dict[str, Counter] = defaultdict(Counter)
    pointed: dict[str, Counter] = defaultdict(Counter)

    files = sorted(directory.glob("*.xml"))
    if not files:
        raise SystemExit(f"No WLC XML files under {directory}")

    for path in files:
        if path.name == "VerseMap.xml":
            continue
        tree = ET.parse(path)
        for element in tree.iter(f"{_OSIS_NS}w"):
            surface = "".join(element.itertext())
            lemma = element.get("lemma") or ""
            numbers = strongs_numbers(lemma)
            if not surface or not numbers:
                continue

            # The whole word as written, prefixes and all.
            whole = surface.replace("/", "")
            for number in numbers:
                key = comparison_key(whole)
                if key:
                    forms[key][number] += 1
                point = pointed_key(whole)
                if point:
                    pointed[point][number] += 1

            # And each morpheme on its own, so a stem written without the
            # prefix the Masoretic text happens to carry is still found.
            segments = word_segments(surface)
            if len(segments) == len(numbers) and len(segments) > 1:
                for segment, number in zip(segments, numbers):
                    key = comparison_key(segment)
                    if key:
                        forms[key][number] += 1
                    point = pointed_key(segment)
                    if point:
                        pointed[point][number] += 1

        print(f"  indexed {path.name}", file=sys.stderr)

    return forms, pointed


def ranked(counters: dict[str, Counter], limit: int) -> dict[str, list[str]]:
    """Most frequent reading first, so the interlinear cell shows the likeliest
    number and the rest stay available as candidates."""
    result: dict[str, list[str]] = {}
    for key in sorted(counters):
        counter = counters[key]
        ordered = [number for number, _ in counter.most_common(limit)]
        result[key] = ordered
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strongs", required=True, type=Path)
    parser.add_argument("--wlc", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument(
        "--max-candidates",
        type=int,
        default=4,
        help="how many Strong's numbers to keep per form",
    )
    arguments = parser.parse_args()

    print("Reading Strong's dictionary…", file=sys.stderr)
    entries = load_strongs(arguments.strongs)
    print(f"  {len(entries)} entries", file=sys.stderr)

    print("Indexing the Westminster Leningrad Codex…", file=sys.stderr)
    forms, pointed = index_wlc(arguments.wlc)
    print(
        f"  {len(forms)} consonantal forms, {len(pointed)} pointed forms",
        file=sys.stderr,
    )

    # Only entries some form actually points at are worth shipping.
    referenced = {number for numbers in forms.values() for number in numbers}
    kept = {number: entries[number] for number in sorted(referenced) if number in entries}
    print(f"  {len(kept)} entries reachable from a form", file=sys.stderr)

    document = {
        "version": 1,
        "about": (
            "Strong's Hebrew Dictionary (public domain) and the Westminster "
            "Leningrad Codex morphology from OpenScriptures morphhb (CC BY 4.0). "
            "See app/data/README.md."
        ),
        "entries": kept,
        "forms": ranked(forms, arguments.max_candidates),
        "pointed": ranked(pointed, arguments.max_candidates),
    }

    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    # Sorted and compact, so the committed file diffs sanely between runs.
    arguments.out.write_text(
        json.dumps(document, ensure_ascii=False, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    print(
        f"Wrote {arguments.out} "
        f"({arguments.out.stat().st_size / 1_048_576:.1f} MB)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
