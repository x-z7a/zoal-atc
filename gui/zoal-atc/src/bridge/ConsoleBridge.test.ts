import {afterEach, beforeEach, describe, expect, it, vi} from "vitest";

import {ConsoleBridge} from "./ConsoleBridge";
import {FakeSkyscriptHost} from "./fakeHost";
import {EVENT_CHANNEL, READY_CHANNEL, REQUEST_CHANNEL} from "./types";

describe("ConsoleBridge", () => {
  let host: FakeSkyscriptHost;
  let bridge: ConsoleBridge;

  beforeEach(() => {
    host = new FakeSkyscriptHost();
    bridge = new ConsoleBridge(host);
  });

  afterEach(() => {
    bridge.dispose();
    vi.useRealTimers();
  });

  it("reports no host when opened outside X-Plane", () => {
    expect(new ConsoleBridge(undefined).available).toBe(false);
    expect(bridge.available).toBe(true);
  });

  describe("start", () => {
    it("subscribes to events before announcing readiness", async () => {
      await bridge.start();

      // The plugin replays its cached snapshot inside the console.ready
      // handler, so a listener registered afterwards misses the first frame of
      // everything.
      expect(host.listeners.has(EVENT_CHANNEL)).toBe(true);
      expect(host.postedChannels).toEqual([READY_CHANNEL]);
    });

    it("delivers a snapshot replayed during ready", async () => {
      const seen: unknown[] = [];
      host.replayOnReady = [
        {kind: "event", event: "flight_snapshot", payload: {callsign: "GABCD"}},
      ];

      bridge.on("flight_snapshot", (payload) => seen.push(payload));
      await bridge.start();

      expect(seen).toEqual([{callsign: "GABCD"}]);
    });

    it("is idempotent", async () => {
      await bridge.start();
      await bridge.start();

      expect(host.posts.filter((post) => post.channel === READY_CHANNEL)).toHaveLength(1);
    });

    it("rethrows a failed ready post", async () => {
      host.readyError = new Error("host is gone");

      await expect(bridge.start()).rejects.toThrow("host is gone");
    });
  });

  describe("request", () => {
    beforeEach(async () => {
      await bridge.start();
    });

    it("resolves with the payload of the matching response", async () => {
      const pending = bridge.request("flight_plan");
      await Promise.resolve();

      host.respondTo(host.lastRequestId, {hasPlan: true, origin: "CYOW"});

      await expect(pending).resolves.toEqual({hasPlan: true, origin: "CYOW"});
      expect(host.requests[0]).toMatchObject({action: "flight_plan", payload: null});
    });

    it("sends the payload it was given", async () => {
      const pending = bridge.request("tune_radio", {mhz: 118.8, target: "active"});
      await Promise.resolve();
      host.respondTo(host.lastRequestId, null);
      await pending;

      expect(host.requests[0].payload).toEqual({mhz: 118.8, target: "active"});
    });

    // The console's own words are what the pilot sees. An action that silently
    // does nothing is indistinguishable from one that worked.
    it("rejects with the console's reason when the request fails", async () => {
      const pending = bridge.request("tune_radio", {mhz: 999, target: "active"});
      await Promise.resolve();

      host.failRequest(host.lastRequestId, "frequency out of band");

      await expect(pending).rejects.toThrow("frequency out of band");
    });

    it("rejects with the plugin's reason when the request is refused outright", async () => {
      host.refusal = "console disconnected";

      await expect(bridge.request("submit_text", {text: "ready to taxi"})).rejects.toThrow(
        "console disconnected",
      );
    });

    it("throws without a host rather than hanging", async () => {
      await expect(new ConsoleBridge(undefined).request("status")).rejects.toThrow(
        "no Skyscript host",
      );
    });

    // Responses arrive on the channel start() subscribes to, so a request made
    // before then would be accepted and never answered.
    it("refuses a request made before the bridge started", async () => {
      await expect(new ConsoleBridge(new FakeSkyscriptHost()).request("status")).rejects.toThrow(
        "bridge not started",
      );
    });

    it("ignores a response to a request nobody is waiting on", async () => {
      expect(() => host.respondTo(4242, {stale: true})).not.toThrow();
    });

    // The plugin gives up after 10s and says so. This is the backstop for the
    // case it cannot report: an ack that arrived but whose response never will.
    it("gives up when nothing ever answers", async () => {
      vi.useFakeTimers();

      const pending = bridge.request("status");
      const assertion = expect(pending).rejects.toThrow("no answer from the console");
      await vi.advanceTimersByTimeAsync(20000);

      await assertion;
    });

    it("settles everything still waiting when the panel shuts down", async () => {
      const pending = bridge.request("status");
      await Promise.resolve();

      bridge.dispose();

      await expect(pending).rejects.toThrow("panel is shutting down");
    });
  });

  describe("subscriptions", () => {
    // Home and Debug both want telemetry. This used to be one handler per event
    // name, so the second registration silently replaced the first.
    it("delivers one event to every listener", async () => {
      const home: unknown[] = [];
      const debug: unknown[] = [];
      bridge.on("telemetry", (payload) => home.push(payload));
      bridge.on("telemetry", (payload) => debug.push(payload));
      await bridge.start();

      host.emitEvent("telemetry", {altitudeFtMsl: 3000});

      expect(home).toEqual([{altitudeFtMsl: 3000}]);
      expect(debug).toEqual([{altitudeFtMsl: 3000}]);
    });

    it("stops only the listener that unsubscribed", async () => {
      const home: unknown[] = [];
      const debug: unknown[] = [];
      const stopHome = bridge.on("telemetry", (payload) => home.push(payload));
      bridge.on("telemetry", (payload) => debug.push(payload));
      await bridge.start();

      stopHome();
      host.emitEvent("telemetry", {altitudeFtMsl: 3000});

      expect(home).toEqual([]);
      expect(debug).toEqual([{altitudeFtMsl: 3000}]);
    });

    it("survives a listener that unsubscribes itself mid-dispatch", async () => {
      const seen: unknown[] = [];
      const stop = bridge.on("comm_log", () => stop());
      bridge.on("comm_log", (payload) => seen.push(payload));
      await bridge.start();

      expect(() => host.emitEvent("comm_log", {text: "taxi via alpha"})).not.toThrow();
      expect(seen).toEqual([{text: "taxi via alpha"}]);
    });

    // One broken view must not stop the rest of the panel hearing the radio.
    it("keeps delivering after a listener throws", async () => {
      const seen: unknown[] = [];
      bridge.on("comm_log", () => {
        throw new Error("render failed");
      });
      bridge.on("comm_log", (payload) => seen.push(payload));
      await bridge.start();

      host.emitEvent("comm_log", {text: "hold short runway 07"});

      expect(seen).toEqual([{text: "hold short runway 07"}]);
    });

    it("normalizes the status envelope's field names", async () => {
      const seen: unknown[] = [];
      bridge.onStatus((status) => seen.push(status));
      await bridge.start();

      host.emitStatus({connected: true, subscribed: true, pending: 2, droppedEvents: 7});

      expect(seen).toEqual([
        {connected: true, subscribed: true, pending: 2, droppedEvents: 7},
      ]);
    });

    it("stops dispatching once disposed", async () => {
      const seen: unknown[] = [];
      bridge.on("telemetry", (payload) => seen.push(payload));
      await bridge.start();

      bridge.dispose();
      host.emitEvent("telemetry", {altitudeFtMsl: 3000});

      expect(seen).toEqual([]);
    });

    it("ignores an envelope that is not an object", async () => {
      await bridge.start();

      expect(() => {
        for (const listener of host.listeners.get(EVENT_CHANNEL) ?? []) {
          listener(null);
        }
      }).not.toThrow();
    });
  });

  it("posts requests on the request channel", async () => {
    await bridge.start();

    const pending = bridge.request("status");
    await Promise.resolve();

    expect(host.posts.map((post) => post.channel)).toContain(REQUEST_CHANNEL);

    // afterEach disposes the bridge, which settles this. Claim the rejection
    // here so it is not reported as an unhandled one.
    host.respondTo(host.lastRequestId, null);
    await pending;
  });
});
