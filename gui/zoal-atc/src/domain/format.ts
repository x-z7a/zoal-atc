// Turning console payloads into the words on the panel.
//
// Pure functions, no DOM: what a tile says is the part worth testing, and it is
// testable only if it is not tangled up with how it is drawn.

import type {BayView, FlightPlanView, PanelTelemetry} from "../bridge/views";

// The placeholder for "no value yet", not "the value is nothing".
export const UNKNOWN = "—";

// Locale is pinned rather than left to the host. The old panel used a bare
// toLocaleString(), which reads the sim machine's locale and would put a
// thousands separator of "." in front of a pilot in one country and "," in
// another. Aviation English is en-US, and a fixed format is also what makes
// this testable on any CI box.
function thousands(value: number): string {
  return Math.round(value).toLocaleString("en-US");
}

export function formatFrequency(mhz: number | undefined): string {
  return mhz ? mhz.toFixed(3) : UNKNOWN;
}

export function formatStandby(mhz: number | undefined): string {
  return mhz ? `sby ${mhz.toFixed(3)}` : "";
}

export function formatAltitude(telemetry: PanelTelemetry): string {
  return telemetry.onGround ? "on ground" : `${thousands(telemetry.altitudeFtMsl)} ft`;
}

export function formatGroundspeed(knots: number): string {
  return `${Math.round(knots)} kt`;
}

// An absent or stale feed is neither occupied nor clear (phase 21). Showing a
// bare count when surveillance is not current would read as "no traffic", which
// is the one thing the panel must not say when it does not know.
export function formatTraffic(telemetry: PanelTelemetry): string {
  if (telemetry.surveillance === "current") {
    return `${telemetry.trafficCount} contacts`;
  }
  return telemetry.surveillance || "unavailable";
}

export function formatFacilityCount(bay: BayView | undefined): string {
  const count = bay?.count ?? 0;
  return count === 1 ? "1 aircraft" : `${count} aircraft`;
}

export function formatFlightPlanRoute(plan: FlightPlanView | undefined): string {
  if (!plan?.hasPlan) {
    return UNKNOWN;
  }
  return [plan.origin, plan.destination].filter(Boolean).join(" -> ") || "filed";
}

export function formatFlightPlanDetail(plan: FlightPlanView | undefined): string {
  if (!plan?.hasPlan) {
    return "";
  }
  return [
    plan.aircraftIcao || "",
    plan.cruiseAltFt ? `${thousands(plan.cruiseAltFt)} ft` : "",
    plan.firstFix ? `first ${plan.firstFix}` : "",
    plan.route || "",
  ]
    .filter(Boolean)
    .join(" | ");
}

export function formatDroppedEvents(dropped: number): string {
  return dropped > 0 ? `${dropped} updates dropped` : "";
}

// Which seat is talking.
//
// Ground and Tower are different people to a pilot, and telling them apart is
// most of what makes a log readable. The console sends its own identifier for
// the position; this is the only place that turns one into words.
const POSITION_NAMES: Readonly<Record<string, string>> = {
  clearance_delivery: "Clearance Delivery",
  ground: "Ground",
  tower: "Tower",
  departure: "Departure",
  approach: "Approach",
  center: "Center",
  oceanic: "Oceanic",
  ramp: "Ramp",
  ramp_control: "Ramp",
  fbo_or_handler: "FBO",
  unicom_ctaf: "UNICOM",
};

// A position with no seat is not a controller talking, so it reads as plain ATC
// rather than "None".
export function formatPosition(position: string | undefined): string {
  if (!position || position === "none") {
    return "ATC";
  }
  return POSITION_NAMES[position] ?? "ATC";
}

// A short class for colouring one seat differently from another. Positions the
// panel does not know fall back to the generic ATC treatment rather than going
// unstyled.
export function positionTone(position: string | undefined): string {
  if (!position || !(position in POSITION_NAMES)) {
    return "atc";
  }
  switch (position) {
    case "ground":
    case "ramp":
    case "ramp_control":
    case "fbo_or_handler":
      return "surface";
    case "tower":
      return "tower";
    case "clearance_delivery":
      return "delivery";
    case "unicom_ctaf":
      return "advisory";
    default:
      return "radar";
  }
}
