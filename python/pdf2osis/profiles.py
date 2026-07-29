from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


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
    manuscript: str
    alt_namespace: str
    hebrew_work: str
    translation_work: str
    title: str
    translation_title: str
    description: str
    expected_hebrew_prefix: str

    # Which module extracts this source: pdf2osis.cochin, .sloane or .ebr530.
    extractor: str = "cochin"
    # A pointed manuscript needs its glyphs resolved against the embedded font's
    # own cmap; see pdf2osis.glyphs.
    pointed: bool = False
    # Provenance for the OSIS header, as ``(type, text)`` pairs. OSIS restricts
    # description/@type to `usfm` or an `x-` extension, so these are `x-…`
    # values. Every claim here should be traceable to one of `sources`.
    descriptions: tuple[tuple[str, str], ...] = ()
    # Where the provenance came from, emitted as <source> elements so a reader
    # can check it rather than take our word for it.
    sources: tuple[str, ...] = ()
    coverage: str = ""
    relation: str = ""
    # Page geometry, which differs per source; see pdf2osis.layout.
    footer_top: float = 660.0
    column_split: float = 305.0
    # The book's own name, where it differs from the OSIS abbreviation.
    osis_book_name: str = ""
    # Which Cochin extractor handles this edition; see pdf2osis.cochin.
    cochin_book: str = ""
    # Calendar for <date>. A century range is not an ISO date.
    date_calendar: str = "ISO"
    # Header credits. The defaults describe the Project Truth Ministries Cochin
    # editions; other sources override them.
    publisher: str = "Project Truth Ministries"
    original_date: str = "ca. 1730"
    edition_date: str = "2024"
    translator: str = "Project Truth Ministries"
    contributor: str = "Janice F. Baca"
    contributor_file_as: str = "Baca, Janice F."
    rights: str = "© copyright 2024 Janice F. Baca"

    def output_names(self) -> dict[str, str]:
        names = {
            "hebrew": f"{self.stem}_hebrew.osis",
            "hebrew_commented": f"{self.stem}_hebrew_commented.osis",
            "translation": f"{self.stem}_translation.osis",
        }
        if self.pointed:
            names["hebrew_consonantal"] = f"{self.stem}_hebrew_consonantal.osis"
        return names

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
    header_y1=40,
    footer_y0=728,
    expected_first=(1, "1"),
    expected_last=(22, "21"),
    expected_chapters=22,
    # 405. Revelation 2:26 and 20:12 are in the source but their headers are not
    # set at the usual size, so the old extractor's size-keyed search dropped
    # them, giving 404. Against that, the second "Revelation 2:21" header is not
    # a verse at all: it is a signpost carrying the notice that the manuscript
    # transposes 2:21 and 2:22, and counting it gave 406.
    expected_verses=405,
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
    expected_hebrew_prefix="אלה הסודות",
    cochin_book="rev",
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
    expected_hebrew_prefix="יעקב עבד",
    cochin_book="jas",
)

