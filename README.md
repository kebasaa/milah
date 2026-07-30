# milah

Manuscript comparison software and a reproducible converter for Hebrew New
Testament manuscript PDFs.

## PDF to OSIS

All Python commands use the shared `tp` environment, created by
`python\install_tp.bat` at `%USERPROFILE%\.venvs\tp`.

```powershell
& "$env:USERPROFILE\.venvs\tp\Scripts\python.exe" -m pip install --no-build-isolation -e ".[dev]"
& "$env:USERPROFILE\.venvs\tp\Scripts\python.exe" -m pytest --basetemp .pytest-tmp
```

Convert one PDF:

```powershell
& "$env:USERPROFILE\.venvs\tp\Scripts\python.exe" -m pdf2osis convert `
  --book sloane_rev `
  --input "data/00_source_files/A-Hebrew-Manuscript-of-the-Book-of-Revelation-British-Library-Sloane-273.pdf" `
  --output-dir data/01_osis
```

Books are `rev`, `jas` and `mat` (Project Truth Ministries Cochin editions),
`sloane_rev` (British Library, Sloane MS 237) and `ebr530_luke` /
`ebr530_john` (Vatican, Vat. ebr. 530). `convert-all` converts every one:

```powershell
& "$env:USERPROFILE\.venvs\tp\Scripts\python.exe" -m pdf2osis convert-all `
  --source-dir data/00_source_files `
  --output-dir data/01_osis
