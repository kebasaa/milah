# milah

Manuscript comparison software and a reproducible converter for the Project
Truth Ministries Cochin Revelation and James PDFs.

## PDF to OSIS

All Python commands for this repository use the `tp` Conda environment.

```powershell
conda run -n tp python -m pip install --no-build-isolation -e ".[dev]"
conda run -n tp python -m pytest --basetemp .pytest-tmp
```

Convert one PDF:

```powershell
conda run -n tp python -m pdf2osis convert `
  --book rev `
  --input data/00_source_files/MS_Cochin_Oo.1.16.2_REV_ProjectTruthMinistries.pdf `
  --output-dir data/01_osis
```

Use `--book jas` with the James PDF, or convert both canonical inputs:

```powershell
conda run -n tp python -m pdf2osis convert-all `
  --source-dir data/00_source_files `
  --output-dir data/01_osis
```

Each conversion writes clean Hebrew, annotated Hebrew, and annotated English
OSIS. The converter parses and validates all three documents before replacing
any existing outputs. Its JSON report lists coverage, empty and alternate
verses, note counts, transcription/interlinear disagreements, excluded
markers, and source anomalies.

Revelation uses explicit PDF layout zones. The displayed transcription
controls its wording and sequence, while corresponding interlinear Hebrew
tokens correct PDF font mappings and supply Hebrew-only footnote markers.
English is read only from the labelled translation paragraph. James uses the
same strict zone boundaries and falls back to its displayed transcription
because that PDF has no equivalent interlinear Hebrew table.

Audit generated Revelation files against the historical outputs:

```powershell
conda run -n tp python -m pdf2osis compare `
  --book rev `
  --generated-dir data/01_osis `
  --reference-dir data/01b_osis_reference
```

The comparison command reads the malformed historical XML in recovery mode
and classifies coverage differences, shifted reference verses, reference
contamination, source-text differences, and generated Hebrew contamination.
The production conversion commands never read the reference files.

The James PDF contains 107 source records: its `James 2:15` record corresponds
to KJV 2:15–16, so the PDF has no independent `James 2:26` record. The
Revelation PDF contains 404 source records, including the combined
`Revelation 14:19-20` record.
