// What the panel says about itself, and how much of it to believe.
//
// Two silences are possible and the pilot has to tell them apart. A dead
// console means nobody is listening to the radio. Stale telemetry means the
// numbers stopped arriving. Everything on screen is last-known during the
// first, which is why it dims the whole panel rather than one line.

import type {ConsoleStatus} from "../bridge/types";

// Telemetry is published about once a second (gui.TelemetryPublishInterval), so
// silence well past that means this aircraft's picture has stopped arriving.
export const TELEMETRY_STALE_MS = 4000;

// describe turns the bridge's status into the one line a pilot needs: whether
// what they are reading is current, and if not, why not.
export function describeStatus(status: ConsoleStatus): string {
  if (!status.connected) {
    return "disconnected";
  }
  if (!status.subscribed) {
    return "subscribing";
  }
  return status.pending > 0 ? `connected (${status.pending} in flight)` : "connected";
}

// The console is reachable only when it is both connected and subscribed.
// Connected-but-not-subscribed still means no events are arriving.
export function isConsoleReachable(status: ConsoleStatus): boolean {
  return status.connected && status.subscribed;
}

export type Freshness =
  | {kind: "fresh"; message: ""}
  | {kind: "paused"; message: "paused"}
  | {kind: "stale"; message: string}
  | {kind: "unreachable"; message: string};

export const CONSOLE_UNREACHABLE_MESSAGE = "console unreachable - values are last known";

// freshnessOf answers, in one place, which of the two silences this is.
//
// An unreachable console outranks stale telemetry: when nobody is listening to
// the radio, saying "no telemetry for 12s" describes a symptom and hides the
// cause.
export function freshnessOf(args: {
  reachable: boolean;
  paused: boolean;
  lastTelemetryAt: number | undefined;
  now: number;
  staleAfterMs?: number;
}): Freshness {
  if (!args.reachable) {
    return {kind: "unreachable", message: CONSOLE_UNREACHABLE_MESSAGE};
  }
  if (args.paused) {
    return {kind: "paused", message: "paused"};
  }
  if (args.lastTelemetryAt === undefined) {
    return {kind: "fresh", message: ""};
  }

  const staleAfterMs = args.staleAfterMs ?? TELEMETRY_STALE_MS;
  const ageMs = args.now - args.lastTelemetryAt;
  if (ageMs < staleAfterMs) {
    return {kind: "fresh", message: ""};
  }
  return {kind: "stale", message: `no telemetry for ${Math.round(ageMs / 1000)}s`};
}
