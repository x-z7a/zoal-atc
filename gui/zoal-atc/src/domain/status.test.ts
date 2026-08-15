import {describe, expect, it} from "vitest";

import type {ConsoleStatus} from "../bridge/types";
import {
  CONSOLE_UNREACHABLE_MESSAGE,
  describeStatus,
  freshnessOf,
  isConsoleReachable,
  TELEMETRY_STALE_MS,
} from "./status";

function status(overrides: Partial<ConsoleStatus> = {}): ConsoleStatus {
  return {connected: false, subscribed: false, pending: 0, droppedEvents: 0, ...overrides};
}

describe("describeStatus", () => {
  it.each([
    {name: "down", value: status(), expected: "disconnected"},
    {
      name: "connected but not yet subscribed",
      value: status({connected: true}),
      expected: "subscribing",
    },
    {
      name: "working",
      value: status({connected: true, subscribed: true}),
      expected: "connected",
    },
    {
      name: "working with requests outstanding",
      value: status({connected: true, subscribed: true, pending: 2}),
      expected: "connected (2 in flight)",
    },
  ])("describes a console that is $name", ({value, expected}) => {
    expect(describeStatus(value)).toBe(expected);
  });
});

describe("isConsoleReachable", () => {
  // Connected but not subscribed still means no events are arriving, so the
  // numbers on screen are last-known either way.
  it("requires both connected and subscribed", () => {
    expect(isConsoleReachable(status({connected: true, subscribed: true}))).toBe(true);
    expect(isConsoleReachable(status({connected: true, subscribed: false}))).toBe(false);
    expect(isConsoleReachable(status({connected: false, subscribed: true}))).toBe(false);
  });
});

describe("freshnessOf", () => {
  const now = 1_000_000;

  it("is fresh while telemetry keeps arriving", () => {
    expect(
      freshnessOf({reachable: true, paused: false, lastTelemetryAt: now - 500, now}),
    ).toEqual({kind: "fresh", message: ""});
  });

  it("goes stale once telemetry stops", () => {
    const result = freshnessOf({
      reachable: true,
      paused: false,
      lastTelemetryAt: now - 12000,
      now,
    });

    expect(result.kind).toBe("stale");
    expect(result.message).toBe("no telemetry for 12s");
  });

  it("is still fresh right up to the threshold", () => {
    expect(
      freshnessOf({
        reachable: true,
        paused: false,
        lastTelemetryAt: now - (TELEMETRY_STALE_MS - 1),
        now,
      }).kind,
    ).toBe("fresh");
  });

  // A paused sim is not a broken feed, and telling a pilot their telemetry died
  // when they pressed pause is a false alarm.
  it("says paused rather than stale when the sim is paused", () => {
    expect(
      freshnessOf({reachable: true, paused: true, lastTelemetryAt: now - 60000, now}),
    ).toEqual({kind: "paused", message: "paused"});
  });

  // The two silences, and the one that outranks. A dead console means nobody is
  // listening to the radio; "no telemetry for 12s" would describe the symptom
  // and hide the cause.
  it("reports an unreachable console ahead of stale telemetry", () => {
    const result = freshnessOf({
      reachable: false,
      paused: false,
      lastTelemetryAt: now - 60000,
      now,
    });

    expect(result.kind).toBe("unreachable");
    expect(result.message).toBe(CONSOLE_UNREACHABLE_MESSAGE);
  });

  it("reports an unreachable console even while paused", () => {
    expect(
      freshnessOf({reachable: false, paused: true, lastTelemetryAt: now, now}).kind,
    ).toBe("unreachable");
  });

  it("does not claim staleness before the first frame ever arrives", () => {
    expect(
      freshnessOf({reachable: true, paused: false, lastTelemetryAt: undefined, now}),
    ).toEqual({kind: "fresh", message: ""});
  });
});
