import {useEffect, useLayoutEffect, useRef, useState, type RefObject} from "react";

import {ACTIONS, LOCAL_ACTIONS} from "../bridge/actions";
import {EVENTS, HUD_FOCUS_CHANNEL, HUD_LAYOUT_CHANNEL} from "../bridge/types";
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
// How long a button shows what it just did, in place of its label. In place,
// not on a line of its own: a new line would resize the window.
const DONE_MS = 3000;

type ShownLine = HudLine & {fading: boolean};

// Material Symbols outlines (Apache 2.0), inline so the HUD needs no icon font.
const ICONS = {
  reload:
    "M480-160q-134 0-227-93t-93-227q0-134 93-227t227-93q69 0 132 28.5T720-690v-110h80v280H520v-80h168q-32-56-87.5-88T480-720q-100 0-170 70t-70 170q0 100 70 170t170 70q77 0 139-44t87-116h84q-28 106-114 173t-196 67Z",
  newFlight:
    "M280-80v-100l120-84v-144L80-280v-120l320-224v-176q0-33 23.5-56.5T480-880q33 0 56.5 23.5T560-800v176l320 224v120L560-408v144l120 84v100l-200-60-200 60Z",
  panel:
    "M200-120q-33 0-56.5-23.5T120-200v-560q0-33 23.5-56.5T200-840h280v80H200v560h560v-280h80v280q0 33-23.5 56.5T760-120H200Zm188-212-56-56 372-372H560v-80h280v280h-80v-144L388-332Z",
  check: "M382-240 154-468l57-57 171 171 367-367 57 57-424 424Z",
  menu: "M120-240v-80h720v80H120Zm0-200v-80h720v80H120Zm0-200v-80h720v80H120Z",
  close:
    "m256-200-56-56 224-224-224-224 56-56 224 224 224-224 56 56-224 224 224 224-56 56-224-224-224 224Z",
  warn: "m40-120 440-760 440 760H40Zm138-80h604L480-720 178-200Zm302-40q17 0 28.5-11.5T520-280q0-17-11.5-28.5T480-320q-17 0-28.5 11.5T440-280q0 17 11.5 28.5T480-240Zm-40-120h80v-200h-80v200Zm40-100Z",
} as const;

function Icon({path}: {path: string}) {
  return (
    <svg className="hud-icon" viewBox="0 -960 960 960" aria-hidden="true" focusable="false">
      <path d={path} />
    </svg>
  );
}

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

// useReleaseFocus gives the keyboard back to X-Plane once a click in the HUD is
// done with. Deferred a tick so the click's own handler runs first.
function useReleaseFocus(): void {
  const {bridge} = useConsole();
  useEffect(() => {
    if (!bridge) {
      return;
    }
    function release(): void {
      setTimeout(() => bridge?.notify(HUD_FOCUS_CHANNEL, {}), 0);
    }
    document.addEventListener("pointerup", release, true);
    return () => document.removeEventListener("pointerup", release, true);
  }, [bridge]);
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
  const [done, setDone] = useState<"reload" | "newFlight" | null>(null);

  useReportHeight(rootRef);
  useReleaseFocus();

  useEffect(() => {
    if (!done) {
      return;
    }
    const lapse = setTimeout(() => setDone(null), DONE_MS);
    return () => clearTimeout(lapse);
  }, [done]);

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
    setDone(null);
    void run(ACTIONS.refreshFlightPlan, {pilotId: ""})
      .then((payload) => {
        store.setEvent(EVENTS.flightPlan, payload);
        setDone("reload");
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
    setDone(null);
    void run(ACTIONS.endFlightSession)
      .then(() => {
        store.clearComm();
        setDone("newFlight");
      })
      .catch(() => {});
  }

  function openPanel(): void {
    clearError();
    void run(LOCAL_ACTIONS.openPanel).catch(() => {});
  }

  return (
    <main className="hud" ref={rootRef} aria-label="Radio HUD">
      {/* The menu opens beside the bar, in the same row, so opening and
          closing it never changes the window's height: X-Plane redraws a
          resized browser window stretched until the page catches up. */}
      <div className="hud-top">
        {open ? (
          <div className="hud-actions" id="hud-drawer">
            <button
              type="button"
              className={`hud-action${done === "reload" ? " is-done" : ""}`}
              onClick={reloadPlan}
              disabled={busy || !ready}
            >
              <Icon path={done === "reload" ? ICONS.check : ICONS.reload} />
              {done === "reload" ? "Plan reloaded" : "Reload plan"}
            </button>
            <button
              type="button"
              className={`hud-action${arming ? " is-armed" : ""}${done === "newFlight" ? " is-done" : ""}`}
              onClick={newFlight}
              disabled={busy || !ready}
              aria-label={arming ? "Confirm new flight" : undefined}
            >
              <Icon
                path={arming ? ICONS.warn : done === "newFlight" ? ICONS.check : ICONS.newFlight}
              />
              {arming ? "Confirm" : done === "newFlight" ? "Started" : "New flight"}
            </button>
            <button type="button" className="hud-action" onClick={openPanel} disabled={!ready}>
              <Icon path={ICONS.panel} />
              Open panel
            </button>
          </div>
        ) : null}

        <div className="hud-bar">
          <span
            className={`hud-led${reachable ? "" : " is-unreachable"}`}
            aria-label={reachable ? "Console connected" : "Console unreachable"}
            role="img"
          />
          {/* Open, the bar gives its width to the menu; the LED still says
              whether the console is there. */}
          {open ? null : <span className="hud-title">{session?.callsign || "zoal-atc"}</span>}
          {reachable || open ? null : <span className="hud-warn">console unreachable</span>}
          <button
            type="button"
            className="hud-handle"
            aria-expanded={open}
            aria-controls="hud-drawer"
            aria-label={open ? "Close" : "Menu"}
            onClick={() => {
              setOpen((value) => !value);
              setArming(false);
              setDone(null);
              clearError();
            }}
          >
            <Icon path={open ? ICONS.close : ICONS.menu} />
          </button>
        </div>
      </div>

      {open && error ? (
        <div className="hud-drawer-error">
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
