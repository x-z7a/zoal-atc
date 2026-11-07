import {describe, expect, it} from "vitest";

import type {CommLogEntry} from "../bridge/views";
import {HUD_LIFESPAN_MS, isFading, liveHudLines, nextHudLines, type HudLine} from "./hud";

const NOW = Date.parse("2026-11-07T12:00:00Z");

function line(text: string, at = new Date(NOW).toISOString()): CommLogEntry {
  return {kind: "atc", text, at, spokenBy: "tower"};
}

describe("the HUD", () => {
  it("shows a transmission the moment it arrives", () => {
    const first = line("Cleared for takeoff");
    const lines = nextHudLines([], [first], [], NOW);
    expect(lines.map((l) => l.entry.text)).toEqual(["Cleared for takeoff"]);
  });

  it("keeps only the newest three", () => {
    let log: CommLogEntry[] = [];
    let lines: HudLine[] = [];
    for (const text of ["one", "two", "three", "four"]) {
      const next = [...log, line(text)];
      lines = nextHudLines(log, next, lines, NOW);
      log = next;
    }
    expect(lines.map((l) => l.entry.text)).toEqual(["two", "three", "four"]);
  });

  it("lets each line go a minute after it came in", () => {
    const first = line("Contact departure");
    const lines = nextHudLines([], [first], [], NOW);
    expect(liveHudLines(lines, NOW + HUD_LIFESPAN_MS - 1)).toHaveLength(1);
    expect(liveHudLines(lines, NOW + HUD_LIFESPAN_MS)).toHaveLength(0);
  });

  it("fades a line out over its last seconds", () => {
    const [shown] = nextHudLines([], [line("Squawk 4521")], [], NOW);
    expect(isFading(shown, NOW + 10_000)).toBe(false);
    expect(isFading(shown, NOW + HUD_LIFESPAN_MS - 1_000)).toBe(true);
  });

  // The plugin replays its newest cached line to every page that loads. That
  // line may be from an hour ago, and it is not what ATC just said.
  it("ignores a replayed line that was said long before", () => {
    const stale = line("Taxi to runway 07", new Date(NOW - 10 * 60_000).toISOString());
    expect(nextHudLines([], [stale], [], NOW)).toEqual([]);
  });

  // A resync replaces the log with the console's record. Everything in it is
  // history the pilot has either heard or can read in the panel.
  it("does not replay history when the log is replaced", () => {
    const shown = nextHudLines([], [line("one")], [], NOW);
    const replaced = [line("older"), line("one")];
    expect(nextHudLines([line("x")], replaced, shown, NOW).map((l) => l.entry.text)).toEqual([
      "one",
    ]);
  });

  it("clears with a new flight", () => {
    const shown = nextHudLines([], [line("one")], [], NOW);
    expect(nextHudLines([line("one")], [], shown, NOW)).toEqual([]);
  });

  it("shows an unstamped line, since nothing says it is old", () => {
    const unstamped: CommLogEntry = {kind: "pilot", text: "Ready for departure"};
    expect(nextHudLines([], [unstamped], [], NOW)).toHaveLength(1);
  });
});
