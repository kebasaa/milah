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

The generators below live in a sibling repository, `hebrew_manuscripts`,
checked out beside this one (`../hebrew_manuscripts` relative to this repo's
root) — the commands assume that layout and are run from this repo's root.

## hebrew_lexicon.json

The Strong's numbers shown under the Combined row, and the word list the
spelling checks consult. Generated — do not edit by hand:

```bash
python ../hebrew_manuscripts/tools/python/tools/build_lexicon.py \
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

## hebrew_roots.json

Strong's numbers to the root they derive from, so the alignment can see that
two differently spelt words belong to one root. Generated — do not edit by
hand, and note it reads the **already-built lexicon** rather than any download,
so it runs in under a second:

```bash
python ../hebrew_manuscripts/tools/python/tools/build_roots.py \
    --lexicon app/data/hebrew_lexicon.json \
    --output  app/data/hebrew_roots.json
```

Kept out of `hebrew_lexicon.json` for two reasons: that file is six megabytes on
a single line, so regenerating it to change a heuristic means a six-megabyte
diff every time; and its own generator needs three external corpora this
repository does not carry, while this one needs nothing.

The source is Strong's `derivation` prose — `"from H4427 (מָלַךְ);"` — read with
a regular expression, which is why it is deliberately timid. Only the first
clause is used, and only when it names exactly one number; `"a primitive root"`
means the entry *is* one; chains are walked at most two steps rather than to a
fixed point; and a root gathering more than forty members is dropped whole.

That timidity is not fussiness. אָדָם "man" derives from אָדַם "to be red" and
יְהוּדָה from יָדָה "to praise" — real etymology, useless for deciding whether two
manuscripts are reading the same word. Every extra step collapses more distant
words into one bucket, and the aligner will happily put a bucket in one column.

**Honest note on its value.** On the Revelation corpus this rung decides
**zero** columns: every pair it would join already shares a Strong's number, and
the stronger rung takes them first. It is carried for coverage as more witnesses
arrive, not on measured benefit. `alignment_test` prints a per-rung tally, so
the claim can be rechecked whenever the corpus grows.

Optional: absent the file, `HebrewLexicon::hasRoots()` is false and the rung
stands down without changing any other answer.

## hebrew_abbreviations.json

What a scribal abbreviation may stand for. Edited **by hand** — there is no
generator, because the readings are editorial judgements about these particular
manuscripts.

A word counts as an abbreviation because it carries one of the marks scribes
write them with. Milah accepts the punctuation geresh U+05F3 and gershayim
U+05F4 **and the whole Hebrew accent block U+0591–U+05AF**, because in these
manuscripts the accents are not accents — these are late translations, not
copies of the Masoretic text, and no witness carries cantillation at all. What
they carry is a mark over an abbreviation, and copyists are not consistent:

| witness | marks used on a standalone `ה` |
|---|---|
| REV Cochin | U+059E (59×) |
| MAT Cochin | U+05F3 (8×), U+059E (1×); U+059D elsewhere |
| JAS Cochin | U+0594 (3×) |
| REV Sloane 237 | none — and it has no accent anywhere in the file |

So `יש׳ו`, `יש״ו` and `יש֞ו` are one entry, and `stem` holds only the letters.
Naming the marks one at a time means missing the next one a scribe reaches for.

This cannot live in `hebrew_phrase_rules.json`: that file's `match` entries are
put through `comparisonKey()`, which strips exactly those marks, so a rule for
`ה֞` would collapse to `ה` and fire on every definite article in the corpus.

Prefixed spellings share one row — `לה֞` and `וה֞` peel their prefix letter,
look up `ה`, and get it back on the front of each expansion.

### The unmarked lone `ה`

Fifteen standalone `ה` in the corpus carry no mark at all, and they are not the
same thing in every witness:

- **James (Cochin), 10×** — the divine name with the mark left off:
  `עבד ה`, `לפני ה האב`, `באם שירצה ה`, `קרוב ה לכל קראיו`.
- **Revelation (Sloane), 2×** — a detached definite article belonging to the
  next word: `מְנוֹרוֹת ה הַזָּהָב`, `מָה ה הָרוּחַ אֹמֶרֶת`.

No mechanical rule separates them; "a lone `ה` before a ה-word is an article"
fails on James' own `לפני ה האב`. So a single-letter stem is **also matched
without a mark**, and the editor decides — the suggestion says outright that it
may instead be a detached article.

That applies to the **suggestions only**. The collation never expands an
unmarked token, because guessing wrong there is silent: Sloane's two articles
would be scored against `אלהים` and pulled into the divine name's column with
nothing on screen to say so. `AbbreviationTable::expansionsFor` is the alignment's
path and stays mark-gated; `unmarkedExpansionsFor` is the editor's.

Longer stems are never matched unmarked. An unmarked `ישו` is the bare form the
phrase rules already speak to, and an unmarked `עי` is not a word.

### Pointing

Expansions are written **pointed**, and reduced to suit the verse they go into:
a Combined verse with no vowel points gets `אלהים`, a pointed one `אֱלֹהִים`, from
this one entry. The same applies to `hebrew_phrase_rules.json`, so `יֵשׁוּעַ`
is offered as `ישוע` in an unpointed edition.

Write expansions pointed and let Milah reduce them — points can be stripped
afterwards but not invented. The verse's convention is decided by a majority of
its words that are long enough to show one, counting **niqqud only**: the
abbreviation marks above live in the accent block, so counting those would read
an unpointed Cochin verse as pointed and do exactly the wrong thing.

Numeric abbreviations (`א׳` for one, `י׳ב` for twelve) are deliberately absent:
they expand to numbers rather than words. They pass through untouched.

## hebrew_names.json

Which spellings are the same proper name — the only table Milah has that is an
assertion rather than a computation.

These manuscripts write New Testament names as Greek transliterations where
other witnesses write the Hebrew. Cochin's `יאהנניס` is Sloane's `יוֹחָנָן`, and
**nothing derives that**: four edits over a seven-letter word, no Strong's
number, no shared consonantal skeleton. No amount of loosening the near-match
threshold would reach it without also pairing largely unrelated words
throughout the corpus. So somebody who knows has to say so, and this is where.

```json
{ "id": "john", "prefer": "יוֹחָנָן", "forms": ["יאהנניס", "יהאנניס", "יאנניס"],
  "note": "John. Cochin writes the Greek Iōannēs three ways…" }
