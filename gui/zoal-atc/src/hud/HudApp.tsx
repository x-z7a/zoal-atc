import {useEffect, useRef, useState} from "react";

import {ACTIONS, LOCAL_ACTIONS} from "../bridge/actions";
import {EVENTS, HUD_LAYOUT_CHANNEL} from "../bridge/types";
import type {SessionView} from "../bridge/views";
import {isConsoleReachable} from "../domain/status";
import {useConsole} from "../state/ConsoleProvider";
import {useConsoleAction, useConsoleEvent, useConsoleStatus, useThemeSync} from "../state/hooks";
import {Icon, ICONS, useReleaseFocus, useReportHeight} from "./shared";

// How long "New flight" stays armed. The same as the panel's, for the same
// reason: it throws a clearance away, and must not be one stray click.
const ARMED_MS = 5000;
// How long a button shows what it just did, in place of its label. In place,
// not on a line of its own: a new line would resize the window.
const DONE_MS = 3000;

// The HUD's bar: the callsign, the console light, and a menu for the three
// things a pilot reaches for without opening the panel.
//
// Its window is always the same size. The transmissions are another window
// under it (HudFeedApp), because X-Plane redraws a resized browser window
// stretched until the page catches up, and the bar would bounce every time a
// line came or went. Nothing here may change the bar's height: the menu opens
// in the same row and a refusal is shown in place of the menu's buttons.
//
// Everything here is drawn on a transparent window, so nothing has a
// background unless it is meant to be seen: the sim shows through the rest.
export function HudApp() {
  const rootRef = useRef<HTMLElement | null>(null);
  const {store, boot} = useConsole();
  const status = useConsoleStatus();
  useThemeSync();
  const session = useConsoleEvent<SessionView>(EVENTS.flightSnapshot);
  const {run, error, clearError, busy} = useConsoleAction();
  const [open, setOpen] = useState(false);
  const [arming, setArming] = useState(false);
  const [done, setDone] = useState<"reload" | "newFlight" | null>(null);

  useReportHeight(rootRef, HUD_LAYOUT_CHANNEL);
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
          closing it never changes the window's height. */}
      <div className="hud-top">
        {open && error ? (
          <div className="hud-actions is-error" id="hud-drawer" role="alert">
            <Icon path={ICONS.warn} />
            <span className="hud-error-text" title={error}>
              {error}
            </span>
            <button
              type="button"
              className="hud-dismiss"
              aria-label="Dismiss"
              onClick={clearError}
            >
              <Icon path={ICONS.close} />
            </button>
          </div>
        ) : open ? (
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

    </main>
  );
}
