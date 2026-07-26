from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re


@dataclass(frozen=True)
class BookProfile:
    key: str
    name: str
    osis_book: str
    scope: str
    stem: str
    default_pdf: str
    first_page: int
    last_page: int
    header_y1: float
    footer_y0: float
    expected_first: tuple[int, str]
    expected_last: tuple[int, str]
    expected_chapters: int
    expected_verses: int
    rtl_mode: str
    manuscript: str
    alt_namespace: str
    hebrew_work: str
    translation_work: str
    title: str
    translation_title: str
    description: str
    header_pattern: re.Pattern[str]
    parenthetical_pattern: re.Pattern[str]
    transcription_pattern: re.Pattern[str]
    translation_pattern: re.Pattern[str]
    translation_stops: tuple[str, ...]
    skip_prefixes: tuple[str, ...]
    empty_phrases: tuple[str, ...]
    expected_hebrew_prefix: str

    def output_names(self) -> dict[str, str]:
        return {
            "hebrew": f"{self.stem}_hebrew.osis",
            "hebrew_commented": f"{self.stem}_hebrew_commented.osis",
            "translation": f"{self.stem}_translation.osis",
        }

    def default_path(self, source_dir: Path) -> Path:
        return source_dir / self.default_pdf


REV = BookProfile(
    key="rev",
    name="Revelation",
    osis_book="Rev",
    scope="REV",
    stem="Cochin_MS_Oo.1.16.2_REV",
    default_pdf="MS_Cochin_Oo.1.16.2_REV_ProjectTruthMinistries.pdf",
    first_page=15,
    # The final 22:21 transcription is on PDF page 370 and its translation
    # continues on page 371. Page 372 starts the back matter.
    last_page=370,
    header_y1=48,
    footer_y0=728,
    expected_first=(1, "1"),
    expected_last=(22, "21"),
    expected_chapters=22,
    expected_verses=404,
    rtl_mode="span",
    manuscript="MS.Oo.1.16.2",
    alt_namespace="https://projecttruthministries.org/studies/cochin-revelation/",
    hebrew_work="MS.Oo.1.16.2_REV_Hebrew",
    translation_work="MS.Oo.1.16.2_REV_PTM",
    title="Revelation (Cochin MS Oo.1.16.2)",
    translation_title="Translation of Revelation (Cochin MS Oo.1.16.2)",
    description=(
        "The Cochin Hebrew New Testament manuscripts are significant "
        "18th-century Hebrew versions of the New Testament. This file "
        "encodes Revelation from Cambridge MS Oo.1.16.2."
    ),
    header_pattern=re.compile(
        r"^Revelation\s+(\d+):"
        r"(\d+[a-zA-Z]?(?:-\d+[a-zA-Z]?)?)"
        r"(?:\s*\(([^)]+)\))?\s*$",
        re.I,
    ),
    parenthetical_pattern=re.compile(r"Cochin\s+(\d+):(\d+[a-zA-Z]?)", re.I),
    transcription_pattern=re.compile(r"^Hebrew Transcription:?\s*$", re.I),
    translation_pattern=re.compile(r"^Translation:\s*", re.I),
    translation_stops=("The Scriptures:", "Aramaic:", "Greek:", "Does Not Exist"),
    skip_prefixes=(
        "The Scriptures:",
        "Aramaic:",
        "Greek:",
        "Copyright",
        "The Scroll of Mysteries",
        "Interlinear",
    ),
    empty_phrases=("does not exist", "changes the order", "omitted"),
    expected_hebrew_prefix="אלה הסודות",
)

JAS = BookProfile(
    key="jas",
    name="James",
    osis_book="Jas",
    scope="JAS",
    stem="Cochin_MS_Oo.1.32_JAS",
    default_pdf="MS_Cochin_Oo.1.32_JAS_ProjectTruthMinistries.pdf",
    first_page=10,
    last_page=68,
    header_y1=49,
    footer_y0=728,
    expected_first=(1, "1"),
    expected_last=(5, "20"),
    expected_chapters=5,
    expected_verses=107,
    rtl_mode="span",
    manuscript="MS.Oo.1.32",
    alt_namespace="https://projecttruthministries.org/studies/cochin-james/",
    hebrew_work="MS.Oo.1.32_JAS_Hebrew",
    translation_work="MS.Oo.1.32_JAS_PTM",
    title="James (Cochin MS Oo.1.32)",
    translation_title="Translation of James (Cochin MS Oo.1.32)",
    description=(
        "The Cochin Hebrew New Testament manuscripts are significant "
        "18th-century Hebrew versions of the New Testament. This file "
        "encodes James from Cambridge MS Oo.1.32."
    ),
    header_pattern=re.compile(
        r"^James\s+(\d+):"
        r"(\d+[a-zA-Z]?(?:-\d+[a-zA-Z]?)?)"
        r"(?:\s*\(([^)]+)\))?\s*$",
        re.I,
    ),
    parenthetical_pattern=re.compile(
        r"(?:Cochin|KJV)\s+(\d+):"
        r"(\d+[a-zA-Z]?(?:-\d+[a-zA-Z]?)?)",
        re.I,
    ),
    transcription_pattern=re.compile(
        r"^(?:Cochin Oo\.1\.32 )?(?:Hebrew )?Transcription:\s*",
        re.I,
    ),
    translation_pattern=re.compile(
        r"^(?:Cochin Oo\.1\.32 )?(?:English )?Translation:\s*",
        re.I,
    ),
    translation_stops=("KJV:",),
    skip_prefixes=("Image from Cochin", "KJV:"),
    empty_phrases=("does not exist", "omitted"),
    expected_hebrew_prefix="יעקב עבד",
)

BOOK_PROFILES = {"rev": REV, "jas": JAS}


def get_profile(key: str) -> BookProfile:
    try:
        return BOOK_PROFILES[key.lower()]
    except KeyError as exc:
        valid = ", ".join(sorted(BOOK_PROFILES))
        raise ValueError(f"Unknown book {key!r}; expected one of: {valid}") from exc
