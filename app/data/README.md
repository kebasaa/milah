# Bundled data

## hebrew_lexicon.json

The Strong's numbers shown under the Combined row, and the word list the
spelling checks consult. Generated — do not edit by hand:

```bash
python python/tools/build_lexicon.py \
    --strongs <downloads>/strongs-hebrew-dictionary.js \
    --wlc     <downloads>/wlc \
    --out     app/data/hebrew_lexicon.json
```

The two inputs are downloaded rather than kept in this repository:

- `strongs-hebrew-dictionary.js` —
  <https://github.com/openscriptures/strongs> (`hebrew/`)
- `wlc/*.xml` — <https://github.com/openscriptures/morphhb> (`wlc/`, 39 files)

### Sources and licences

**A Concise Dictionary of the Words in the Hebrew Bible** (James Strong, 1890),
in the OpenScriptures machine-readable edition. Public domain.

**Westminster Leningrad Codex with morphology**, from the OpenScriptures
[morphhb](https://github.com/openscriptures/morphhb) project, used to index
which inflected forms each Strong's number appears as. Licensed
**CC BY 4.0** — <https://creativecommons.org/licenses/by/4.0/> — which requires
attribution wherever this data is redistributed, including in builds of Milah
that embed it.

### What it covers, and what it does not

The form index is built from the Hebrew Bible, so it covers biblical Hebrew. A
Hebrew New Testament also contains proper nouns, loanwords and later coinages
that are not in the Tanakh at all. A word the index does not recognise means
"not attested in the Hebrew Bible" — not "misspelled". Nothing is guessed:
unmatched words are reported as unmatched.

Where a form is ambiguous — the consonantal skeleton of several different
words — the candidates are ranked by how often each reading occurs in the
Westminster text, and all of them are kept so the alternatives stay visible.