SLOANE_REV = BookProfile(
    key="sloane_rev",
    name="Revelation",
    osis_book="Rev",
    scope="REV",
    # The edition prints "MS Sloane 273", but the British Library catalogue
    # records the Hebrew Revelation as Sloane MS 237 — four paper folios in
    # square Hebrew script, which is exactly the 1r–4v this text occupies.
    stem="REV_Sloane237",
    default_pdf=(
        "A-Hebrew-Manuscript-of-the-Book-of-Revelation-"
        "British-Library-Sloane-273.pdf"
    ),
    first_page=1,
    last_page=11,
    header_y1=60,
    footer_y0=660,
    expected_first=(1, "1"),
    expected_last=(2, "13"),
    expected_chapters=2,
    expected_verses=33,
    manuscript="British Library, Sloane MS 237",
    alt_namespace="https://www.nehemiaswall.com/sloane-273",
    hebrew_work="Sloane237_REV_Hebrew",
    translation_work="Sloane237_REV_Gordon",
    title="Revelation (British Library, Sloane MS 237)",
    translation_title=(
        "English Translation of Revelation (British Library, Sloane MS 237)"
    ),
    description=(
        "Hebrew transcription and English translation of the Revelation "
        "passages in British Library, Sloane MS 237, a fully pointed Hebrew "
        "manuscript, transcribed and translated by Nehemia Gordon."
    ),
    descriptions=(
        (
            "x-contents",
            "A Hebrew translation of the Revelation of John occupying four "
            "paper folios, 1r–4v. This edition covers Revelation 1:1–2:13.",
        ),
        (
            "x-script",
            "Square Hebrew script, fully vocalised, ruled and written ten "
            "lines to the page.",
        ),
        (
            "x-provenance",
            "From the collection of Sir Hans Sloane (1660–1753), baronet, "
            "physician and collector. Part of the Sloane bequest, "
            "incorporated into the newly founded British Museum in 1753 and "
            "held by the British Library since 1973.",
        ),
        (
            "x-editorial",
            "The editor's notes record pointing absent from the manuscript — "
            "a missing sheva, shin- and sin-dots, and a cholam — together "
            "with one extraneous kamatz that the manuscript does carry. The "
            "manuscript numbers its own verses with Hebrew letters, which "
            "disagree with the printed numbering at Revelation 1:9, 1:15, "
            "1:16, 1:17 and 2:8.",
        ),
    ),
    sources=(
        "Shelfmark, extent, script, date and provenance from the British "
        "Library Archives and Manuscripts catalogue record for Sloane MS 237.",
        "Text, translation and annotation from Nehemia Gordon, 'A Hebrew "
        "Manuscript of the Book of Revelation' (2017), nehemiaswall.com.",
    ),
    coverage="Revelation 1:1–2:13",
    date_calendar="Gregorian",
    expected_hebrew_prefix="חֲזוֹן יְהוֹשֻׁעַ",
    extractor="sloane",
    pointed=True,
    publisher="Nehemia Gordon",
    original_date="between 1500 and 1699",
    edition_date="2017",
    translator="Nehemia Gordon",
    contributor="Nehemia Gordon",
    contributor_file_as="Gordon, Nehemia",
    rights="© 2017 by Nehemia Gordon",
)


EBR530_LUKE = BookProfile(
    key="ebr530_luke",
    name="Luke",
    osis_book="Luke",
    osis_book_name="Luke",
    scope="LUK",
    stem="LUK_Ebr530",
    default_pdf=(
        "Hebrew-Gospels-of-Luke-and-John-from-the-Vatican_"
        "Biblioteca Apostolica ebr. 530.pdf"
    ),
    first_page=1,
    last_page=13,
    header_y1=60,
    footer_y0=701,
    expected_first=(1, "1"),
    expected_last=(1, '35'),
    expected_chapters=1,
    expected_verses=35,
    manuscript=(
        "Biblioteca Apostolica Vaticana, Vat. ebr. 530, part 1, fragment 11, "
        "folios 1r–2v"
    ),
    alt_namespace="https://digi.vatlib.it/view/MSS_Vat.ebr.530.pt.1",
    hebrew_work="Ebr530_LUK_Hebrew",
    translation_work="Ebr530_LUK_Gordon",
    title="Luke (Vatican, Vat. ebr. 530)",
    translation_title="English Translation of Luke (Vatican, Vat. ebr. 530)",
    description=(
        "Hebrew transcription and English translation of Luke from "
        "Biblioteca Apostolica Vaticana, Vat. ebr. 530, part 1, fragment 11, "
        "folios 1r–2v, transcribed, translated and annotated by Nehemia "
        "Gordon."
    ),
    descriptions=(
        (
            "x-contents",
            "Part 1, fragment 11 of a composite volume of fragments. Folios "
            "1r–2v carry Luke 1:1–35 and John 1:1–13 in Hebrew, each "
            "with a heading and a chapter heading.",
        ),
        (
            "x-script",
            "Fully pointed Hebrew. The pointing is irregular by Tiberian "
            "standards: dagesh is often omitted where expected, and kamatz "
            "and patach — like tsere and segol — are used "
            "interchangeably, which the editor relates to the Sephardic "
            "pronunciation and to Palestinian pointing (Nikud Eretz-Yisraeli).",
        ),
        (
            "x-provenance",
            "Held by the Biblioteca Apostolica Vaticana and published through "
            "its digitisation programme on DigiVatLib.",
        ),
        (
            "x-editorial",
            "The editor notes that a scribe scratched out Adonai and wrote "
            "Yehovah in its place, and records variant forms attested in "
            "Mishnaic manuscripts and in the Bar Ilan Responsa Database.",
        ),
    ),
    sources=(
        "Shelfmark, extent and repository from the manuscript's record in "
        "DigiVatLib, https://digi.vatlib.it/view/MSS_Vat.ebr.530.pt.1. "
        "Codicological detail is catalogued by Umberto Cassuto, 'Codices "
        "Vaticani Hebraici' — cited here, not consulted.",
        "Text, translation and annotation from Nehemia Gordon, 'Hebrew "
        "Gospels of Luke and John found in the Vatican Library', version 3.9 "
        "(2018), nehemiaswall.com.",
    ),
    coverage="Luke 1:1–35",
    date_calendar="Gregorian",
    original_date="undated",
    edition_date="2018",
    publisher="Nehemia Gordon",
    translator="Nehemia Gordon",
    contributor="Nehemia Gordon",
    contributor_file_as="Gordon, Nehemia",
    rights="© 2018 Nehemia Gordon. All rights reserved.",
    expected_hebrew_prefix="בִהְיוֹת",
    extractor="ebr530",
    pointed=True,
    footer_top=701.0,
    column_split=306.0,
)

