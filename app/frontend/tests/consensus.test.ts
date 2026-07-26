import { describe, expect, it } from "vitest";

import {
  alignVerse,
  generateCombined,
} from "../src/core/alignment";
import { parseOsis } from "../src/core/osis";

function witness(id: string, text: string) {
  return parseOsis(
    `<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
      <osisText osisIDWork="${id}" osisRefWork="bible">
        <header><work osisWork="${id}"><title>${id}</title></work></header>
        <div type="book" osisID="Matt"><chapter osisID="Matt.1">
          <verse osisID="Matt.1.1">${text}</verse>
        </chapter></div>
      </osisText>
    </osis>`,
    { id, name: `${id}.osis`, role: "manuscript" },
  );
}

describe("Combined consensus", () => {
  it("uses a strict majority and the priority witness for a tie", () => {
    const majority = [witness("a", "ספר"), witness("b", "סֵפֶר"), witness("c", "דבר")];
    const majorityDraft = generateCombined(
      alignVerse("Matt.1.1", majority, "a"),
      majority,
      "a",
    );
    expect(majorityDraft.columns[0].text).toBe("ספר");
    expect(majorityDraft.columns[0].needsReview).toBe(false);

    const tie = [witness("a", "ספר"), witness("b", "דבר")];
    const tieDraft = generateCombined(
      alignVerse("Matt.1.1", tie, "b"),
      tie,
      "b",
    );
    expect(tieDraft.columns[0].text).toBe("דבר");
    expect(tieDraft.columns[0].needsReview).toBe(true);
  });
});