```

`prefer` is a member of its own group, so a witness reading the Hebrew name
needs no separate entry in `forms`. It is written **pointed** and reduced for an
unpointed edition, exactly as an abbreviation expansion is.

### Prefix peeling

One or two letters of `ובכלמהש` come off, so `ולעפהיזוס` reaches `עפהיזוס` and
`לסמירנון` reaches `סמירנון`. **Shortest peel first, and that is load-bearing**:
`ולאדיצאן` has to resolve as `ו` + `לאדיצאן` because Laodicea begins with a
lamed of its own, while Sloane's `לאודיקיאה` has to resolve whole. Any other
order gets one of the two wrong.

A glued particle is **not** peeled — `אל` is not a prefix letter, and it is
`god`, `to`, `not` and `these` depending on pointing. Sloane's `ואלסמרנה` is
listed literally as `אלסמרנה` instead, and the `ו` comes off that. Words Sloane
runs together entirely (`אנייוחנן` for "I John") are a tokenisation problem and
out of scope.

A prefix taken off a folded key is bare consonants, so it is glued back onto a
pointed name: `ולעפהיזוס` offers `ואֶפֶסוֹס`, where the vav wants a shewa. Cosmetic,
and the abbreviation table already does the same with `לאֱלֹהִים`.

### Keep it small

A group outranks a shared Strong's number in the alignment (`kScoreName` = 42
against `kScoreStrongs` = 36), so a wrong equivalence beats the dictionary
silently. What holds that in check is that the table is ten curated entries of
exotic transliterations, that an exact agreement still wins at 48, and that the
alignment test's rung tally makes every column the table decided countable in
one diff. **If this ever grows past a page, the rung needs re-thinking rather
than more entries.**

`יהושע` is both Yeshua and the biblical Joshua. Grouping it under `jesus` is
right for Revelation and would be wrong for a book quoting Joshua son of Nun;
groups are corpus-wide and have no way to say "here it means Joshua".

The five `ישו` rules that used to live in `hebrew_phrase_rules.json` are here
now. A phrase rule has no peeling, so each of `ל`, `ו`, `ב` and `כ` needed a
rule of its own; one entry here covers them all.

Entries marked `"unconfirmed": true` carry a preferred spelling proposed rather
than settled — they go into the edition when accepted, so read them first. The
field is documentation; Milah does not read it.

## rabbinic.words.txt

The vocabulary the unknown-word check accepts beyond the Hebrew Bible.
Generated — but plain text, so words may be **appended by hand**; blank lines
and `#` comments are ignored:

```bash
python ../hebrew_manuscripts/tools/python/tools/build_wordlist.py --out app/data/rabbinic.words.txt
```

Any file named `*.words.txt` in a data directory is read and merged, so your
own list can sit beside this one without touching it or rebuilding Milah.

### Why it exists

The lexicon indexes the Hebrew Bible. A Hebrew New Testament is full of words
the Tanakh has not — `יֵשׁוּעַ`, `יוֹחָנָן`, and ordinary post-biblical vocabulary —
so on its own the check flags legitimate text. The Mishnah and Tosefta are the
1st–3rd century register those manuscripts inhabit.

A word being in this list means it is **attested**, not that it has a Strong's
number. Strong's covers only the Hebrew Bible, so such words show **`M`** in
the interlinear row rather than a number or a dash.

### Sources and licences

Hebrew texts of the **Mishnah** and **Tosefta** from the
[Sefaria export](https://github.com/Sefaria/Sefaria-Export). Sefaria licenses
per *version*, and some are CC-BY-NC — its plain-text API serves one such
version by default. The generator therefore reads the export bucket, where the
version is a path segment and each file states its own licence, and **refuses
anything outside Public Domain, CC0 and CC-BY**. The versions actually used,
and any refused, are listed in the header of the generated file.

Commentaries are excluded. Sefaria files them under the text they comment on,
so `json/Mishnah/` holds five times more Bartenura and Tosafot Yom Tov than
Mishnah; that is medieval Hebrew, and admitting it would make the list so
permissive it stopped catching anything.

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
