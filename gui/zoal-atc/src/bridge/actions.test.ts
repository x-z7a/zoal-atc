import {describe, expect, it} from "vitest";

import {ALL_ACTIONS} from "./actions";

// The counterpart of TestEveryActionThePanelSendsIsAccepted in
// console/internal/gui/gui_test.go. That test pins the strings the console will
// accept; this one pins the strings the panel will send. Both exist because a
// mismatch between them is silent -- the console refuses an unknown action, the
// panel shows nothing happening, and nothing fails.
//
// If this test fails, the fix is not to update the literal below. It is to
// check whether the Go list moved too.
describe("the action contract with the console", () => {
  it("sends exactly the actions the console pins", () => {
    expect([...ALL_ACTIONS].sort()).toEqual([
      "comm_log",
      "debug_tail",
      "facility_snapshot",
      "flight_plan",
      "flight_snapshot",
      "refresh_flight_plan",
      "save_settings",
      "settings",
      "status",
      "submit_text",
      "traffic",
      "tune_radio",
    ]);
  });
});