```

Each conversion writes clean Hebrew, annotated Hebrew and annotated English
OSIS; pointed manuscripts also get a niqqud-stripped consonantal variant. The
converter parses and validates every document before replacing any existing
output. Its JSON report lists coverage, empty and alternate verses, note
counts, excluded markers and source anomalies.

### Pointed manuscripts

MuPDF ≥1.26 already returns Hebrew in logical order, so nothing reverses it —
re-reversing, as the old notebooks did, roughly doubles the error rate. What
does need repair is the glyph mapping, and the two pointed manuscripts fail in
opposite ways:

| source | ToUnicode | embedded `cmap` | primitive |
|---|---|---|---|
| Sloane 237 | broken, ~12% of glyphs | present, correct | `get_texttrace` + font cmap |
| Vat. ebr. 530 | correct | **absent entirely** | `rawdict` glyph geometry |

Sloane 237's PDF embeds `David` as a Type0/Identity-H subset whose ToUnicode
CMap covers 30 of the ~170 glyph IDs in use and gets several wrong: sheva
decodes as dagesh, hiriq and qubuts as a shin dot, hataf segol as a space —
which is why a naive extraction produces more shin dots than shins. Its
embedded `cmap` is correct, so `pdf2osis.glyphs` resolves glyph IDs there.

Vat. ebr. 530's subsets have no `cmap` at all, and `get_texttrace` merges
adjacent runs — sometimes two whole printed lines — into one span, so reversing
a span swaps its runs. That profile reads `rawdict` and places each glyph by its
own coordinates. A combining mark is drawn at its base letter's left edge, so
attachment is exact; where two vowels overlap between neighbouring letters, the
rule that no letter takes the same point twice separates them.

`pdf2osis.layout` detects the footnote separator rule per page instead of
assuming a fixed y band; the rule moves between y=518 and y=621, and a fixed
band both dropped Revelation 1:18 and double-counted body text as footnotes.

### OSIS structure

All output uses milestoned `<chapter>` and `<verse>` so that material
belonging to no verse can sit in the flow:

- `<div type="introduction">` with a `<title type="main">` for the manuscript
  incipit, and `<div type="titlePage">` for the edition's title block
- `<title type="chapter">` for the gate heading dividing the two chapters
- `<milestone type="pb">` for the eight folio boundaries, at their true
  position — including mid-verse
- `<milestone type="x-ms-verse">` for divisions the manuscript numbers but the
  edition leaves unnumbered

Notes are `<note type="explanation" placement="foot">` with `osisRef` and
`osisID`, anchored at the offset where their superscript is printed.
`type="footnote"` is *not* a valid OSIS 2.1.1 note type, so nothing emits it.

Where a source states its own reference for a verse, it rides on
`subType="x-alt-…"` — Sloane's Hebrew letter-numerals, which disagree with the
printed numbering at Rev 1:9, 1:15, 1:16, 1:17 and 2:8, and Cochin's own
chapter and verse. OSIS requires such values to begin with `x-`, which is why
this is not a private-namespace attribute. The canonical reference stays in
`osisID`, and `n` carries the printed label — a range, such as `19-20`, where
one record covers two verses.

Every file validates against the upstream `osisCore.2.1.1.xsd`, vendored in
`python/pdf2osis/schema/`, and is pretty-printed one verse per line.

### Coverage

| profile | records | extent |
|---|---|---|
| `rev` | 405 | Revelation 1:1–22:21, including the combined `14:19-20` record |
| `jas` | 107 | James 1:1–5:20; its `2:15` record covers KJV 2:15–16, so there is no independent `2:26` |
| `mat` | 646 | Matthew 1:1–19:30; the volume stops there |
| `sloane_rev` | 33 | Revelation 1:1–2:13 |
| `ebr530_luke` / `ebr530_john` | 35 / 13 | Luke 1:1–35, John 1:1–13 |

Revelation is **405**, arrived at from two corrections in opposite directions.
`Rev 2:26` (PDF page 54) and `Rev 20:12` (page 329) are in the source but their
headers are not set at the usual type size, so a size-keyed search missed them;
both were checked against the rendered pages. Against that, the second
`Revelation 2:21` header is not a verse at all — see below — so counting it gave
406.

#### Verses the manuscripts lack

Five references are printed with no text because the manuscript has none:
`Rev 2:6`, `2:28`, `9:9`, `16:11` and `Jas 1:21`. Each is emitted as a numbered,
empty verse carrying the edition's own notice — "This verse does not exist in
the Cochin Oo.1.16.2 manuscript" — as a `<note type="explanation">` with
`osisID="…!note.absent"`. Absence by design is therefore never mistaken for a
gap in extraction.

Detection is anchored to the start of a transcription, translation or `Note:`
field. The same words appear elsewhere meaning something else: `Rev 14:19-20`'s
note says "verse 20 does not exist" while the record itself holds verse 19, Matt
8:10 translates "there does not exist faith like this", and the KJV and Aramaic
comparison columns use the phrase for their own missing text.

#### Verses the manuscript transposes

Cochin Oo.1.16.2 swaps `Rev 2:21` and `2:22` — Cochin 2:20 = KJV 2:22, Cochin
2:21 = KJV 2:21 — and the edition follows the manuscript's order. It marks the
swap with a bare `Revelation 2:21` header on page 50 whose whole content is
"Note: The Cochin manuscript changes the order of the following verses"; the
verse itself is on page 51. That signpost is not a verse, and counting it split
2:21 into a spurious empty `21a` and a real `21b`. It now attaches to `Rev.2.21`
as a `…!note.order` note.

Both transposed verses carry `type="x-reordered"`, which is a separate
`attributeExtension` from the `subType` holding the Cochin reference, so the
flag survives into the `hebrew` variant that has no apparatus. They are the only
two out-of-order verses in the corpus; James and Matthew have none.

### Cochin extraction

The three Cochin PDFs share a house style but not a format, so each has its own
extractor in `pdf2osis/cochin.py`: Revelation is headed `Revelation N:V (Cochin
N:V)` and carries an interlinear gloss table, James is headed `James N:V (KJV …)`
with no such table, and Matthew is headed `Chapter C:V` and prints a Syriac
Aramaic column at the same type size as its English — so script, not size,
separates them.

Where the interlinear table repeats a transcription word at the same length and
differs in exactly one letter, the gloss wins: the two are set in different
subsets of one face and each resolves letters the other confuses (he read as
het, bet as kaf). Anything less clear-cut is left alone, because the
transcription is the authority on wording and order.

Output is pretty-printed one verse per line. lxml's `pretty_print` refuses to
reformat mixed content, which is exactly what milestone form produces, so
`pdf2osis.osis.indent_body` sets the tails by hand.

## Known issues

- The Cochin editions print no material outside their verses, so unlike the two
  manuscripts they carry no `<title>`, folio milestones or introduction div —
  there is nothing in the source to put there.
- The absence and order notices are attached as notes, so they appear only in
  the `hebrew_commented` and `translation` variants. The `x-reordered` flag is
  an attribute and appears in all of them.
- PyMuPDF must be ≥1.26, where MuPDF switched to returning text in logical
  order. Every extractor reads Hebrew on that assumption, so an older wheel
  reverses it silently rather than failing.
- The Milah editor in `app/` is a separate Qt 6 desktop application with its own
  build and tests; see `app/README.md`. It reads this corpus but does not depend
  on the converter.
