// What the HUD's two windows share: the bar's (HudApp) and the feed's
// (HudFeedApp) are separate pages in separate windows, built from one bundle.

import {useEffect, useLayoutEffect, useRef, useState, type RefObject} from "react";

import {HUD_FOCUS_CHANNEL} from "../bridge/types";
import {isFading, liveHudLines, nextHudLines, type HudLine} from "../domain/hud";
import {useConsole} from "../state/ConsoleProvider";
import {useCommLog} from "../state/hooks";

// How often expired lines are swept. A line's fade is CSS; this only removes
// what has finished fading.
const SWEEP_MS = 1000;

export type ShownLine = HudLine & {fading: boolean};

// Material Symbols outlines (Apache 2.0), inline so the HUD needs no icon font.
export const ICONS = {
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

export function Icon({path}: {path: string}) {
  return (
    <svg className="hud-icon" viewBox="0 -960 960 960" aria-hidden="true" focusable="false">
      <path d={path} />
    </svg>
  );
}

// useHudLines keeps the HUD's three lines in step with the comm log.
export function useHudLines(): ShownLine[] {
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

// useReportHeight tells the plugin how tall `root` is drawing, every time that
// changes, on `channel`.
export function useReportHeight(root: RefObject<HTMLElement | null>, channel: string): void {
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
        bridge.notify(channel, {height});
      }
    }
    report();
    if (typeof ResizeObserver === "undefined") {
      return;
    }
    const observer = new ResizeObserver(report);
    observer.observe(element);
    return () => observer.disconnect();
  }, [bridge, root, channel]);
}

// useReleaseFocus gives the keyboard back to X-Plane once a click in the HUD is
// done with. Deferred a tick so the click's own handler runs first.
export function useReleaseFocus(): void {
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