EBR530_JOHN = BookProfile(
    key="ebr530_john",
    name="John",
    osis_book="John",
    osis_book_name="John",
    scope="JOH",
    stem="JOH_Ebr530",
    default_pdf=(
        "Hebrew-Gospels-of-Luke-and-John-from-the-Vatican_"
        "Biblioteca Apostolica ebr. 530.pdf"
    ),
    first_page=1,
    last_page=13,
    header_y1=60,
    footer_y0=701,
    expected_first=(1, "1"),
    expected_last=(1, '13'),
    expected_chapters=1,
    expected_verses=13,
    manuscript=(
        "Biblioteca Apostolica Vaticana, Vat. ebr. 530, part 1, fragment 11, "
        "folios 1r–2v"
    ),
    alt_namespace="https://digi.vatlib.it/view/MSS_Vat.ebr.530.pt.1",
    hebrew_work="Ebr530_JOH_Hebrew",
    translation_work="Ebr530_JOH_Gordon",
    title="John (Vatican, Vat. ebr. 530)",
    translation_title="English Translation of John (Vatican, Vat. ebr. 530)",
    description=(
        "Hebrew transcription and English translation of John from "
        "Biblioteca Apostolica Vaticana, Vat. ebr. 530, part 1, fragment 11, "
        "folios 1r–2v, transcribed, translated and annotated by Nehemia "
        "Gordon."
    ),
    descriptions=(
        (
            "x-contents",
            "Part 1, fragment 11 of a composite volume of fragments. Folios "
            "1r–2v carry Luke 1:1–35 and John 1:1–13 in Hebrew, each "
            "with a heading and a chapter heading.",
        ),
        (
            "x-script",
            "Fully pointed Hebrew. The pointing is irregular by Tiberian "
            "standards: dagesh is often omitted where expected, and kamatz "
            "and patach — like tsere and segol — are used "
            "interchangeably, which the editor relates to the Sephardic "
            "pronunciation and to Palestinian pointing (Nikud Eretz-Yisraeli).",
        ),
        (
            "x-provenance",
            "Held by the Biblioteca Apostolica Vaticana and published through "
            "its digitisation programme on DigiVatLib.",
        ),
        (
            "x-editorial",
            "The editor notes that a scribe scratched out Adonai and wrote "
            "Yehovah in its place, and records variant forms attested in "
            "Mishnaic manuscripts and in the Bar Ilan Responsa Database.",
        ),
    ),
    sources=(
        "Shelfmark, extent and repository from the manuscript's record in "
        "DigiVatLib, https://digi.vatlib.it/view/MSS_Vat.ebr.530.pt.1. "
        "Codicological detail is catalogued by Umberto Cassuto, 'Codices "
        "Vaticani Hebraici' — cited here, not consulted.",
        "Text, translation and annotation from Nehemia Gordon, 'Hebrew "
        "Gospels of Luke and John found in the Vatican Library', version 3.9 "
        "(2018), nehemiaswall.com.",
    ),
    coverage="John 1:1–13",
    date_calendar="Gregorian",
    original_date="undated",
    edition_date="2018",
    publisher="Nehemia Gordon",
    translator="Nehemia Gordon",
    contributor="Nehemia Gordon",
    contributor_file_as="Gordon, Nehemia",
    rights="© 2018 Nehemia Gordon. All rights reserved.",
    expected_hebrew_prefix="בְרֵאשִׁית",
    extractor="ebr530",
    pointed=True,
    footer_top=701.0,
    column_split=306.0,
)

