// What the HUD shows: the last few transmissions, each for a minute.
//
// The HUD is not a second radio log. It is the answer to "what did ATC just
// say?" with the panel closed, so it shows only what arrived while it was up,
// keeps three lines at most, and lets each one go a minute after it came in.
// The full record is one click away in the panel.

import type {CommLogEntry} from "../bridge/views";

export const HUD_LINE_LIMIT = 3;
export const HUD_LIFESPAN_MS = 60_000;
// How long before the end a line starts to fade, so it leaves rather than
// vanishing mid-read.
export const HUD_FADE_MS = 5_000;
// Slack for the console's clock and this computer's disagreeing. A line whose
// own timestamp is older than its lifespan plus this was said before the HUD
// was looking: the plugin replays its newest cached line to every page that
// loads, and a clearance from the last session is not news.
const CLOCK_SKEW_MS = 30_000;

export type HudLine = {
  key: string;
  entry: CommLogEntry;
  // When the HUD first showed it, on this computer's clock. The lifespan runs
  // from here rather than from the console's timestamp, so a skewed clock cannot
  // cut a line short.
  shownAt: number;
};

function lineKey(entry: CommLogEntry, shownAt: number): string {
  return [entry.at ?? "", entry.generation ?? "", entry.kind ?? "", entry.text ?? "", shownAt].join(
    "|",
  );
}

function saidTooLongAgo(entry: CommLogEntry, now: number): boolean {
  if (!entry.at) {
    return false;
  }
  const at = Date.parse(entry.at);
  return Number.isFinite(at) && at < now - HUD_LIFESPAN_MS - CLOCK_SKEW_MS;
}

// extends reports whether `next` is `previous` with lines added to the end --
// which is what a transmission arriving looks like in the store. Anything else
// is the log being replaced (a resync after the window was closed) or cleared
// (a new flight), and neither is something that was just said.
function extendsLog(previous: readonly CommLogEntry[], next: readonly CommLogEntry[]): boolean {
  if (next.length < previous.length) {
    return false;
  }
  for (let i = 0; i < previous.length; i++) {
    if (previous[i] !== next[i]) {
      return false;
    }
  }
  return true;
}

// nextHudLines folds a change in the comm log into what the HUD shows.
export function nextHudLines(
  previous: readonly CommLogEntry[],
  next: readonly CommLogEntry[],
  lines: readonly HudLine[],
  now: number,
): HudLine[] {
  if (next.length === 0) {
    // A new flight: what the last one was told is not this one's business.
    return [];
  }
  const shown = [...lines];
  if (extendsLog(previous, next)) {
    for (const entry of next.slice(previous.length)) {
      if (!entry || saidTooLongAgo(entry, now)) {
        continue;
      }
      shown.push({key: lineKey(entry, now), entry, shownAt: now});
    }
  }
  return liveHudLines(shown, now);
}

// liveHudLines drops what has outlived its minute and keeps the newest three.
export function liveHudLines(lines: readonly HudLine[], now: number): HudLine[] {
  const live = lines.filter((line) => now - line.shownAt < HUD_LIFESPAN_MS);
  return live.length > HUD_LINE_LIMIT ? live.slice(live.length - HUD_LINE_LIMIT) : live;
}

export function isFading(line: HudLine, now: number): boolean {
  return now - line.shownAt >= HUD_LIFESPAN_MS - HUD_FADE_MS;
}
