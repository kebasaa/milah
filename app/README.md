# Milah — application source

Milah is documented at the repository root: see [`../README.md`](../README.md)
for what it does, how to build it, how to run the tests, and what a `.milah`
project holds. This file is a signpost so that nothing is described twice and
left to drift.

Two things here have documentation of their own:

- [`data/README.md`](data/README.md) — the lexicons, word lists and rule tables
  Milah reads from disk at run time, with their sources and licences.
- [`../tools/README.md`](../tools/README.md) — the PDF-to-OSIS converter, and
  the generators that build those data files.

Briefly, the layout: `src/core/` holds the comparison and transcription logic
and is built as the `MilahCore` static library, so the tests exercise it
without a display. `src/app_controller.*` and `src/transcription_controller.*`
hold the two editing sessions; `src/main_window.*` and `src/ui/` are the
interface; `tests/` are the QTest binaries `ctest` runs.
