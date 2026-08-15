import {beforeEach, describe, expect, it} from "vitest";

import {ConsoleBridge} from "../bridge/ConsoleBridge";
import {FakeSkyscriptHost} from "../bridge/fakeHost";
import {COMM_LOG_LIMIT, ConsoleStore} from "./ConsoleStore";

describe("ConsoleStore", () => {
  let store: ConsoleStore;

  beforeEach(() => {
    store = new ConsoleStore();
  });

  // The reason this class exists: a tab mounted after an event still needs the
  // value. facility_snapshot only fires when an aircraft connects, so without
  // this a Debug tab opened mid-flight shows an empty bay indefinitely.
  it("hands a late reader the value it missed", () => {
    store.setEvent("facility_snapshot", {count: 2});

    expect(store.getEvent("facility_snapshot")).toEqual({count: 2});
  });

  it("returns undefined for an event that has not arrived", () => {
    expect(store.getEvent("telemetry")).toBeUndefined();
  });

  it("starts disconnected rather than claiming a connection it has not seen", () => {
    expect(store.getStatus()).toEqual({
      connected: false,
      subscribed: false,
      pending: 0,
      droppedEvents: 0,
    });
  });

  describe("snapshot identity", () => {
    // useSyncExternalStore compares snapshots by identity and re-renders when
    // they differ. A slice that did not change has to keep its reference or
    // every component re-renders on every event.
    it("keeps the same reference for an untouched event", () => {
      store.setEvent("telemetry", {altitudeFtMsl: 3000});
      const first = store.getEvent("telemetry");

      store.setEvent("flight_snapshot", {callsign: "GABCD"});

      expect(store.getEvent("telemetry")).toBe(first);
    });

    it("keeps the same comm log reference when another event arrives", () => {
      store.setEvent("comm_log", {kind: "atc", text: "taxi via alpha"});
      const first = store.getCommLog();

      store.setEvent("telemetry", {altitudeFtMsl: 3000});

      expect(store.getCommLog()).toBe(first);
    });

    it("replaces the comm log reference when an entry arrives", () => {
      store.setEvent("comm_log", {kind: "atc", text: "taxi via alpha"});
      const first = store.getCommLog();

      store.setEvent("comm_log", {kind: "pilot", text: "taxi via alpha"});

      expect(store.getCommLog()).not.toBe(first);
    });
  });

  describe("comm log", () => {
    it("keeps entries in the order they were heard", () => {
      store.setEvent("comm_log", {kind: "atc", text: "first"});
      store.setEvent("comm_log", {kind: "pilot", text: "second"});

      expect(store.getCommLog().map((entry) => entry.text)).toEqual(["first", "second"]);
    });

    it("preserves the party-line marking", () => {
      store.setEvent("comm_log", {
        kind: "atc",
        text: "cleared to land",
        overheard: true,
        addressedTo: "GWXYZ",
      });

      expect(store.getCommLog()[0]).toMatchObject({overheard: true, addressedTo: "GWXYZ"});
    });

    // A panel left open for a long flight must not grow without bound inside
    // the sim's process.
    it("keeps only the most recent entries", () => {
      for (let i = 0; i < COMM_LOG_LIMIT + 25; i++) {
        store.setEvent("comm_log", {kind: "atc", text: `entry ${i}`});
      }

      const log = store.getCommLog();
      expect(log).toHaveLength(COMM_LOG_LIMIT);
      expect(log[0].text).toBe("entry 25");
      expect(log[log.length - 1].text).toBe(`entry ${COMM_LOG_LIMIT + 24}`);
    });

    it("ignores an empty entry rather than logging a blank line", () => {
      store.setEvent("comm_log", null);

      expect(store.getCommLog()).toHaveLength(0);
    });
  });

  describe("subscribers", () => {
    it("notifies on an event and on a status change", () => {
      let notifications = 0;
      store.subscribe(() => notifications++);

      store.setEvent("telemetry", {altitudeFtMsl: 3000});
      store.setStatus({connected: true, subscribed: true, pending: 0, droppedEvents: 0});

      expect(notifications).toBe(2);
    });

    it("stops notifying once unsubscribed", () => {
      let notifications = 0;
      const stop = store.subscribe(() => notifications++);

      stop();
      store.setEvent("telemetry", {altitudeFtMsl: 3000});

      expect(notifications).toBe(0);
    });
  });

  describe("attach", () => {
    it("fills from the bridge's events and status", async () => {
      const host = new FakeSkyscriptHost();
      const bridge = new ConsoleBridge(host);
      store.attach(bridge);
      await bridge.start();

      host.emitEvent("telemetry", {altitudeFtMsl: 3000, trafficCount: 0});
      host.emitEvent("comm_log", {kind: "atc", text: "hold short runway 07"});
      host.emitStatus({connected: true, subscribed: true, pending: 1, droppedEvents: 2});

      expect(store.getEvent("telemetry")).toMatchObject({altitudeFtMsl: 3000});
      expect(store.getCommLog()).toHaveLength(1);
      expect(store.getStatus()).toEqual({
        connected: true,
        subscribed: true,
        pending: 1,
        droppedEvents: 2,
      });

      bridge.dispose();
    });

    it("stops filling once detached", async () => {
      const host = new FakeSkyscriptHost();
      const bridge = new ConsoleBridge(host);
      const detach = store.attach(bridge);
      await bridge.start();

      detach();
      host.emitEvent("telemetry", {altitudeFtMsl: 3000});

      expect(store.getEvent("telemetry")).toBeUndefined();

      bridge.dispose();
    });
  });
});
