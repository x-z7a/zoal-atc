import {describe, expect, it} from "vitest";

import {ALL_ACTIONS, ALL_LOCAL_ACTIONS} from "./actions";

// The counterpart of the action list the console pins in its own package. That
// side names the strings a console will accept; this one names the strings the
// panel will send. Both exist because a mismatch between them is silent -- the
// console refuses an unknown action, the panel shows nothing happening, and
// nothing fails.
//
// settings and save_settings left this list deliberately and are now in
// LOCAL_ACTIONS: a pilot's SimBrief ID and their notification preference belong
// to the person in the cockpit rather than to a facility, the plugin's config
// file already holds the ID, and answering them locally keeps them working
// while the console is unreachable.
//
// If this test fails, the fix is not to update the literal below. It is to
// check whether the Go list moved too.
describe("the action contract with the console", () => {
  it("sends exactly the actions the console pins", () => {
    expect([...ALL_ACTIONS].sort()).toEqual([
      "comm_log",
      "facility_snapshot",
      "flight_plan",
      "flight_snapshot",
      "refresh_flight_plan",
      "status",
      "submit_text",
      "traffic",
      "tune_radio",
    ]);
  });

  // The plugin checks its local handler before it checks the socket, so a name
  // in both sets would be answered locally and never reach the console -- the
  // console action would simply stop happening, with nothing failing anywhere.
  it("keeps the plugin's local actions out of the console's set", () => {
    const shared = ALL_LOCAL_ACTIONS.filter((action) =>
      (ALL_ACTIONS as readonly string[]).includes(action),
    );
    expect(shared).toEqual([]);
  });
});
