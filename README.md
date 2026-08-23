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

**The window title names the file the tab you are on has open** —
`JAS_MSOo132.trscrpt — Milah` — and marks it while there is unsaved work in
either job, since the window is what would take it away. A transcription you
have not saved yet reads `Untitled`. Several transcriptions of one codex look
identical from outside, and this is what tells them apart without opening one.

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
one, **Line break after this word** — which says the manuscript's line ends
there, for training a recogniser, and reaches no export — and **Delete word**,
which takes that word off the folio and leaves the verse and the words around it
where they are. A recogniser that read one word too many has not made the verse
wrong. Ctrl+Z puts any of them back.

Right-clicking the **folio** rather than the text offers to fill it from a
published transcription — either continuing the one the last folio used, or
starting a new one where you clicked. See below.

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

Beside the eye is a switch between **word boxes** and **line boxes**. Word boxes
are what it opens with, and they answer where each reading landed. Line boxes
answer the other question: what the recogniser thought a *line* was. It draws
one box per line — the segmenter's own outline where the folio has it, the
rectangle round the line's words where it does not — with the line's number and
the baseline the training strip is cut along faint underneath, and **the line's
whole reading written underneath the ink it was laid onto**, nothing behind it,
so the words and the writing can be read against each other.

This is worth looking at because the line, not the word, is what a recogniser
trains on: it cuts a strip by straightening the ink along that baseline and
masking away everything outside that outline. On a hand it was not trained for,
the grouping is where it goes wrong — two lines of the manuscript run into one
detection, or one line cut into two — and none of that shows in the word boxes,
because each box looks right on its own.

Reading the words against the ink is also how you find out that the line is the
wrong *length*. A line takes as many words as the recogniser drew boxes on it,
and on a rapid cursive that count is not the word count — it splits one word into
two boxes as readily as it runs two words into one, and either way every word
below is one place out for the rest of the leaf. So click a word, in the reading
or on its box, and say so:

| | |
|---|---|
| **Space** | You have read this line against the ink and it says what the scribe wrote. Marks every word of it looked at, and moves to the next line that still needs reading. |
| **Enter** | The line ends **before** this word. The rest of it moves down, and the lines below are laid again. |
| **Backspace** | With the first word of a line selected, pulls it up onto the line above — one word a press. |
| **← →** | Back and forward along the reading, in reading order. A line too crowded to write out in full is still walked this way. |
| **↑ ↓** | To the line above or below, without accepting anything. |
| **Escape** | Drop the selection. |

The caret beside the selected word shows which side the break falls on. What no
longer fits at the foot of the leaf is not lost: the folio's recorded end point
moves back with it, so the next folio's **Continue from the previous
transcription** begins exactly there, and Milah says how many words went. Ctrl+Z
takes a break and the whole re-flow back in one press.

**Space is the loop.** A line is worth nothing to the training set until every
word on it has been looked at, and reading a line is one decision rather than
twenty — so read it, press Space, and the selection moves to the next line that
still needs it. The number beside each line says how far off it is: `21·3` is
line twenty-one with three words nobody has read. Space leaves the **margin
alone**, deliberately: a note beside the text is the one place the recogniser is
both likely wrong and unhelped by the poured transcription, so vouching for the
line does not vouch for a note nobody has transcribed. Such a line trains trimmed
to what is vouched for, or not at all, until the note is dealt with on its own.

What a line holds is remembered on the folio, so a re-flow started somewhere
above it does not throw the answer away — and it outlives the fill that prompted
it, because how many words the scribe wrote on a line is a fact about the leaf
rather than about which transcription was poured onto it.

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
| **See what training will be shown…** | This folio's finished lines, cut the way training cuts them, with the ground truth under each and the lines Kraken refuses beside them. Grey until a line has been checked all the way through. |
| **Save this folio for HTR training** | This folio's checked lines, into its manuscript's training set, with the library's largest scan, and a word on what of it a model will actually see. Grey until a line has been checked all the way through. |
| **Train a model…** | Teaches a model the hand, from what has been saved. Grey until a set holds 50 lines. |
| **Install Kraken…** | Only when Kraken is absent. |
| **Remove Kraken…** | Deletes the Python environment and the models, and leaves WSL2 and your Linux distribution alone. |

Removing a single model in **Manage models…** means one of two things, and the
confirmation says which: a model Milah downloaded is deleted and its disk
reclaimed, while a model you chose off your own disk is merely forgotten and the
file left where it is.