MAT = BookProfile(
    key="mat",
    name="Matthew",
    osis_book="Matt",
    osis_book_name="Matthew",
    scope="MAT",
    stem="Cochin_MS_Oo.1.32_MAT",
    default_pdf="MS_Cochin_Oo.1.32_MAT_ProjectTruthMinistries.pdf",
    # The volume opens with front matter and stops at Matthew 19:30.
    first_page=12,
    last_page=966,
    # Matthew carries no running header; its verse headers start as high as
    # y=33, so clipping the top of the page would drop them.
    header_y1=0,
    footer_y0=728,
    expected_first=(1, "1"),
    expected_last=(19, "30"),
    expected_chapters=19,
    expected_verses=646,
    manuscript="MS.Oo.1.32",
    alt_namespace="https://projecttruthministries.org/studies/cochin-matthew/",
    hebrew_work="MS.Oo.1.32_MAT_Hebrew",
    translation_work="MS.Oo.1.32_MAT_PTM",
    title="Matthew (Cochin MS Oo.1.32)",
    translation_title="Translation of Matthew (Cochin MS Oo.1.32)",
    description=(
        "The Cochin Hebrew New Testament manuscripts are significant "
        "18th-century Hebrew versions of the New Testament. This file "
        "encodes Matthew from Cambridge MS Oo.1.32."
    ),
    descriptions=(
        (
            "x-contents",
            "Matthew 1:1–19:30. The edition stops at 19:30; the remaining "
            "chapters are not part of this volume.",
        ),
        (
            "x-script",
            "Essentially unpointed Hebrew, with gershayim marking divine names "
            "and abbreviations.",
        ),
        (
            "x-editorial",
            "Each verse is set with the Hebrew transcription, an English "
            "translation, The Scriptures 2009 for comparison, a Syriac Aramaic "
            "witness with its own English rendering, and an interlinear gloss "
            "table.",
        ),
    ),
    sources=(
        "Text, translation and commentary from Janice F. Baca, 'The Cochin "
        "Hebrew Book of Matthew' (Project Truth Ministries, 2025).",
    ),
    coverage="Matthew 1:1–19:30",
    original_date="ca. 1730",
    edition_date="2025",
    rights="© copyright 2025 Janice F. Baca",
    expected_hebrew_prefix="ספר הלידה",
    cochin_book="mat",
)

BOOK_PROFILES = {
    "rev": REV,
    "jas": JAS,
    "mat": MAT,
    "sloane_rev": SLOANE_REV,
    "ebr530_luke": EBR530_LUKE,
    "ebr530_john": EBR530_JOHN,
}


def get_profile(key: str) -> BookProfile:
    try:
        return BOOK_PROFILES[key.lower()]
    except KeyError as exc:
        valid = ", ".join(sorted(BOOK_PROFILES))
        raise ValueError(f"Unknown book {key!r}; expected one of: {valid}") from exc
