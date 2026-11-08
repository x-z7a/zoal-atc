import {useEffect, useState} from "react";

import {EVENTS, HUD_FEED_LAYOUT_CHANNEL} from "../bridge/types";
import type {SessionView} from "../bridge/views";
import {CommBubble} from "../components/CommLog";
import {HUD_FEED_COLLAPSED_PX, HUD_FEED_HEIGHT_PX} from "../domain/hud";
import {useConsole} from "../state/ConsoleProvider";
import {useConsoleEvent, useThemeSync} from "../state/hooks";
import {useHudLines, useReleaseFocus} from "./shared";

// Taller than this, the plugin has grown the window for us. Deliberately far
// below the full height: how many CSS pixels a window of so many boxels gives
// depends on X-Plane's UI scale, and the gate must open at any of them.
const EXPANDED_MIN_PX = 24;
// If the window never grows -- no plugin, an older one -- draw anyway after
// this rather than hold the radio back.
const EXPAND_TIMEOUT_MS = 1000;

// useWindowExpanded reports whether the window is now big enough to draw in,
// once `wanted` asks it to be.
function useWindowExpanded(wanted: boolean): boolean {
  const [tall, setTall] = useState(() => window.innerHeight > EXPANDED_MIN_PX);
  const [waitedOut, setWaitedOut] = useState(false);

  useEffect(() => {
    function measure(): void {
      setTall(window.innerHeight > EXPANDED_MIN_PX);
    }
    measure();
    window.addEventListener("resize", measure);
    return () => window.removeEventListener("resize", measure);
  }, []);

  useEffect(() => {
    if (!wanted) {
      setWaitedOut(false);
      return;
    }
    const timer = setTimeout(() => setWaitedOut(true), EXPAND_TIMEOUT_MS);
    return () => clearTimeout(timer);
  }, [wanted]);

  return wanted && (tall || waitedOut);
}

// The HUD's transmissions, in a window of their own under the bar.
//
// The window has two sizes and changes between them only while it is showing
// nothing, so X-Plane's stretched redraw of a resized window never has
// anything to stretch:
//
//   - empty, it asks for a 1px strip, so it takes no cockpit clicks;
//   - when a line arrives it asks for its full height and draws nothing until
//     the window has grown;
//   - while any line is up it stays at full height -- a line arriving or
//     leaving moves nothing;
//   - the last line leaves only once it has faded out, and then it shrinks.
export function HudFeedApp() {
  const {bridge} = useConsole();
  useThemeSync();
  useReleaseFocus();
  const session = useConsoleEvent<SessionView>(EVENTS.flightSnapshot);
  const lines = useHudLines();
  const wanted = lines.length > 0;
  const expanded = useWindowExpanded(wanted);

  useEffect(() => {
    bridge?.notify(HUD_FEED_LAYOUT_CHANNEL, {
      height: wanted ? HUD_FEED_HEIGHT_PX : HUD_FEED_COLLAPSED_PX,
    });
  }, [bridge, wanted]);

  return (
    <main className="hud hud-feed-window" aria-label="Radio HUD transmissions">
      {expanded ? (
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