#### When Kraken cannot read the hand

No model has seen every hand, and the gap shows. Reading two of these manuscripts
with the same model, counting how many of the words it produced are forms the
Hebrew Bible actually uses:

| | words read | forms the Hebrew Bible uses |
|---|---|---|
| Gaster 1616, a square bookhand | 144 | 87 (**60%**) |
| MS Oo.1.32, a Cochin cursive | 241 | 100 (41%) |

The first reads as Hebrew. The second does not — its 41% is short accidental
strings. BiblIA and the other Hebrew models are trained on **bookhands**:
Ashkenazi, Byzantine, Italian, Oriental, Sephardi, Yemenite. A rapid cursive is a
different letter-form system, and no amount of resolution closes that: the same
folio read at 1024 px and at its 3948 px master gives 41% and 38%.

**Resolution does not change the boxes either**, which is the part that matters
when the text comes from a published transcription rather than from the model.
Measured on 158r of MS Oo.1.32:

| | lines | boxes on text lines |
|---|---|---|
| 1024 px | 42 | 493 |
| 1491×2000 px, the largest single request Cambridge allows | 41 | 505 |

Two per cent more boxes for a fetch nearly half again as large. Asking for
`/full/2000,/` does not even give 2000 across: the service caps height as well,
so it returns 1491×2000.

**And the box counts are not the problem.** On the lines that carry James,
Kraken's segmentation is good: the first line of James on that leaf is thirteen
words, and Kraken returns thirteen boxes for it. What throws a pour off is
narrower and easier to miss — see the maqqef below.

**The way through is to teach the model the hand**, which is what Kraken's own
documentation recommends and what a transcriber correcting a folio is already
doing the work for. Milah runs it, in two steps.

**The line a model is shown is the one the segmenter drew.** Kraken cuts a
training strip by dewarping the line along its baseline and masking everything
outside its boundary, so those two decide what the model learns from. Kraken's
own ALTO gives both — a sloping, multi-point baseline and a boundary of eighty
or more points that follows the ink — and Milah keeps them now. It used to read
neither, and rebuild both from the word boxes: a level line through their
middles, and a rectangle round the outside.

Measured on 158r of MS Oo.1.32, that substitution was costly. The rectangle
overlapped its neighbouring lines by **45.7%** of its own area against the
polygon's 20.9%, taking in 3.2 times as much ink belonging to other lines —
nearly half of every strip was the line above or below. And a baseline there
falls a median of 12 pixels across a leaf whose letters are some 30 tall, so a
level one sheared the strip by a third of a letter's height.

A folio read before Milah kept them still falls back to the invention, since a
line without a baseline is one Kraken skips in silence. **Recover the machine's
readings** re-reads such a folio for its geometry and its original readings
alone, leaving the text untouched — Transcribe would recover them too and
replace the text doing it, which on a folio already filled from a published
transcription trades the larger thing for the smaller.

**File ▸ Handwriting recognition ▸ Save this folio for HTR training** puts what
you have corrected into that manuscript's training set. One folio at a time,
because that is how anybody works: the set accumulates between sessions and
between files, so Matthew and James of one manuscript feed one set. Saving a
folio again replaces it rather than adding beside it — going back over a folio
makes a better statement of the same lines, not a second folio.

**The picture saved is the largest the library holds, not the one on screen.**
Milah shows a folio at 1024 px, which a recogniser reads just as well — that was
measured. But training cuts every line out and shows it to the model again and
again, and a line stretched from 56 px to the 120 px the model wants is invented
detail. Libraries cap a single request well below their masters (Cambridge holds
3948 × 5295 and answers at most 2000 × 2000), so the folio is fetched as a grid
of regions and put back together — six pieces for Cambridge, twelve for
Manchester. Where that cannot be done, the picture on screen is used and the
message says so rather than leaving you to wonder later.

**Only lines where every word has been checked are saved.** A word the recogniser
read and nobody has looked at is the machine's own guess, and training on it
teaches the model the mistakes it already makes. A line also needs every word to
have come from the recogniser — a word typed by hand has no place on the picture,
so its line cannot be cut out and is left out instead.


#### Seeing what the model will be shown

A recogniser learns from neither the folio nor the word boxes. It learns from
**one picture per line**, cut by straightening the ink along that line's baseline
and zeroing everything outside its outline. So every question worth asking about
a training set is a question about those pictures — is this one straight, does it
hold one line of writing or the bottom of the line above, has the mask taken in a
descender from somewhere else — and until now not one of them could be answered.
The geometry could only be argued about.

