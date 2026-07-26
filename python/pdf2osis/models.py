from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Marker:
    offset: int
    number: str


@dataclass
class VerseRecord:
    chapter: int
    verse: str
    page: int
    source_verse: str | None = None
    alt_chapter: int | None = None
    alt_verse: str | None = None
    empty: bool = False
    hebrew: str = ""
    english: str = ""
    hebrew_markers: list[Marker] = field(default_factory=list)
    english_markers: list[Marker] = field(default_factory=list)
    notes: dict[str, str] = field(default_factory=dict)
    transcription_hebrew: str = ""
    interlinear_hebrew: str = ""
    transcription_markers: list[Marker] = field(default_factory=list)
    interlinear_markers: list[Marker] = field(default_factory=list)
    transcription_fragments: list[
        tuple[int, float, float, str, list[Marker]]
    ] = field(default_factory=list, repr=False)
    interlinear_fragments: list[
        tuple[int, float, float, str, list[Marker]]
    ] = field(default_factory=list, repr=False)
    extraction_disagreements: list[str] = field(default_factory=list)
    excluded_markers: list[str] = field(default_factory=list)

    @property
    def label(self) -> str:
        return f"{self.chapter}:{self.source_verse or self.verse}"
