This folder contains the `pdf2osis` package and the conversion notebooks.

The supported converter is the CLI:

```powershell
& "$env:USERPROFILE\.venvs\tp\Scripts\python.exe" -m pdf2osis convert-all `
  --source-dir data/00_source_files `
  --output-dir data/01_osis
```

`pdf2osis_cli.ipynb` is a thin interactive wrapper around the same package;
`03_pdf2osis_REV_sloane237.ipynb` drives the Sloane 237 profile,
`04_pdf2osis_LUK_JOH_ebr530.ipynb` the two Vatican ebr. 530 profiles, and
`05_pdf2osis_cochin.ipynb` the three Cochin editions.

Package layout:

- `glyphs.py` — glyph-accurate decoding for PDFs whose ToUnicode CMap is wrong,
  resolving glyph IDs against each embedded font's own `cmap`
- `layout.py` — page geometry: column split, footnote separator detection,
  superscript and footnote-definition extraction
- `sloane.py` — extraction for British Library, Sloane MS 237
- `ebr530.py` — extraction for Vatican, Vat. ebr. 530 (Luke and John)
- `cochin.py` — one extractor per Cochin edition (`rev`, `jas`, `mat`)
- `osis.py` — OSIS construction: milestoned verses, attributed headers, and
  the indentation pass that gives one verse per line
- `validate.py`, `converter.py`, `profiles.py`, `models.py`

## Reading order and fonts

The two pointed manuscripts fail in opposite ways, so each profile names the
primitive it needs:

| source | ToUnicode | embedded `cmap` | primitive |
|---|---|---|---|
| Sloane 237 | broken, ~12% of glyphs | present, correct | `get_texttrace` + font cmap |
| Vat. ebr. 530 | correct | **absent** | `rawdict` glyph geometry |

MuPDF ≥1.26 returns Hebrew already in logical order, so nothing reverses it.