**File ▸ Handwriting recognition ▸ See what training will be shown…** cuts them
and shows them, with the text the model is told each one says. It calls Kraken's
own `extract_polygons` — the function `ketos compile` calls — rather than
redrawing the idea in Milah, because a preview of what Milah *believes* training
does is the one thing this must not be. The strips are cut from the same picture
and the same layout the save would use, and the window says which picture that
turned out to be.

It also shows the refusals, and those are the other silence. Kraken skips a line
it cannot cut and carries on with a log warning; `ketos compile` drops a line
whose text is empty. So a transcriber can save sixty lines, train on forty-five,
and never be told. Here a refused line keeps its place in the list and says what
was wrong with it — *Baseline length below minimum 5px*, *Line polygon outside of
image bounds*.

**Saving asks the same question**, at the moment it matters most. Once the lines
are written into the set, Milah cuts them and tells you what a model will
actually see — "all of them cut cleanly", or which ones will not be and why. Only
where Kraken is installed to ask: the saving does not depend on it and has
already happened, so this adds a sentence to the answer or nothing at all.

A refused line stays in the set and is still counted there. Taking it back out
would mean running this cut on every save whether or not Kraken is there; saying
so does not, and it is what tells you whether a set of eighty is really eighty.

**File ▸ Handwriting recognition ▸ Train a model…** opens once a set holds 50
lines, about two folios. Kraken keeps a tenth of the data back to measure the
model against, so fewer leaves nothing to measure with; five folios is where it
starts to tell. The window lists the sets, and **more than one can be ticked**,
because a scribe outlives a shelfmark — MS Oo.1.32 and Oo.1.16 are one hand in
two bindings, and a model shown both sees more of it than a model shown either.

Training runs on the processor and takes hours; the window says so before it
starts, keeps a log and an elapsed clock, and Stop leaves the model you are using
untouched. What comes out is checked by being loaded, exactly as a downloaded
model is, and then joins the model list under whatever name you gave it. **Name
it after the hand rather than the manuscript** — it is offered on every
manuscript you open, which is the point of having trained it.

Then do it again: five folios corrected, train, transcribe five more with the
result, correct those. Each round starts from a better reading than the last.

#### When a transcription already exists

Several of these manuscripts have been transcribed and published — Matthew and
James of MS Oo.1.32 among them, in `hebrew_manuscripts/manuscripts/`. Typing 250
words of a cursive to make training data out of a folio whose text is already
written down is work nobody should do twice.

**Right-click the folio where the transcription begins.** That is the only way
in, and deliberately: where a published text starts on a leaf is a thing you
point at, and a menu entry could only ever offer the same job with the pointing
left out. It works on a folio nothing has read — there are no boxes to name a
line with then, so what Milah keeps is **the place you clicked**, and it turns
that into a line once it has read the folio and has lines to choose from.

The menu offers up to two things, and the difference between them is **which
file**:

| | |
|---|---|
| **Continue from the previous transcription (Jas 1:25)…** | the file the last filled folio used, at the verse and word it stopped on |
| **Fill from a new transcription file starting here…** | choose an `.osis`; its beginning goes on the line you clicked |

The first folio of a book has nothing to continue, so only the second appears.
**Which of the two it is, is always asked and never guessed** — from the document
a new book beginning mid-leaf looks exactly like a continuation, so guessing
wrong lays down the wrong text, which is a bug this had.

**Continuing shows no window at all.** The file, the book, the chapter, the verse,
the word to start on and the line to start on are all settled before it runs —
the first five by the folio before it, the last by where you clicked. There is
nothing to ask, so nothing is asked: **the folio reads itself if nothing has read
it**, the text goes in, and the status line says what was laid down. The only
thing that appears is the recogniser's own progress bar, which is a two-minute
job reporting itself rather than a question, and it can be cancelled.
**Ctrl+Z puts the folio back exactly as it was**, which is what makes an action
with no confirmation safe.

**Starting a new transcription is where the choosing lives.** The window opens at
once, whatever state the folio is in, and the file chooser with it. Then book,
chapter and verse, and the button says **Read the folio and fill…**: choosing is
the part that needs a person and needs no recognition whatever, so the minute of
reading is spent after the deciding rather than in front of it. The window stays
open while it reads, and the lines appear in it. However poorly Kraken reads a
hand, **where it found the lines is the part being used**.

