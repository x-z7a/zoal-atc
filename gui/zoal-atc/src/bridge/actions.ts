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
  debugTail: "debug_tail",
  // Writes.
  tuneRadio: "tune_radio",
  submitText: "submit_text",
  refreshFlightPlan: "refresh_flight_plan",
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
// down; these are the ones that have to work then.
export const LOCAL_ACTIONS = {
  connectionSettings: "connection_settings",
  saveConnectionSettings: "save_connection_settings",
  // The pilot's own preferences. They moved here from ACTIONS deliberately: a
  // SimBrief ID and how somebody wants to be notified belong to the person in
  // the cockpit, not to whichever facility they are talking to today. The
  // plugin's config file already holds the ID and already sends it on the
  // hello, so a copy on a console would be a second answer to a question that
  // has one -- and the two would drift the first time this pilot flew
  // somewhere else. Answering locally also means they keep working while the
  // console is unreachable, which is when settings get fiddled with.
  settings: "settings",
  saveSettings: "save_settings",
} as const;

export type LocalActionName = (typeof LOCAL_ACTIONS)[keyof typeof LOCAL_ACTIONS];

export const ALL_LOCAL_ACTIONS: readonly LocalActionName[] =
  Object.values(LOCAL_ACTIONS);
