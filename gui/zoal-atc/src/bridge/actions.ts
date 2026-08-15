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

// Actions the *plugin* answers by itself. These never reach the console, so
// they are deliberately not in ACTIONS above and must never be added to the Go
// list: the console has no business writing a file in the X-Plane install, and
// listing them there would assert it accepts something it should refuse.
//
// They exist because the connection settings are how a pilot repairs a console
// they cannot reach. Every action in ACTIONS is refused while the socket is
// down; these two are the ones that have to work then.
export const LOCAL_ACTIONS = {
  connectionSettings: "connection_settings",
  saveConnectionSettings: "save_connection_settings",
} as const;

export type LocalActionName = (typeof LOCAL_ACTIONS)[keyof typeof LOCAL_ACTIONS];

export const ALL_LOCAL_ACTIONS: readonly LocalActionName[] =
  Object.values(LOCAL_ACTIONS);
