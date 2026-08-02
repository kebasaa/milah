# Milah

Milah is an offline desktop editor for Hebrew New Testament manuscripts. One
window holds two tabs that share little beyond the window itself:

- **Textual criticism** — load OSIS witnesses, align their readings word for
  word, and build a reviewable Combined edition with Strong's numbers and an
  interlinear translation beneath it.
- **Transcription** — put a photographed folio on screen with a magnifier over
  it, and type out what it says, with glosses suggested as you go.

Everything runs locally. The only part of Milah that reaches the network is
the manuscript download dialog, and only when you press Download.

The repository also holds a PDF-to-OSIS converter under
[`tools/`](tools/README.md). Milah reads that corpus but does not depend on the
converter, and neither needs the other to be built.

This file is the manual: what every menu, field and shortcut does, in the
order you use them. If something in the running application does not match
what is written here, the application is right and this file has drifted —
say so.

## Getting started

1. Launch `milah-portable\Milah.exe`.
2. Press **Ctrl+M** and load two or more manuscripts of the same passage —
   `Load manuscripts` opens your library, or Browse for a file directly.
3. Pick a **Book** and **Chapter** from the toolbar.
4. Read the **Combined** row at the bottom of each verse card: it is Milah's
   working edition, built by aligning the witnesses you loaded.
5. Press **Ctrl+E** to export it as OSIS.

The rest of this file covers both tabs in full before returning to exporting,
since the exports differ by tab and are easier to read once you know what
produced them.

## The two tabs

The menu bar's top-right corner carries a small tab strip: **Textual
criticism** (**Ctrl+1**) and **Transcription** (**Ctrl+2**). Switching tabs
swaps the menus, the toolbar beneath them, and the dock on the right — each
tab remembers whether you had closed its dock. The two tabs do not share
unsaved work: leaving one with something unsaved asks before it is discarded,
independently of the other.

## Textual criticism

### Loading manuscripts and translations

