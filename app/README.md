# Milah

Milah is an offline manuscript-comparison editor. It loads read-only OSIS
witnesses and optional translations, aligns their readings, generates a
reviewable Combined edition, and saves a portable `.milah` project.

Milah is deliberately independent of every tool outside this directory. It
does not import PDFs and does not use the repository's PDF-to-OSIS converter.
The one exception is the test suite, which reads `data/01_osis/` when it is
present so the editor is checked against the converter's real output.

## Current implementation

- C++20 Qt 6 Widgets desktop application. No web view, no JavaScript, no
  Node.js.
- Namespace-aware OSIS import with safe DTD/entity rejection, built on
  `QXmlStreamReader`.
- Common-book/chapter detection, RTL token grids, note tooltips, manuscript
  alignment, majority consensus, priority-witness tie handling, and Combined
  editing.
- Optional translation association and correctable alignment spans.
- Atomic portable-project storage using ZIP, plus standalone Combined OSIS
  export.

The comparison logic lives in `src/core/` and is built as a `MilahCore` static
library, so the tests exercise it without a display. `src/app_controller.*`
holds the editing session; `src/main_window.*` and `src/ui/` are the interface.

## Requirements

- Windows 10 or 11.
- Qt 6.6 or newer with Core, Gui and Widgets, built for MinGW.
- The MinGW toolchain, CMake 3.24+ and Ninja. A default Qt installation
  supplies all three under `Tools/`.

ZLIB and QuaZip are built automatically by the build script; nothing else is
needed. Visual Studio, Qt WebEngine, Qt WebChannel and Node.js are **not**
required.

## Build

From the repository root:

```powershell
.\build_milah_windows.ps1
```

The script discovers Qt, MinGW, CMake and Ninja — checking `C:\Qt` and
`%USERPROFILE%\Qt` — builds the ZLIB and QuaZip dependencies under `build\deps\`,
builds Milah, and assembles a self-contained `milah-portable\` folder with
`windeployqt`. Nothing needs to be on `PATH` beforehand.

Useful switches: `-Configuration Debug`, `-Tests`, `-Clean`, `-SkipPortable`,
`-KeepBuildArtifacts`, `-NoDownload`. Paths can be forced with `-QtBin`,
`-MinGwBin`, `-CMake`, `-Ninja`, `-QuaZipRoot` and `-ZlibRoot`.

To build by hand instead:

```powershell
cmake -S app -B build/milah -G Ninja -DCMAKE_PREFIX_PATH="<qt-kit>;<quazip>;<zlib>"
cmake --build build/milah
```

The resulting desktop executable is `Milah.exe`.

## Running

Launch `milah-portable\Milah.exe`. OSIS files can also be named on the command
line, which is the quickest way to get to a populated window:

```powershell
.\milah-portable\Milah.exe data\01_osis\JOH_Ebr530_hebrew.osis data\01_osis\JOH_Ebr530_hebrew_consonantal.osis -t data\01_osis\JOH_Ebr530_translation.osis
```

Do not run `Milah.exe` from the build folder unless the Qt runtime DLLs are on
`PATH`; the portable folder exists precisely so that is not necessary.

## Tests

```powershell
.\build_milah_windows.ps1 -Tests
```

Four QTest binaries run under `ctest`:

- `tests/osis_test.cpp` — Hebrew import, note anchoring, DTD/entity rejection.
- `tests/consensus_test.cpp` — majority consensus and priority-witness ties.
- `tests/apparatus_test.cpp` — titles, folio milestones, alternate verse
  numbering, and the Combined OSIS round trip. It also parses every
  `REV_Sloane237_*` file in `data/01_osis/`, skipping that part when the corpus
  is not next to the build.
- `tests/project_storage_test.cpp` — `.milah` archive round trip.

Sample OSIS documents live in `tests/test_data.cpp` rather than beside the
tests: moc mis-parses raw string literals, and a test file containing one is
silently reported as having no relevant classes.

## Project format

A `.milah` file is a versioned ZIP archive. It contains `manifest.json`,
embedded source OSIS files, the Combined state, translation associations and
alignment corrections, review state, and a current Combined OSIS snapshot.
Paths inside archives are validated before reading or writing.

Source manuscript and translation text is immutable. Only Combined text and
translation alignment metadata can be changed.
