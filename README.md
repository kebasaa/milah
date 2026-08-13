![C++](https://img.shields.io/badge/c++-%2300599C.svg?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-%23217346.svg?style=for-the-badge&logo=Qt&logoColor=white)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

# Milah

Milah is an offline desktop editor for Hebrew manuscripts. One window holds two
tabs that share little beyond the window itself:

- **Textual criticism** — load OSIS witnesses, align their readings word for
  word, and build a reviewable Combined edition with Strong's numbers and an
  interlinear translation beneath it.
- **Transcription** — put a photographed folio on screen with a magnifier over
  it, and type out what it says, with glosses suggested as you go.

Everything runs locally. The only part of Milah that reaches the network is
the manuscript download dialog and the dialog that pulls in manuscript scans
from online sources, and only when you press Download.

## Manual

### Getting started

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

### The two tabs

The menu bar's top-right corner carries a small tab strip: **Textual
criticism** (**Ctrl+1**) and **Transcription** (**Ctrl+2**). Switching tabs
swaps the menus, the toolbar beneath them, and the dock on the right — each
tab remembers whether you had closed its dock. The two tabs do not share
unsaved work: leaving one with something unsaved asks before it is discarded,
independently of the other.

### Textual criticism

#### Loading manuscripts and translations

**File ▸ Load manuscripts** (**Ctrl+M**) opens your manuscript library — see
[The library](#the-library-and-your-dictionary) below. **File ▸ Load
translations** (**Ctrl+T**) does the same for a modern-language translation to
align against a manuscript. **File ▸ Download manuscripts…** fetches
manuscripts published online into the library, so they need no separate file
to keep track of. Either dialog can also Browse for a file directly, which is
how any other OSIS file gets opened.

**File ▸ Open project** (**Ctrl+O**) and **File ▸ Save project** (**Ctrl+S**)
load and write a `.milah` project — see [Project format](#project-format) —
which remembers every manuscript, translation, edit and note together.
**File ▸ Close project** (**Ctrl+W**) clears the window, asking first if
anything would be lost.

#### Book and chapter

The toolbar's **Book** and **Chapter** combos list every book and chapter any
loaded manuscript reaches — not just the ones every witness shares. An entry
is **bold** where every loaded manuscript covers it, and *italic* where at
least one does not; hovering an italic chapter names which manuscripts are
missing it. The **←** and **→** toolbar buttons (**Alt+←** / **Alt+→**) step
through chapters one at a time.

#### The reference

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

#### Reading a verse

Each verse is a card with one row per loaded manuscript, a row for its
acronym, and the **Combined** row beneath them. Corresponding words share a
column, so the verse reads across the screen. Letters that differ from the
reference witness are marked within each word, not just the word as a whole.
Toggling **Strong's** on the toolbar adds a row of Strong's numbers under
Combined; when a translation is loaded and aligned, an **Interlinear** row
appears beneath that.

#### Editing the Combined row

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

#### The Strong's row

Each word under Combined is marked once Strong's is switched on:

| Marker | Meaning |
|---|---|
| `H1234` | The lexicon knows one Strong's number for this word. |
| `H1234?` | The lexicon knows several; the likeliest is shown. |
| `M` | Not in Strong's, but the Mishnah or Tosefta attests the word. |
| `—` | Nothing in the shipped data knows this word. |
| `·D` | You have defined this word yourself, in your dictionary. |

#### Translations and the Interlinear row

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

#### Notes

Right-click **Add/edit note** on a Combined word, or select it and use the
Notes panel under Review filters, to write your own remark. A manuscript's own
notes and comments — carried over from its OSIS source — appear in the same
panel, grouped by witness, whenever a word that has one is selected; such
words are marked with a small red `*` in their table cell so you know to look.

#### Review filters

The right-hand dock's **Review filters** group narrows which words are
highlighted for a second look: **Consensus ties** (the witnesses split with no
majority), **Manually edited**, **Missing readings** and **Uncertain
translation**.

#### Regenerate

**Regenerate** (toolbar, or **F5**) rebuilds the Combined row for the current
chapter from the loaded manuscripts and the current reference, discarding
whatever hand-editing produced the row before. It warns first, naming exactly
what would be lost — manual edits, notes, or interlinear wording you typed —
and only when there is something to lose; a chapter regenerated with nothing
on it yet opens with no warning at all.

#### Menus and toolbar at a glance

**File:** Open project, Save project, Close project │ Download manuscripts…,
Load manuscripts, Load translations │ Export Combined │ Save my dictionary
as…, Load a dictionary… │ Quit.
**Edit:** Undo, Redo │ Split word, Merge with previous word, Merge with next
word │ Define word in my dictionary.
**Toolbar:** Book, Chapter, ← →, Reference, Strong's, Regenerate.
**Right-hand dock:** Loaded manuscripts, Translations, Review filters, Notes.

### Transcription

#### Opening a folio

**File ▸ Open Image** opens a photograph of a folio; **←** and **→** on the
transcription toolbar (**Alt+←** / **Alt+→**) step to the neighbouring images
in the same folder, so a whole manuscript can be worked through in order.
**File ▸ Open Transcription Project** and **Save Transcription project**
(**Ctrl+S**) load and write the project once it has a file to live in — the
first save asks where; every one after that does not. **File ▸ Close
Transcription Project** (**Ctrl+W**) clears the window, asking first if
anything is unsaved.

#### The loupe

Toggling **Magnify** on the toolbar moves a magnifier with the pointer over
the folio; the mouse wheel changes how much it enlarges. It is off by default,
so it is not in the way of reading a page whole.

#### Book and chapter

Type the book's name into the **Book** field — a dropdown completer suggests
canonical names as you type, and filling it in with a canonical name also
fills the read-only field beside it, labelled by " as ", with the OSIS id the
verses will be exported under. For a work outside the canon (e.g. an
apocryphal text), the id field is yours to write instead, since nobody but you
can say what it should be called. **Chapter** sets which chapter the folio
opens in; where a chapter begins partway down the page, right-click that
verse's own number instead — see below.

#### Typing the text

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

Right-clicking a word offers **Add/edit note**, **Remove note** where there is
one, and **Delete word** — which takes that word off the folio and leaves the
verse and the words around it where they are. A recogniser that read one word
too many has not made the verse wrong. Ctrl+Z puts it back.

#### Reading a folio with Kraken

**Transcribe** on the toolbar hands the folio to
[Kraken](https://github.com/mittagessen/kraken), a handwriting recogniser, and
fills the grid with what it read. Every word arrives marked *unchecked* and is
drawn quieter than the rest; moving the caret through a word marks it checked,
whether or not you change it. The status bar says how many are left.

The **eye** button beside it draws each recognised word over the ink it was read
from, so you can see at a glance what was read where — struck through while the
overlay is off, and greyed out on a folio that has no readings to show. Words
still unchecked are outlined with a dashed box. A finished recognition opens the
overlay itself, since checking a reading against the ink is what happens next;
shut it again and it stays shut until the next one.

The first time Transcribe is pressed, Milah asks before it installs anything:

> Install WSL2 and Kraken to automatically attempt transcriptions?

— naming WSL2 only on a Windows that has not got it. About 3–4 GB is
downloaded, nearly all of it PyTorch. Kraken is a Linux program, so on Windows
it runs inside WSL; on Linux everything is local and there is no subsystem to
install.

Milah brings its own Python rather than using the distribution's. Kraken needs
3.10–3.13, and what a distribution ships is its own affair — Ubuntu's WSL image
has been ahead of that range — so [uv](https://docs.astral.sh/uv/) fetches a
standalone interpreter into Milah's own folder. Nothing is added to the
distribution, nothing needs root, and removal is still the deletion of one
folder. The only thing asked of the distribution is `curl`, to fetch the first
file; if that is missing, Milah opens a console window where you can add it,
since `sudo` wants your Linux password and cannot be answered through a pipe. Installing WSL2 needs administrator rights and a restart: Windows
raises its own elevation prompt, Milah never restarts anything, and after the
restart the next Transcribe carries on where it left off.

Models are added under **File ▸ Handwriting recognition ▸ Manage models…**.
**Add from repository…** lists what Kraken's repository holds in a sortable
table — file, fit, script, language, character error rate, size, date. The
**Size** column is what pressing Download will fetch: the Hebrew bookhand models
are 16 MB apiece, the multilingual base models several hundred.

The **File** column carries the name a model is actually known by. The four
medieval Hebrew bookhand models all summarise themselves as "Medieval Hebrew
manuscripts", and only their files say which is which: `BiblIA_01`,
`Ashkenazi_01`, `Sephardi_01`, `Italian_01`. The filter box searches it, so
typing `bibl` finds BiblIA.

A **Script** box picks what the models should read, opening on
Hebrew, and a **Fit** column says how narrowly each model is aimed at it:
*Dedicated* where it reads that script and no other, *Focused* where it reads a
few, *Multilingual* where it reads many, *Mentioned* where only the summary says
so. A model trained on medieval Hebrew alone therefore outranks a twelve-script
generalist that merely lists Hebrew. Models of equal fit are ordered by whether
they name a matching language, then by their stated error rate, lowest first.

**Kraken's repository holds models for other programs too**, and Kraken cannot
load them. They are left out of the list, on three signals: one was downloaded
once and refused, one names another program in its record, one names another
program in its summary. **Show models Kraken cannot load** brings them back,
greyed, each saying which of the three applies — because a suspicion and a
demonstration are not the same claim.

Milah still asks Kraken to load every model as it arrives, since a record can
declare Kraken and be something else. One that fails is deleted, not left on
disk, and is not offered again. **Manage models…** also says when there are
downloads no installed model uses — from an interrupted download, say — and
offers to clear them out. CER is explained
wherever it appears — including the warning that the figures were each measured
on their own author's manuscripts and are not comparable between models.

**Choose a file…** uses a `.mlmodel` already on the machine, which is the one
route that needs no network. Downloads land inside Milah's own folder, so
removing Kraken removes them too.

**More than one model can be installed.** Which one runs is chosen from the
arrow beside the **Transcribe** button — pressing the button transcribes,
pressing the arrow lists the installed models with the current one ticked — and
the Transcribe tooltip names the model it would use.

**File ▸ Handwriting recognition** keeps the two jobs apart, because they happen
on different schedules: Kraken once per machine, a model whenever a new hand
turns up.

| | |
|---|---|
| **Use model ▸** | The installed models, the running one ticked. |
| **Manage models…** | Add from the repository or from a file, remove, and choose which runs. |
| **Show last recognition…** | What Milah ran, what Kraken said, and the layout file that came back. One run's worth, replaced each time, so a reading that goes wrong can be looked at rather than guessed about. |
| **Install Kraken…** | Only when Kraken is absent. |
| **Remove Kraken…** | Deletes the Python environment and the models, and leaves WSL2 and your Linux distribution alone. |

Removing a single model in **Manage models…** means one of two things, and the
confirmation says which: a model Milah downloaded is deleted and its disk
reclaimed, while a model you chose off your own disk is merely forgotten and the
file left where it is.

**File ▸ Import recognised layout…** reads an ALTO or PAGE file produced
elsewhere — an institution's own eScriptorium export, or a folio processed on
another machine — onto the folio on screen. It needs word-level segmentation:
a file segmented only into lines says so rather than filling the grid with
sentences.

Reading onto a folio that already has text on it asks first.

#### The metadata dock

The right-hand dock records what the folio is, as against what it says, all
optional: **Manuscript**, **Transcriber**, **Origin**, **Library**,
**Shelfmark**, **Date**, **Language**, **Notes**, then **Save details**.

#### Exporting

**File ▸ Export to OSIS** (**Ctrl+E**) writes the transcription out — see
[Exports](#exports) below.

#### Menus and toolbar at a glance

**File:** Open Image, Get Online Manuscript Scan, Open Transcription Project,
Open Recent, Save Transcription project │ Import recognised layout, Handwriting
recognition (Use model, Manage models, Show last recognition, Install Kraken,
Remove Kraken) │ Export to OSIS, Export to Word,
Add to my library, Close Transcription Project │ Quit.
**Edit:** Undo, Redo │ Move verse to new chapter │ Define word in my dictionary.
**Toolbar:** Book, " as " acronym, Chapter, ← →, Magnify, Transcribe, eye.

### Exports

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

### Keyboard shortcuts

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

### The library and your dictionary

Downloaded manuscripts live in a library, so opening one does not mean
remembering where on disk it sits. Milah looks in `$MILAH_MANUSCRIPT_DIR`, then
a folder under the application data directory, then a `manuscripts` folder
beside the executable — which lets a portable copy ship texts that need no
download at all. Browsing for a file still works too, for any OSIS file kept
elsewhere.

Words you accept can carry your own definitions, which the marker shows as
`·D` and which travel with you across projects rather than living in one.
**File ▸ Save my dictionary as…** and **Load a dictionary…** back it up and
restore it.

### Requirements

- Windows 10 or 11.
- Qt 6.6 or newer with Core, Gui, Widgets and Network, built for MinGW.
- The MinGW toolchain, CMake 3.24+ and Ninja. A default Qt installation
  supplies all three under `Tools/`.

ZLIB and QuaZip are built automatically by the build script; nothing else is
needed.

### Build

From the repository root:

```powershell
.\build_milah_windows.ps1
```

The script discovers Qt, MinGW, CMake and Ninja by itself, builds the ZLIB and
QuaZip dependencies under `build\deps\`, builds Milah, and assembles a
self-contained `milah-portable\` folder with `windeployqt`. Nothing needs to be
on `PATH` beforehand.

Qt is looked for under `C:\Qt` first and then `%USERPROFILE%\Qt` — the
system-wide and the per-user location the online installer offers — taking the
newest 64-bit Qt 6 MinGW kit it finds. The MinGW toolchain, CMake and Ninja come from
the `Tools\` directory of whichever root supplied that kit, falling back to the
other root and finally to `PATH`, so an installation split across the two still
builds. `-QtRoot` pins the search to a single directory instead.

Useful switches: `-Configuration Debug`, `-Tests`, `-Clean`, `-SkipPortable`,
`-KeepBuildArtifacts`, `-NoDownload`. Paths can be forced with `-QtBin`,
`-MinGwBin`, `-CMake`, `-Ninja`, `-QuaZipRoot` and `-ZlibRoot`.

To build by hand instead:

```powershell
cmake -S app -B build/milah -G Ninja -DCMAKE_PREFIX_PATH="<qt-kit>;<quazip>;<zlib>"
cmake --build build/milah
```

### Running

Launch `milah-portable\Milah.exe`. OSIS files can also be named on the command
line, which is the quickest way to a populated window:

```powershell
.\milah-portable\Milah.exe path\to\manuscript.osis -t path\to\translation.osis
```

Do not run `Milah.exe` from the build folder unless the Qt runtime DLLs are on
`PATH`; the portable folder exists precisely so that is not necessary.

**A copy of `Milah.exe` without its `data\` folder loses its Strong's numbers**
— the interlinear marker row goes blank and the spelling checks stand down. The
status bar says which directories were searched when that happens.

### Tests

```powershell
.\build_milah_windows.ps1 -Tests
```

Thirteen QTest binaries run under `ctest`, covering OSIS import and note
anchoring, alignment and its scoring, consensus, coverage, the apparatus,
transcription, the manuscript catalogue, word markers, the lexicon, the
spelling suggestions, the grapheme diff, acronyms and the `.milah` round trip.

The comparison logic lives in `app/src/core/` and is built as a `MilahCore`
static library, so the tests exercise it without a display.

Sample OSIS documents live in `app/tests/test_data.cpp` rather than beside the
tests: moc mis-parses raw string literals, and a test file containing one is
silently reported as having no relevant classes.

### Project format

A `.milah` file is a versioned ZIP archive holding `manifest.json`, the source
OSIS files, the Combined state, the per-chapter and per-verse references,
divided words, notes, interlinear wording, translation associations and
alignment corrections, and a current Combined OSIS snapshot. Paths inside
archives are validated before reading or writing.

A `.trscrpt` file is a ZIP archive holding images of scanned manuscripts, 
and the typed transcription, comments and metadata fields. These can be loaded
and saved between sessions until the user is ready to export to OSIS.

Source manuscript and translation text is immutable. Only the Combined text,
the interlinear, your notes and the alignment metadata can be changed.

## Data files

`app/data/` is read from disk at run time rather than compiled in, so a
regenerated lexicon replaces the old one without rebuilding: the Strong's
lexicon, the root index, the abbreviation table, the phrase rules and the
rabbinic word list. What each holds, where it came from and under what licence
is in [`app/data/README.md`](app/data/README.md) — which carries the
attribution CC-BY requires, and which travels with every build.

## Licence

Milah is free software under the [GNU General Public License, version
3](https://www.gnu.org/licenses/gpl-3.0.html) or later. It comes with no
warranty. See [`LICENSE`](LICENSE).

Copyright © 2026 Jonathan D. Müller.

The shipped data carries its own terms: Strong's *Concise Dictionary* is public
domain; morphhb and STEP Bible's TBESH are CC BY 4.0; the rabbinic word list is
drawn from public-domain and CC-BY versions in the Sefaria export. Details are
in [`app/data/README.md`](app/data/README.md).
