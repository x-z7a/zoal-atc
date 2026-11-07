import {useEffect, useLayoutEffect, useRef, useState, type RefObject} from "react";

import {ACTIONS, LOCAL_ACTIONS} from "../bridge/actions";
import {EVENTS, HUD_LAYOUT_CHANNEL} from "../bridge/types";
import type {SessionView} from "../bridge/views";
import {CommBubble} from "../components/CommLog";
import {ErrorText} from "../components/ErrorText";
import {isFading, liveHudLines, nextHudLines, type HudLine} from "../domain/hud";
import {isConsoleReachable} from "../domain/status";
import {useConsole} from "../state/ConsoleProvider";
import {useCommLog, useConsoleAction, useConsoleEvent, useConsoleStatus} from "../state/hooks";

// How long "New flight" stays armed. The same as the panel's, for the same
// reason: it throws a clearance away, and must not be one stray click.
const ARMED_MS = 5000;
// How often expired lines are swept. A line's fade is CSS; this only removes
// what has finished fading.
const SWEEP_MS = 1000;

type ShownLine = HudLine & {fading: boolean};

// useHudLines keeps the HUD's three lines in step with the comm log.
function useHudLines(): ShownLine[] {
  const entries = useCommLog();
  const previous = useRef(entries);
  const [lines, setLines] = useState<HudLine[]>([]);
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    const before = previous.current;
    previous.current = entries;
    if (before === entries) {
      return;
    }
    const at = Date.now();
    setNow(at);
    setLines((shown) => nextHudLines(before, entries, shown, at));
  }, [entries]);

  useEffect(() => {
    if (lines.length === 0) {
      return;
    }
    const timer = setInterval(() => {
      const at = Date.now();
      setNow(at);
      setLines((shown) => {
        const live = liveHudLines(shown, at);
        return live.length === shown.length ? shown : live;
      });
    }, SWEEP_MS);
    return () => clearInterval(timer);
  }, [lines.length]);

  return lines.map((line) => ({...line, fading: isFading(line, now)}));
}

// useReportHeight tells the plugin how tall the HUD is drawing, every time
// that changes, so its window is never taller than what is on it.
function useReportHeight(root: RefObject<HTMLElement | null>): void {
  const {bridge} = useConsole();
  const last = useRef(0);

  useLayoutEffect(() => {
    const element = root.current;
    if (!element || !bridge) {
      return;
    }
    function report(): void {
      if (!element || !bridge) {
        return;
      }
      const height = Math.ceil(element.getBoundingClientRect().height);
      if (height > 0 && height !== last.current) {
        last.current = height;
        bridge.notify(HUD_LAYOUT_CHANNEL, {height});
      }
    }
    report();
    if (typeof ResizeObserver === "undefined") {
      return;
    }
    const observer = new ResizeObserver(report);
    observer.observe(element);
    return () => observer.disconnect();
  }, [bridge, root]);
}

// The HUD: the last three transmissions over the cockpit, and a drawer for the
// three things a pilot reaches for without opening the panel.
//
// Everything here is drawn on a transparent window, so nothing has a
// background unless it is meant to be seen: the sim shows through the rest.
export function HudApp() {
  const rootRef = useRef<HTMLElement | null>(null);
  const {store, boot} = useConsole();
  const status = useConsoleStatus();
  const session = useConsoleEvent<SessionView>(EVENTS.flightSnapshot);
  const lines = useHudLines();
  const {run, error, clearError, busy} = useConsoleAction();
  const [open, setOpen] = useState(false);
  const [arming, setArming] = useState(false);
  const [note, setNote] = useState("");

  useReportHeight(rootRef);

  useEffect(() => {
    if (!arming) {
      return;
    }
    const lapse = setTimeout(() => setArming(false), ARMED_MS);
    return () => clearTimeout(lapse);
  }, [arming]);

  const reachable = isConsoleReachable(status);
  const ready = boot === "ready";

  function reloadPlan(): void {
    clearError();
    setNote("");
    void run(ACTIONS.refreshFlightPlan, {pilotId: ""})
      .then((payload) => {
        store.setEvent(EVENTS.flightPlan, payload);
        setNote("Flight plan reloaded");
      })
      .catch(() => {});
  }

  function newFlight(): void {
    if (!arming) {
      setArming(true);
      return;
    }
    setArming(false);
    clearError();
    setNote("");
    void run(ACTIONS.endFlightSession)
      .then(() => {
        store.clearComm();
        setNote("New flight started");
      })
      .catch(() => {});
  }

  function openPanel(): void {
    clearError();
    void run(LOCAL_ACTIONS.openPanel).catch(() => {});
  }

  return (
    <main className="hud" ref={rootRef} aria-label="Radio HUD">
      <div className="hud-bar">
        <span
          className={`hud-led${reachable ? "" : " is-unreachable"}`}
          aria-label={reachable ? "Console connected" : "Console unreachable"}
          role="img"
        />
        <span className="hud-title">{session?.callsign || "zoal-atc"}</span>
        {reachable ? null : <span className="hud-warn">console unreachable</span>}
        <button
          type="button"
          className="hud-handle"
          aria-expanded={open}
          aria-controls="hud-drawer"
          onClick={() => {
            setOpen((value) => !value);
            setArming(false);
          }}
        >
          {open ? "Close" : "Menu"}
        </button>
      </div>

      {open ? (
        <div className="hud-drawer" id="hud-drawer">
          <button
            type="button"
            className="btn tonal"
            onClick={reloadPlan}
            disabled={busy || !ready}
          >
            Reload plan
          </button>
          <button
            type="button"
            className={`btn ${arming ? "btn-filled is-armed" : "tonal"}`}
            onClick={newFlight}
            disabled={busy || !ready}
          >
            {arming ? "Confirm new flight" : "New flight"}
          </button>
          <button type="button" className="btn outlined" onClick={openPanel} disabled={!ready}>
            Open panel
          </button>
          {note ? <p className="hud-note">{note}</p> : null}
          <ErrorText message={error} />
        </div>
      ) : null}

      {lines.length > 0 ? (
        <ul className="hud-feed" aria-live="polite" aria-label="Recent transmissions">
          {lines.map((line) => (
            <CommBubble
              key={line.key}
              entry={line.entry}
              ownCallsign={session?.callsign}
              className={line.fading ? "is-fading" : ""}
            />
          ))}
        </ul>
      ) : null}
    </main>
  );
}
