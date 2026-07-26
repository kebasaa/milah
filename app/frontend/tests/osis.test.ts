import { describe, expect, it } from "vitest";

import { parseOsis } from "../src/core/osis";

const osis = `<?xml version="1.0" encoding="UTF-8"?>
<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
  <osisText osisIDWork="Test" osisRefWork="bible" xml:lang="he">
    <header><work osisWork="Test"><title>Test witness</title></work></header>
    <div type="book" osisID="Matt"><chapter osisID="Matt.1">
      <verse osisID="Matt.1.1">ספר <note n="1">A comment</note>הולדת</verse>
    </chapter></div>
  </osisText>
</osis>`;

describe("OSIS import", () => {
  it("reads Hebrew and anchors an inline note while rejecting DTDs", () => {
    const document = parseOsis(osis, {
      id: "witness-a",
      name: "test.osis",
      role: "manuscript",
    });
    expect(document.verses["Matt.1.1"].text).toBe("ספר הולדת");
    expect(document.verses["Matt.1.1"].tokens[0].notes[0].text)
      .toBe("A comment");
    expect(() => parseOsis(
      osis.replace(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
        "<!DOCTYPE osis [<!ENTITY xxe SYSTEM \"file:///secret\">]>",
      ),
      { id: "unsafe", name: "unsafe.osis", role: "manuscript" },
    )).toThrow(/DTD or ENTITY/);
  });
});