**File ▸ Load manuscripts** (**Ctrl+M**) opens your manuscript library — see
[The library](#the-library-and-your-dictionary) below. **File ▸ Load
translations** (**Ctrl+T**) does the same for a modern-language translation to
align against a manuscript. **File ▸ Download manuscripts…** fetches
manuscripts published online into the library, so they need no separate file
to keep track of. Either dialog can also Browse for a file directly, which is
how the [`tools/data/01_osis`](tools/README.md) corpus, or any other OSIS
file, gets opened.

**File ▸ Open project** (**Ctrl+O**) and **File ▸ Save project** (**Ctrl+S**)
load and write a `.milah` project — see [Project format](#project-format) —
which remembers every manuscript, translation, edit and note together.
**File ▸ Close project** (**Ctrl+W**) clears the window, asking first if
anything would be lost.

### Book and chapter

The toolbar's **Book** and **Chapter** combos list every book and chapter any
loaded manuscript reaches — not just the ones every witness shares. An entry
is **bold** where every loaded manuscript covers it, and *italic* where at
least one does not; hovering an italic chapter names which manuscripts are
missing it. The **←** and **→** toolbar buttons (**Alt+←** / **Alt+→**) step
through chapters one at a time.

### The reference

A verse is compared by marking every witness's differences against one of
them, its *reference*. Milah answers this two ways at once:

- The toolbar's **Reference** combo sets the reference for the whole chapter,
  and is remembered chapter by chapter as you move around — the `.milah`
  project keeps this too.
- Right-clicking a manuscript's name in a verse card offers **Read this verse
  against `<acronym>`**, which sets the reference for that one verse only,
  overriding the chapter's choice; **Follow the chapter's reference** clears
  it back. The same choice is repeated in the Combined word's own context menu,
  under **Reference for this verse**.

A witness silent for a verse is never used as its reference, so a verse only
one manuscript covers still produces an edition rather than a blank row.

### Reading a verse

Each verse is a card with one row per loaded manuscript, a row for its
acronym, and the **Combined** row beneath them. Corresponding words share a
column, so the verse reads across the screen. Letters that differ from the
reference witness are marked within each word, not just the word as a whole.
Toggling **Strong's** on the toolbar adds a row of Strong's numbers under
Combined; when a translation is loaded and aligned, an **Interlinear** row
appears beneath that.

### Editing the Combined row

Right-clicking a Combined word opens a menu built from what that word allows:

| Entry | What it does |
|---|---|
| `Split into A + B` | Divides the word into two columns, e.g. at a maqaf. |
| `Merge with the previous/next word, <word>` | Rejoins two columns this editor divided, separated by a space. |
| Each witness's reading, `<text>  —  <acronym>` | Replaces the Combined word with that witness's own. |
| `Change to <suggestion>` | Applies a spelling suggestion from the shipped lexicon or phrase rules, offered but never applied on its own. |
| `Add/edit note` | Opens the word in the Notes panel, for your own remark on it. |
| `Reference for this verse ▸` | Sets this verse's reference, as above. |
| `Define <word> in my dictionary` | Opens the dictionary entry dialog for it. |
| `Omit this word` | Clears it from Combined. |

The same edits are available from **Edit ▸ Split word**, **Merge with
previous word** and **Merge with next word** (**Ctrl+Shift+S** for Split)
while the caret is in a Combined word, and **Edit ▸ Define word in my
dictionary**.

### The Strong's row

Each word under Combined is marked once Strong's is switched on:

| Marker | Meaning |
|---|---|
| `H1234` | The lexicon knows one Strong's number for this word. |
| `H1234?` | The lexicon knows several; the likeliest is shown. |
| `M` | Not in Strong's, but the Mishnah or Tosefta attests the word. |
| `—` | Nothing in the shipped data knows this word. |
| `·D` | You have defined this word yourself, in your dictionary. |

### Translations and the Interlinear row

Loading a translation (**Ctrl+T**) aligns it word-by-word against a
manuscript, filling the Interlinear row under the Combined words that
manuscript's columns cover. Right-clicking a translation's name offers:

- **Align with ▸** — realigns it against a different loaded manuscript, for
  when it was aligned against one whose words come in a different order.
- **Close translation** — drops it. Its aligned spans are discarded, so the
  card asks first.

Each translation span carries its own arrows to move it a column at a time,
and a trashcan to remove it. The Interlinear row itself is directly editable —
type over what it suggests and your wording is kept, joining several English
words onto one Hebrew word with a dash (*to be* becomes `to-be`). Parentheses
and other punctuation stay attached to the word they surround rather than
becoming words of their own.

### Notes

Right-click **Add/edit note** on a Combined word, or select it and use the
Notes panel under Review filters, to write your own remark. A manuscript's own
notes and comments — carried over from its OSIS source — appear in the same
panel, grouped by witness, whenever a word that has one is selected; such
words are marked with a small red `*` in their table cell so you know to look.

### Review filters

The right-hand dock's **Review filters** group narrows which words are
highlighted for a second look: **Consensus ties** (the witnesses split with no
majority), **Manually edited**, **Missing readings** and **Uncertain
translation**.

### Regenerate

**Regenerate** (toolbar, or **F5**) rebuilds the Combined row for the current
chapter from the loaded manuscripts and the current reference, discarding
whatever hand-editing produced the row before. It warns first, naming exactly
what would be lost — manual edits, notes, or interlinear wording you typed —
and only when there is something to lose; a chapter regenerated with nothing
on it yet opens with no warning at all.

### Menus and toolbar at a glance

**File:** Open project, Save project, Close project │ Download manuscripts…,
Load manuscripts, Load translations │ Export Combined │ Save my dictionary
as…, Load a dictionary… │ Quit.
**Edit:** Undo, Redo │ Split word, Merge with previous word, Merge with next
word │ Define word in my dictionary.
**Toolbar:** Book, Chapter, ← →, Reference, Strong's, Regenerate.
**Right-hand dock:** Loaded manuscripts, Translations, Review filters, Notes.

## Transcription

### Opening a folio

**File ▸ Open Image** opens a photograph of a folio; **←** and **→** on the
transcription toolbar (**Alt+←** / **Alt+→**) step to the neighbouring images
in the same folder, so a whole manuscript can be worked through in order.
**File ▸ Open Transcription Project** and **Save Transcription project**
(**Ctrl+S**) load and write the project once it has a file to live in — the
first save asks where; every one after that does not. **File ▸ Close
Transcription Project** (**Ctrl+W**) clears the window, asking first if
anything is unsaved.

### The loupe

Toggling **Magnify** on the toolbar moves a magnifier with the pointer over
the folio; the mouse wheel changes how much it enlarges. It is off by default,
so it is not in the way of reading a page whole.

### Book and chapter

Type the book's name into the **Book** field — a dropdown completer suggests
canonical names as you type, and filling it in with a canonical name also
fills the read-only field beside it, labelled by " as ", with the OSIS id the
verses will be exported under. For a work outside the canon (e.g. an
apocryphal text), the id field is yours to write instead, since nobody but you
can say what it should be called. **Chapter** sets which chapter the folio
opens in; where a chapter begins partway down the page, right-click that
verse's own number instead — see below.

### Typing the text

The word grid below the folio has one editable row for the Hebrew and one for
its English gloss, laid out in the same aligned bands the textual-criticism
verse cards use. Typing follows the reading direction of the row you are in:

| Key | What it does |
|---|---|
| Space | Divides the word here — or, at the start of a verse cell, opens a new verse if what was typed is a number. |
| Backspace at the start of a word | Joins it onto the word before it. |
| Tab / Shift+Tab | Moves along the row. |
| ↑ / ↓ | Moves between the Hebrew row and its gloss. |
| ← / → | Steps out of a full field into the next one. |

Right-clicking a verse's number offers **Move to new chapter** — everything
from that verse on becomes the next chapter — and **Delete this verse**.
**Edit ▸ Move verse to new chapter** repeats the first for whichever verse has
the caret.

### The metadata dock

The right-hand dock records what the folio is, as against what it says, all
optional: **Manuscript**, **Transcriber**, **Origin**, **Library**,
**Shelfmark**, **Date**, **Language**, **Notes**, then **Save details**.

### Exporting

**File ▸ Export to OSIS** (**Ctrl+E**) writes the transcription out — see
[Exports](#exports) below.

### Menus and toolbar at a glance

**File:** Open Image, Open Transcription Project, Save Transcription project │
Export to OSIS, Close Transcription Project │ Quit.
**Edit:** Undo, Redo │ Move verse to new chapter │ Define word in my dictionary.
**Toolbar:** Book, " as " acronym, Chapter, ← →, Magnify.

## Exports

**Textual criticism — File ▸ Export Combined** (**Ctrl+E**) asks for one file
path and writes from it:

| File | When |
|---|---|
| `Name.osis` | Always — the edition alone. |
| `Name-notes.osis` | When you have written notes. |
| `Name-interlinear.osis` | When there is an interlinear translation to write. |

The status bar names exactly which files were written.

**Transcription — Export to OSIS** (**Ctrl+E**) writes the folio's transcribed
text as a single OSIS file, addressed by the book id and chapter the toolbar
holds.

## Keyboard shortcuts

| Shortcut | Textual criticism | Transcription |
|---|---|---|
| Ctrl+1 / Ctrl+2 | Switch to this tab | Switch to this tab |
| Ctrl+O | Open project | — |
| Ctrl+S | Save project | Save transcription project |
| Ctrl+W | Close project | Close transcription project |
| Ctrl+E | Export Combined | Export to OSIS |
| Ctrl+M | Load manuscripts | — |
| Ctrl+T | Load translations | — |
| Ctrl+Z / Ctrl+Y | Undo / Redo | Undo / Redo |
| Ctrl+Shift+S | Split word | — |
| F5 | Regenerate | — |
| Alt+← / Alt+→ | Previous/next chapter | Previous/next image |
| Ctrl+Q | Quit | Quit |
| F1 | About | About |

Ctrl+S, Ctrl+W and Ctrl+E carry different actions in each tab on purpose — at
most one of the pair is ever enabled, so the shortcut is never ambiguous.

## The library and your dictionary

Downloaded manuscripts live in a library, so opening one does not mean
remembering where on disk it sits. Milah looks in `$MILAH_MANUSCRIPT_DIR`, then
a folder under the application data directory, then a `manuscripts` folder
beside the executable — which lets a portable copy ship texts that need no
download at all. Browsing for a file still works, and is how the
[`tools/data/01_osis`](tools/README.md) corpus is opened.

Words you accept can carry your own definitions, which the marker shows as
`·D` and which travel with you across projects rather than living in one.
**File ▸ Save my dictionary as…** and **Load a dictionary…** back it up and
restore it.

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
.\milah-portable\Milah.exe tools\data\01_osis\John_Ebr530_hebrew.osis -t tools\data\01_osis\John_Ebr530_translation.osis
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
