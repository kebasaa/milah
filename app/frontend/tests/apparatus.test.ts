import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import { describe, expect, it } from "vitest";

import { parseOsis } from "../src/core/osis";
import { serializeCombinedOsis } from "../src/core/serialize";
import type { CombinedDraft } from "../src/core/types";

const repoRoot = fileURLToPath(new URL("../../../", import.meta.url));

/**
 * Shaped like the converter's Sloane 273 output: milestoned verses, an incipit
 * title outside any verse, a chapter title, folio boundaries, and notes typed
 * `explanation` rather than the non-standard `footnote`.
 */
const manuscript = `<?xml version="1.0" encoding="UTF-8"?>
<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
  <osisText osisIDWork="Sloane" osisRefWork="bible" xml:lang="he">
    <header><work osisWork="Sloane"><title>Sloane 273</title></work></header>
    <div type="book" osisID="Rev" canonical="true"><div type="introduction" canonical="true"><milestone type="pb" n="1v"/><title type="main" canonical="true">חזון יוחנן<note n="1">An incipit note</note></title></div><chapter sID="Rev.1" osisID="Rev.1" n="1"/><verse sID="Rev.1.1" osisID="Rev.1.1" n="1" subType="x-ms-verse-1"/>ספר <note type="explanation" n="2">A comment</note>הולדת<milestone type="pb" n="2r"/><verse eID="Rev.1.1"/><chapter eID="Rev.1"/><title type="chapter" canonical="true">השער שני</title><chapter sID="Rev.2" osisID="Rev.2" n="2"/><verse sID="Rev.2.1" osisID="Rev.2.1" n="1" subType="x-ms-verse-9"/>דבר<milestone type="x-ms-verse" n="8"/><verse eID="Rev.2.1"/><chapter eID="Rev.2"/></div>
  </osisText>
</osis>`;

const parsed = parseOsis(manuscript, {
  id: "sloane",
  name: "sloane.osis",
  role: "manuscript",
});

describe("non-verse OSIS content", () => {
  it("keeps verses and their inline notes", () => {
    expect(parsed.verses["Rev.1.1"].text).toBe("ספר הולדת");
    expect(parsed.verses["Rev.1.1"].tokens[0].notes[0].text).toBe("A comment");
    expect(Object.keys(parsed.verses)).toEqual(["Rev.1.1", "Rev.2.1"]);
  });

  it("captures titles that belong to no verse", () => {
    expect(parsed.titles.map((title) => title.text))
      .toEqual(["חזון יוחנן", "השער שני"]);
    const [incipit, gate] = parsed.titles;
    expect(incipit.type).toBe("main");
    expect(incipit.canonical).toBe(true);
    expect(incipit.book).toBe("Rev");
    expect(incipit.chapter).toBeNull();
    // A note attached to non-verse text used to be discarded entirely.
    expect(incipit.notes.map((note) => note.text)).toEqual(["An incipit note"]);
    expect(gate.type).toBe("chapter");
    expect(gate.chapter).toBe(2);
  });

  it("captures folio and manuscript-division milestones with their position", () => {
    expect(parsed.milestones.map((m) => [m.type, m.n, m.verseId])).toEqual([
      ["pb", "1v", null],
      ["pb", "2r", "Rev.1.1"],
      ["x-ms-verse", "8", "Rev.2.1"],
    ]);
    expect(parsed.milestones[1].charOffset).toBe("ספר הולדת".length);
  });

  it("reads the manuscript's own verse numbering", () => {
    expect(parsed.verses["Rev.1.1"].altNumber).toBe("1");
    expect(parsed.verses["Rev.2.1"].altNumber).toBe("9");
  });
});

describe("combined edition export", () => {
  const drafts: Record<string, CombinedDraft> = {
    "Rev.1.1": {
      reference: { id: "Rev.1.1", book: "Rev", chapter: 1, verse: "1" },
      columns: [],
      manualText: "ספר הולדת",
    },
  };

  it("round-trips notes, titles and milestones", () => {
    const xml = serializeCombinedOsis(drafts, { workId: "Combined" }, {
      titles: parsed.titles,
      milestones: parsed.milestones.filter((m) => m.verseId === "Rev.1.1"),
      notes: { "Rev.1.1": parsed.verses["Rev.1.1"].tokens[0].notes },
    });
    // The exporter previously emitted no <note> at all, silently dropping
    // every footnote it had read.
    expect(xml).toContain("<note");
    expect(xml).toContain("A comment");
    expect(xml).toContain('<milestone type="pb" n="2r"/>');
    expect(xml).toContain("חזון יוחנן");

    const round = parseOsis(xml, {
      id: "round",
      name: "round.osis",
      role: "combined",
    });
    expect(round.verses["Rev.1.1"].text).toBe("ספר הולדת");
    expect(round.verses["Rev.1.1"].tokens[0].notes[0].text).toBe("A comment");
    expect(round.titles.map((title) => title.text)).toContain("חזון יוחנן");
    expect(round.milestones.map((m) => m.n)).toContain("2r");
  });

  it("escapes apostrophes in text", () => {
    const xml = serializeCombinedOsis({
      "Rev.1.1": {
        reference: { id: "Rev.1.1", book: "Rev", chapter: 1, verse: "1" },
        columns: [],
        manualText: "She'ol & <hope>",
      },
    });
    expect(xml).toContain("She&apos;ol &amp; &lt;hope&gt;");
  });
});

/**
 * The converter's own output, so the two sides of the pipeline are checked
 * against each other rather than only against a hand-written sample.
 */
describe("generated Sloane 273 OSIS", () => {
  const variants = [
    "hebrew", "hebrew_commented", "translation", "hebrew_consonantal",
  ];
  const pathFor = (variant: string) =>
    `${repoRoot}data/01_osis/REV_Sloane237_${variant}.osis`;
  const available = variants.every((variant) => existsSync(pathFor(variant)));
  const maybe = available ? it : it.skip;

  maybe.each(variants)("loads %s without warnings", (variant) => {
    const document = parseOsis(readFileSync(pathFor(variant), "utf8"), {
      id: variant,
      name: variant,
      role: "manuscript",
    });
    expect(Object.keys(document.verses)).toHaveLength(33);
    expect(document.warnings).toEqual([]);
  });

  maybe("keeps the manuscript's non-verse text", () => {
    const document = parseOsis(readFileSync(pathFor("hebrew"), "utf8"), {
      id: "hebrew",
      name: "hebrew",
      role: "manuscript",
    });
    const incipit = document.titles.find(
      (title) => title.canonical && title.type === "main",
    );
    expect(incipit?.text).toContain("חֲזוֹן יוֹחָנָן הַקֹּדֶשׁ");
    expect(document.titles.find((t) => t.type === "chapter")?.text)
      .toBe("הַשַּׁעַר שֵׁנִי");
    expect(document.milestones.filter((m) => m.type === "pb").map((m) => m.n))
      .toEqual(["1r", "1v", "2r", "2v", "3r", "3v", "4r", "4v"]);
    // Revelation 1:18 was dropped entirely by the previous extraction.
    expect(document.verses["Rev.1.18"].text).toContain("וְהָחָי וְהָיִיתִי מֵת");
    expect(document.verses["Rev.1.15"].altNumber).toBe("14");
  });
});