**The lines are the fixed thing and the words are laid into them.** Each line is
given a number of words and its boxes are cut up or joined so that every word
gets one — including the words Kraken never found, which on a cursive is many of
them. A box holding three words comes out as three columns of it; a spare box
joins the word before it. The cut is an even division and is meant to be read as
a guess: a box drawn round two words does not record where the space between
them fell. It costs the training nothing, because what training reads is the
line's own extent.

**A word joined at a maqqef is one word.** `בכל־דרכיו` is one word on the leaf,
in one box, so it is poured as one. Milah's tokeniser splits it deliberately —
for collation, where a compound has to line up against a witness that writes two
words — but a fill is not collation, and pouring it as two lays an extra word on
the line and puts **everything below it one place late for the rest of the
folio**. That was the whole of the drift on 158r: read word by word against the
source, the pour agreed for 95 words and then diverged exactly once, there.

**A verse number is a word.** Oo.1.32 writes its verse numbers into the running
text as Arabic digits — `2:`, `3:` … `20:` — so the segmenter finds a box for
each, and a pour that walked past them laid the verse's first word onto the
numeral's box and put everything after it one place out for the rest of the leaf.
The same fault as the maqqef, in a third disguise. So each verse opens with its
own number, taken from the OSIS `n=` attribute.

Two exceptions, both read off the leaves themselves. **Verse 1 carries no
numeral**: James opens `יעקב עבד ה` with nothing before it, and after the `פרק`
heading on 159r the next chapter opens the same way — a number tells a verse
apart from the one before it, and the first verse of a chapter has nothing to be
told apart from. And **a verse the edition prints empty gets none either**, since
a number on the leaf for a verse that is not on it takes a box from the verse
that is; this edition prints Jas 1:21 with no text because the manuscript has
none.

The colon written beside the digit is not poured. The OSIS does not record it —
a verse's text begins at its first word — and inventing a character for ground
truth is worse than leaving a box to type into.

**The OSIS says nothing about folios** — its elements are verse, chapter, note
and div, with no page mark of any kind — so where on the leaf the text begins is
the one thing you have to supply. Milah tried to work it out, sliding the
recognised words along the book and scoring them, and got one folio of three
right with the correct one's margin no better than the wrong ones'. At 41% noise
there is not enough in a reading to place it. Measured, then dropped rather than
shipped as a guess.

So the click is the answer. A click between two lines means the **lower** one,
because "starts here" means from here on; a click in the margin beside a line
means that line. The same thing can be said inside the window afterwards — select
the line and press **The text starts on this line** — which is how you correct it
once you can see where the pour landed.

**A new transcription starts at its own beginning.** Choose an OSIS and the first
words of it go on the line you pointed at. Nothing moves that but the four boxes
in the window, which say the book, the chapter, the first verse, and how many
words of that verse to skip. Pointing at a line and being answered with mid-book
text — because the folio before had stopped there — is a bug this had, and the
fix was to make carrying on something you ask for by name.

A book beginning halfway down its first leaf is the ordinary case, not the
exception: **the lines above are left exactly as they are**, so a leaf opening
with the tail of the previous book keeps it, and they go on showing what the
recogniser read so you can find your place against the picture. Then walk down,
and where a line is a word over or a word short, **one word more** / **one word
fewer** re-flows everything below it. About 25 decisions per folio instead of 250
typed words.

**A book runs on across the leaves.** Where the next leaf picks up is **read off
the previous one** — its last verse, and how many words of that verse it holds
right now — rather than taken from a note written when it was filled. That
matters because filling is only the start: you then walk the folio and correct
it, and deleting a word the recogniser invented changes what the leaf holds. A
number written before that correction cannot follow it, and the next folio would
resume a word late for the rest of the book. Counting what is actually there
moves with every edit.

It looks back to the **nearest** earlier folio with text on it, so a verso left
blank or a leaf skipped for later does not send the next one back two places in
the book. **The transcription also remembers which `.osis` it was filled from**,
so continuing needs no file dialog. The path is kept and the file is not — the
text belongs to whoever published it, and a copy carried inside the project would
go stale the moment the edition was corrected — so a transcription opened on
another machine, or one written before Milah remembered the source, asks **once**
and never again.

**Chapters come across with the text.** A leaf running from Jas 1:25 into chapter
2 numbers those verses 2:1, 2:2 — the chapter break is written where the source
has it, and the folio records which chapter it opens in.

Nothing is marked checked by filling. A machine put those words there — a better
machine than the recogniser, but a machine — and that is exactly what unchecked
means. Walk the folio, confirm the lines, and then save it for training.

