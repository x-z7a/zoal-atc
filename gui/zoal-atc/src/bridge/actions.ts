// Every action name the panel sends, in one place.
//
// The console pins the same list from its side in
// console/internal/gui/gui_test.go (TestEveryActionThePanelSendsIsAccepted),
// because a rename on either side fails closed and silently: the request is
// simply refused, the panel shows nothing happening, and no test fails because
// the path is never taken.
//
// So there are two lists, deliberately, and they are meant to be diffed by eye
// in review. Changing one without the other is the mistake both are here to
// catch.

export const ACTIONS = {
  // Reads.
  flightSnapshot: "flight_snapshot",
  commLog: "comm_log",
  traffic: "traffic",
  flightPlan: "flight_plan",
  facilitySnapshot: "facility_snapshot",
  status: "status",
  settings: "settings",
  debugTail: "debug_tail",
  // Writes.
  tuneRadio: "tune_radio",
  submitText: "submit_text",
  refreshFlightPlan: "refresh_flight_plan",
  saveSettings: "save_settings",
} as const;

export type ActionName = (typeof ACTIONS)[keyof typeof ACTIONS];

// The same set as a flat list, for the contract test.
export const ALL_ACTIONS: readonly ActionName[] = Object.values(ACTIONS);
