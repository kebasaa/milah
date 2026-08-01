# Milah

Milah is an offline desktop editor for Hebrew New Testament manuscripts. It
holds two workspaces that share a window and little else:

- **Comparison** — load OSIS witnesses, align their readings word for word,
  and build a reviewable Combined edition with its Strong's and interlinear
  lines beneath it.
- **Transcription** — put a photographed folio on screen with a magnifier over
  it, and type out what it says, with glosses suggested as you go.

Everything runs locally. The only part of Milah that reaches the network is the
manuscript download dialog, and only when you press Download.

The repository also holds a PDF-to-OSIS converter under
[`tools/`](tools/README.md). Milah reads that corpus but does not depend on the
converter, and neither needs the other to be built.

## Comparison

Load two or more manuscripts and Milah aligns them, marking letter-level
differences against a reference witness. Corresponding words share a column,
so a verse reads across the screen with each witness on its own row and the
Combined edition beneath them.

- **The reference is answerable per chapter and per verse.** The toolbar sets
  the chapter's, remembered as you move between chapters; right-clicking a
  manuscript's name, or any Combined word, sets one verse's. A witness that is
  silent for a verse is never its reference, so a verse only one manuscript
  covers still yields an edition rather than a blank row.
- **The Combined row is edited word by word.** Right-click for the other
  witnesses' readings, to divide a word at its maqaf, to join two back, or to
  accept a word into your dictionary. Suggestions from the shipped lexicons
  and phrase rules are offered, never applied.
- **The Strong's line** marks each word: `H1234`, `H1234?` where the lexicon
  knows several and shows the likeliest, `M` for a word the Mishnah or Tosefta
  attests but Strong's cannot number, `—` for one nothing knows, and `·D` for
  one you have defined yourself.
- **The Interlinear line** is editable too. It follows the aligned translation,
  joining several words to one Hebrew word with a dash — *to do* becomes
  `to-do` — until you type over it, and it is exactly what gets exported.
- **Notes.** A manuscript's own notes appear in the panel on the right, grouped
  by witness; a red `*` marks the words that carry one. Your own remarks go
  beneath them.
- **Translations** are aligned to the manuscript you choose — "Align with" on
  the translation's name — and their spans corrected with the arrows, narrowed,
  widened, merged, split or removed.

Books and chapters are chosen from two dropdowns covering everything any
manuscript has: **bold** where every loaded manuscript reaches it, *italic*
where they do not.

### Exports

**Export Combined** writes from the one path you choose:

| file | when |
|---|---|
| `Name.osis` | always — the edition alone |
| `Name-notes.osis` | when you have written notes |
| `Name-interlinear.osis` | when there is an interlinear to write |

## Transcription

The folio fills the top of the window with a loupe you move across it; the
words you read go in the grid below, laid out in the same aligned bands as the
comparison. Each Hebrew word gets a gloss suggested from the lexicon, which
stays yours once you edit it. A transcription remembers where it lives, so
saving after the first time asks nothing.

## The library and your dictionary

Downloaded manuscripts live in a library, so opening one does not mean
remembering where on disk it sits. Milah looks in `$MILAH_MANUSCRIPT_DIR`, then
a folder under the application data directory, then a `manuscripts` folder
beside the executable — which lets a portable copy ship texts that need no
download at all. Browsing for a file still works, and is how the
[`tools/data/01_osis`](tools/README.md) corpus is opened.

Words you accept can carry your own definitions, which the marker shows as `·D`
and which travel with you across projects rather than living in one.

## Requirements

- Windows 10 or 11.
- Qt 6.6 or newer with Core, Gui, Widgets and Network, built for MinGW.
- The MinGW toolchain, CMake 3.24+ and Ninja. A default Qt installation
  supplies all three under `Tools/`.

ZLIB and QuaZip are built automatically by the build script; nothing else is
needed.

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

## Running

Launch `milah-portable\Milah.exe`. OSIS files can also be named on the command
line, which is the quickest way to a populated window:

```powershell
.\milah-portable\Milah.exe tools\data\01_osis\JOH_Ebr530_hebrew.osis tools\data\01_osis\JOH_Ebr530_hebrew_consonantal.osis -t tools\data\01_osis\JOH_Ebr530_translation.osis
```

Do not run `Milah.exe` from the build folder unless the Qt runtime DLLs are on
`PATH`; the portable folder exists precisely so that is not necessary.

**A copy of `Milah.exe` without its `data\` folder loses its Strong's numbers**
— the interlinear marker row goes blank and the spelling checks stand down. The
status bar says which directories were searched when that happens.

## Tests

```powershell
.\build_milah_windows.ps1 -Tests
```

Thirteen QTest binaries run under `ctest`, covering OSIS import and note
anchoring, alignment and its scoring, consensus, coverage, the apparatus,
transcription, the manuscript catalogue, word markers, the lexicon, the
spelling suggestions, the grapheme diff, acronyms and the `.milah` round trip.

The comparison logic lives in `app/src/core/` and is built as a `MilahCore`
static library, so the tests exercise it without a display. `apparatus_test`
and `alignment_test` additionally read `tools/data/01_osis/`, checking the
editor against the converter's real output, and skip that part when the corpus
is not beside the build.

Sample OSIS documents live in `app/tests/test_data.cpp` rather than beside the
tests: moc mis-parses raw string literals, and a test file containing one is
silently reported as having no relevant classes.

## Project format

A `.milah` file is a versioned ZIP archive holding `manifest.json`, the source
OSIS files, the Combined state, the per-chapter and per-verse references,
divided words, notes, interlinear wording, translation associations and
alignment corrections, and a current Combined OSIS snapshot. Paths inside
archives are validated before reading or writing.

Source manuscript and translation text is immutable. Only the Combined text,
the interlinear, your notes and the alignment metadata can be changed.

## Data files

`app/data/` is read from disk at run time rather than compiled in, so a
regenerated lexicon replaces the old one without rebuilding: the Strong's
lexicon, the root index, the abbreviation table, the phrase rules and the
rabbinic word list. What each holds, where it came from and under what licence
is in [`app/data/README.md`](app/data/README.md) — which carries the
attribution CC-BY requires, and which travels with every build.

The generators that produce them are Python and live under
[`tools/python/tools/`](tools/README.md).

## Licence

Milah is free software under the [GNU General Public License, version
3](https://www.gnu.org/licenses/gpl-3.0.html) or later. It comes with no
warranty. See [`LICENSE`](LICENSE).

Copyright © 2026 Jonathan D. Müller.

The shipped data carries its own terms: Strong's *Concise Dictionary* is public
domain; morphhb and STEP Bible's TBESH are CC BY 4.0; the rabbinic word list is
drawn from public-domain and CC-BY versions in the Sefaria export. Details are
in [`app/data/README.md`](app/data/README.md).
