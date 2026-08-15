import {useCallback, useEffect, useState, useSyncExternalStore} from "react";

import type {ConsoleStatus} from "../bridge/types";
import type {CommLogEntry, PanelTelemetry} from "../bridge/views";
import {EVENTS} from "../bridge/types";
import {
  freshnessOf,
  isConsoleReachable,
  type Freshness,
} from "../domain/status";
import {useConsole} from "./ConsoleProvider";

// The store keeps the last payload of each event and replaces it rather than
// mutating it, so useSyncExternalStore can compare snapshots by identity: a
// component reading an event that did not change does not re-render.

export function useConsoleEvent<T>(event: string): T | undefined {
  const {store} = useConsole();
  return useSyncExternalStore(store.subscribe, () => store.getEvent(event)) as T | undefined;
}

export function useCommLog(): readonly CommLogEntry[] {
  const {store} = useConsole();
  return useSyncExternalStore(store.subscribe, store.getCommLog);
}

export function useConsoleStatus(): ConsoleStatus {
  const {store} = useConsole();
  return useSyncExternalStore(store.subscribe, store.getStatus);
}

export type ActionRunner = {
  run: (action: string, payload?: unknown) => Promise<unknown>;
  error: string;
  clearError: () => void;
  busy: boolean;
};

// Runs an action and keeps the console's answer if it refused.
//
// The panel is a command source, never the decider: it sends, and shows
// whatever came back. An action that silently does nothing is
// indistinguishable from one that worked.
export function useConsoleAction(): ActionRunner {
  const {bridge} = useConsole();
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const run = useCallback(
    async (action: string, payload?: unknown): Promise<unknown> => {
      if (!bridge) {
        setError("not connected to the console");
        throw new Error("not connected to the console");
      }
      setError("");
      setBusy(true);
      try {
        return await bridge.request(action, payload);
      } catch (caught) {
        setError(caught instanceof Error ? caught.message : String(caught));
        throw caught;
      } finally {
        setBusy(false);
      }
    },
    [bridge],
  );

  const clearError = useCallback(() => setError(""), []);

  return {run, error, clearError, busy};
}

// Which silence this is, kept current by a one-second tick.
//
// Telemetry itself arrives about once a second, so the tick adds no render the
// feed was not already causing; what it buys is that the panel goes stale on
// its own when the feed stops, rather than sitting on a number that stopped
// being true.
export function useTelemetryFreshness(): Freshness {
  const telemetry = useConsoleEvent<PanelTelemetry>(EVENTS.telemetry);
  const status = useConsoleStatus();
  const [lastTelemetryAt, setLastTelemetryAt] = useState<number | undefined>(undefined);
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    if (telemetry) {
      setLastTelemetryAt(Date.now());
      setNow(Date.now());
    }
  }, [telemetry]);

  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(timer);
  }, []);

  return freshnessOf({
    reachable: isConsoleReachable(status),
    paused: telemetry?.paused === true,
    lastTelemetryAt,
    now,
  });
}