**Where a word came out wrong**, right-click its box on the folio and choose
**Edit this word…**. A small field opens **over the box**, right-to-left and
prefilled, with the ink you are reading it against directly underneath — Enter
keeps it, Escape abandons it, clicking away keeps it. It commits by the same
path the text grid does, so a word corrected on the picture and one corrected in
the text mean the same thing, including that correcting it is what checks it.

**Where the recogniser got a line wrong**, right-click it on the folio. The
segmenter fails in two directions and there is an entry for each:

- **The line ends after “…”** — it ran two lines of the manuscript together, and
  the first of them stops at the word you clicked. The detection is exported as
  two lines, each with its own strip clipped to its own half.
- **Join with the line below** — it cut one line of the manuscript in two. The
  words of the line below come up onto this one and the whole line is laid out
  again, which is what puts two side-by-side pieces back into a single
  right-to-left run. The two baselines are joined; the two outlines are given up
  for the rectangle round the joined line's words, since two separate rings
  cannot honestly be made into one.
- **This whole line is not part of the transcribed text** — every word of it
  held out at once. A margin note or a running header usually has a line to
  itself, and it is the same mark as the one below, made once instead of nine
  times.

Both repairs are offered in either view, because the fault is usually noticed in
the word boxes — where the poured text stops matching the ink — and understood
in the line boxes. Neither reaches the exports of the *text*: OSIS and Word
carry a work, not a page. What they change is the training data, and the same
right-click still carries **Line break after this word** from the text grid,
which is the same mark under an older name.

### Marginalia, which the published text does not have

A recogniser segments every mark with ink in it. A published transcription holds
the *work*, and normally no marginalia at all. So a note in the margin is one
more box on the leaf, and a fill treats it as one more slot in the running text —
a word of James lands on it and **every word after it shifts by one for the rest
of the folio**. The note is destroyed and the line breaks go wrong, which for
training data is worse than losing the note.

**Right-click the box on the folio** (or the word in the text) and mark **Not
part of the transcribed text**. **The box greys out** — a dotted grey outline
with a faint wash over it, plainly not the dashed outline of a word merely
waiting to be checked — **the box goes back to what the recogniser read there**,
and the words below it move up one place —
the word of the work that was poured onto the note is given back to the passage,
and one more word is drawn from the transcription at the foot of the leaf. Lines
above the note are untouched, so anything you corrected up there is safe; if
you have already checked anything below it, Milah asks before replacing it.
Ctrl+Z takes the mark, the reading and the whole re-flow back in one press.

The reading stays legible under the grey, dimmed rather than hidden: it is what
you type over to turn the note into training data.

**A folio filled before Milah recorded where its text began cannot re-flow**, and
says so in the status line — the box is still held out, and filling the folio
again gives it back the ability. The same goes for an edition that has moved
since: the mark stands either way, because it is a statement about the leaf
rather than about the fill.

From then on:

- **a fill steps over it.** The line takes one word fewer and the boxes are cut
  to fit what remains, so the note keeps its place and the text flows past it.
  The words are woven back into reading order, so a note in the middle of a line
  stays in the middle of it;
- **the exports carry it as a note, not as a word.** It leaves the running text
  and its reading joins the note on the last word of its line — which is where a
  marginal gloss actually attaches — and rides out as an OSIS note. A note the
  recogniser gave a line of its own, which is most of them, attaches to the line
  above;
- **training uses it once you have read it.** Its ink is in the picture, so the
  ground truth has to account for it. Type what it says and check it, and the
  line goes out whole with the note in place — a model that learns marginalia
  beats one taught to ignore letters it can plainly see. Leave it unchecked and
  it is trimmed off the end of its line instead, polygon and text together;
  a line with an unread note stuck in the *middle* is left out altogether, since
  there is no way to cut it that does not leave the ink unaccounted for.

The word keeps its reading and stays editable throughout. Marking it says where
it belongs, not that it is worthless.

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
recognition (Use model, Manage models, Show last recognition, See what training
will be shown, Save this folio for HTR training, Train a model, Install Kraken,
Remove Kraken) │ Export to OSIS,
Export to Word,
Add to my library, Close Transcription Project │ Quit.
**Edit:** Undo, Redo │ Move verse to new chapter │ Define word in my dictionary.
**Toolbar:** Book, " as " acronym, Chapter, ← →, Magnify, Transcribe, eye,
word boxes / line boxes.

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
