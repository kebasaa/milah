This folder contains the `pdf2osis` package and historical conversion
notebooks.

The supported converter is the CLI:

```powershell
conda run -n tp python -m pdf2osis convert-all `
  --source-dir data/00_source_files `
  --output-dir data/01_osis
```

`pdf2osis_cli.ipynb` is a thin interactive wrapper around the same package.
The older notebooks and `_old` scripts remain for diagnostic comparison; they
are not independent supported conversion pipelines.

Use `python -m pdf2osis compare` to audit generated Revelation OSIS against
`data/01b_osis_reference` without making the reference a runtime dependency.
