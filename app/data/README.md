# Data files

These are **read from disk at run time, not compiled into `Milah.exe`**, so a
regenerated lexicon or an edited rule table replaces the old one without
rebuilding the application.

## Where Milah looks

In order, first readable copy wins:

1. `$MILAH_DATA_DIR` — set it to point at a freshly generated file
2. `data/` under the user's application data directory — a personal copy
3. `data/` beside the executable — what ships with the application

So dropping a newer `hebrew_lexicon.json` into your application data directory
overrides the installed one, and the installed one can be replaced in place.

The build stages these files next to the binaries, and
`build_milah_windows.ps1` copies them into the portable folder. **A copy of
`Milah.exe` without its `data/` directory loses its Strong's numbers**: the
interlinear row goes blank, the spelling check switches off rather than
flagging every word, and the status bar says which directories were searched.


## hebrew_lexicon.json

The Strong's numbers shown under the Combined row, and the word list the
spelling checks consult. Generated — do not edit by hand:

```bash
python python/tools/build_lexicon.py \
    --strongs <downloads>/strongs-hebrew-dictionary.js \
    --wlc     <downloads>/wlc \
    --tbesh   <downloads>/tbesh.txt \
    --out     app/data/hebrew_lexicon.json
```

The three inputs are downloaded rather than kept in this repository:

- `strongs-hebrew-dictionary.js` —
  <https://github.com/openscriptures/strongs> (`hebrew/`)
- `wlc/*.xml` — <https://github.com/openscriptures/morphhb> (`wlc/`, 39 files)
- `tbesh.txt` — <https://github.com/STEPBible/STEPBible-Data> (`Lexicons/`,
  "TBESH - Translators Brief lexicon of Extended Strongs for Hebrew")

### Sources and licences

**A Concise Dictionary of the Words in the Hebrew Bible** (James Strong, 1890),
in the OpenScriptures machine-readable edition. Public domain.

**Westminster Leningrad Codex with morphology**, from the OpenScriptures
[morphhb](https://github.com/openscriptures/morphhb) project, used to index
which inflected forms each Strong's number appears as. Licensed
**CC BY 4.0** — <https://creativecommons.org/licenses/by/4.0/> — which requires
attribution wherever this data is redistributed, including in builds of Milah
that embed it.

**TBESH — Translators Brief lexicon of Extended Strongs for Hebrew**, an
abridged Brown-Driver-Briggs keyed to extended Strong's numbers, from
**[STEP Bible](https://www.STEPBible.org)**. Licensed **CC BY 4.0**, and
STEPBible asks to be credited as "STEP Bible" with a link to
www.STEPBible.org — so that credit has to travel with any build that embeds
this file.

TBESH's `eStrong#` column is zero-padded and often carries a homonym letter, so
several of its entries can share one plain Strong's number: H1 covers both
"father" and a proper name. Those are merged in file order and only the leading
senses are kept, because this feeds a tooltip rather than a reference work.

### Not used, but worth knowing about

**TAHOT** (same repository and licence) could replace morphhb as the form
source and would bring disambiguated Strong's numbers, sharpening the ambiguity
marker. Not done: morphhb already resolves 14 of 15 words in a real verse, so
the swap is churn for a small gain.

**A Mishnah corpus** from [Sefaria-Export](https://github.com/Sefaria/Sefaria-Export)
is the agreed next addition. It would supply attested forms in the 1st–3rd
century register the manuscripts inhabit, which is what the unknown-word check
currently lacks — see the coverage note below. Sefaria licenses each text
separately, so that needs checking before use.

### What it covers, and what it does not

The form index is built from the Hebrew Bible, so it covers biblical Hebrew. A
Hebrew New Testament also contains proper nouns, loanwords and later coinages
that are not in the Tanakh at all. A word the index does not recognise means
"not attested in the Hebrew Bible" — not "misspelled". Nothing is guessed:
unmatched words are reported as unmatched.

Where a form is ambiguous — the consonantal skeleton of several different
words — the candidates are ranked by how often each reading occurs in the
Westminster text, and all of them are kept so the alternatives stay visible.
