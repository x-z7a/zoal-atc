// The payloads the console publishes, as the page sees them.
//
// These mirror the Go view structs in console/internal/app (views.go,
// app_gui.go, app_bay.go). Field names are JSON tags, so they are a wire
// contract like the action names: a mismatch renders blank rather than failing.
// Everything is optional-tolerant because Go's omitempty means a zero value
// simply is not sent.

export type PanelTelemetry = {
  frequencyMhz?: number;
  standbyMhz?: number;
  altitudeFtMsl: number;
  heightAglFt: number;
  groundspeedKts: number;
  headingDeg: number;
  onGround: boolean;
  airport?: string;
  paused?: boolean;
  // An absent feed is neither occupied nor clear (phase 21), so the panel shows
  // this word rather than a count that would read as "no traffic".
  surveillance?: string;
  trafficCount: number;
};

export type CommLogEntry = {
  kind?: string; // "pilot" | "atc"
  sessionId?: string;
  // A party-line copy: heard on the frequency but addressed to someone else.
  overheard?: boolean;
  addressedTo?: string;
  text?: string;
  category?: string;
  generation?: number;
  delivered?: boolean;
  at?: string;
  // The controller position that transmitted ("ground", "tower", …). Empty on a
  // pilot entry, and on an ATC entry from a console that predates the field.
  spokenBy?: string;
};

export type FlightPlanView = {
  hasPlan?: boolean;
  origin?: string;
  destination?: string;
  cruiseAltFt?: number;
  aircraftIcao?: string;
  route?: string;
  firstFix?: string;
  importedAt?: string;
};

// One aircraft's durable preferences. Flight-scoped: the console works a
// facility, so two connected pilots each keep their own.
export type SettingsView = {
  simbriefPilotId?: string;
  notifications?: NotificationsSetting;
};

// What the *plugin* holds, not the console: the credential and address of the
// socket everything else here travels over. Answered locally, so it is readable
// and writable while the console is unreachable.
export type ConnectionView = {
  // The bearer token sent on the upgrade. Empty means the endpoint is open.
  token?: string;
  // Read-only. Shown so the pilot can see which console the token is for.
  url?: string;
};

// How this pilot wants to be told ATC spoke while the panel is closed. The
// plugin raises the toast, so these are pushed to it rather than acted on here.
export type NotificationsSetting = {
  enabled: boolean;
  corner: string;
  timeoutSecs: number;
  sound: boolean;
};

export const DEFAULT_NOTIFICATIONS: NotificationsSetting = {
  enabled: true,
  corner: "top_right",
  timeoutSecs: 8,
  sound: true,
};

export type SessionView = {
  hasSession?: boolean;
  sessionId?: string;
  callsign?: string;
  lifecycle?: string;
  service?: string;
  comms?: string;
  clearance?: string;
  flightRules?: string;
  operation?: string;
  identity?: string;
  risk?: string;
  abnormal?: string;
  coordination?: string;
  conduct?: string;
  readbackPending?: boolean;
  // Any non-zero value means the rest of the flight is running on a hand-forced
  // state vector -- worth showing, so a later bug report is read in that light.
  lifecycleOverrides?: number;
  abnormalActive?: boolean;
  riskActive?: boolean;
};

export type BayRow = {
  flightId: string;
  callsign?: string;
  lifecycle?: string;
  service?: string;
  airport?: string;
  // The field whose positions own this aircraft, which is not the same as
  // airport: that one is whatever own-ship is nearest to, and for most of a
  // flight it names somewhere the aircraft is only passing over.
  facility?: string;
  frequencyMhz?: number;
  onGround?: boolean;
  altitudeFt?: number;
  heldRunway?: string;
  readbackPending?: boolean;
  awaitingDelivery?: boolean;
  telemetryAgeSeconds?: number;
  hasTelemetry?: boolean;
  paused?: boolean;
  active?: boolean;
};

// One field being worked, and how many aircraft are at it.
export type FacilityBay = {
  facility: string;
  count?: number;
};

export type BayView = {
  flights?: BayRow[];
  count?: number;
  // The fields being worked. Two aircraft at different airports are two
  // organisations that share nothing, and a single flat bay showed them as one
  // controller's problem.
  facilities?: FacilityBay[];
};

// One record from the console's structured debug log.
export type DebugRecordView = {
  ts?: string;
  category?: string;
  fields?: Record<string, unknown>;
};

export type TranscriptView = {
  sessionId?: string;
  sequence?: number;
  text?: string;
  source?: string;
  error?: string;
  durationMs?: number;
  receivedAt?: string;
};

export type ATCReplyView = {
  sessionId?: string;
  category?: string;
  spokenBy?: string;
  text?: string;
  speak?: boolean;
  generation?: number;
  sent?: boolean;
  error?: string;
  sentAt?: string;
};
