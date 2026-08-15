// The wire between the page and the plugin.
//
// The panel never talks to the console directly. It posts to the plugin over
// Skyscript's message bridge, and the plugin proxies over the WebSocket that
// already authenticated and identified this flight. Credentials therefore stay
// out of the page, and a GUI action is attributed to the socket rather than to
// anything the page could write into a payload.
//
// A console.request resolves to a *receipt*, not an answer: the plugin's
// Skyscript handler runs on the X-Plane main thread and must never wait on a
// socket. The answer arrives later on console.event, correlated by request id.

export type ConsoleStatus = {
  connected: boolean;
  subscribed: boolean;
  pending: number;
  droppedEvents: number;
};

export type ResponseEnvelope = {
  kind: "response";
  request_id: number;
  ok: boolean;
  payload: unknown;
  error: string;
};

export type EventEnvelope = {
  kind: "event";
  event: string;
  payload: unknown;
};

export type StatusEnvelope = {
  kind: "status";
  connected: boolean;
  subscribed: boolean;
  pending: number;
  dropped_events: number;
};

export type ConsoleEnvelope = ResponseEnvelope | EventEnvelope | StatusEnvelope;

export type RequestAck = {
  accepted: boolean;
  request_id: number;
  error: string;
};

// The object Skyscript injects into the page.
export type SkyscriptHost = {
  postMessage(channel: string, payload: unknown): Promise<unknown>;
  onMessage(channel: string, callback: (payload: unknown) => void): void;
};

export const REQUEST_CHANNEL = "console.request";
export const READY_CHANNEL = "console.ready";
export const EVENT_CHANNEL = "console.event";
export const PANEL_LOG_PREFIX = "[zoal-atc panel]";

// The events the console publishes, from console/internal/gui/gui.go. Names are
// a wire contract: a rename on either side fails closed and silently, so they
// live in one place on this side too.
export const EVENTS = {
  flightSnapshot: "flight_snapshot",
  facilitySnapshot: "facility_snapshot",
  commLog: "comm_log",
  transcript: "transcript",
  atcReply: "atc_reply",
  traffic: "traffic",
  status: "status",
  telemetry: "telemetry",
  flightPlan: "flight_plan",
} as const;

export type ConsoleEventName = (typeof EVENTS)[keyof typeof EVENTS];
