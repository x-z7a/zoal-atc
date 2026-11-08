import {act, render, screen, waitFor} from "@testing-library/react";
import {afterEach, beforeEach, describe, expect, it, vi} from "vitest";

import {ConsoleBridge} from "../bridge/ConsoleBridge";
import {FakeSkyscriptHost} from "../bridge/fakeHost";
import {HUD_FEED_HEIGHT_PX} from "../domain/hud";
import {ConsoleProvider} from "../state/ConsoleProvider";
import {HudFeedApp} from "./HudFeedApp";

// The HUD's transmissions, in their own window under the bar.
describe("the HUD feed", () => {
  let host: FakeSkyscriptHost;
  let bridge: ConsoleBridge;
  const fullHeight = window.innerHeight;

  async function mountReady() {
    const result = render(
      <ConsoleProvider bridge={bridge}>
        <HudFeedApp />
      </ConsoleProvider>,
    );
    await waitFor(() => expect(host.postedChannels).toContain("console.ready"));
    host.emitStatus({connected: true, subscribed: true});
    return result;
  }

  function transmit(text: string, kind = "atc", at = new Date().toISOString()): void {
    act(() => host.emitEvent("comm_log", {kind, text, at, spokenBy: "tower"}));
  }

  function askedHeights(): number[] {
    return host.posts
      .filter((post) => post.channel === "hud.feed_layout")
      .map((post) => (post.payload as {height: number}).height);
  }

  function lastAsked(): number | undefined {
    const heights = askedHeights();
    return heights[heights.length - 1];
  }

  function resizeWindow(height: number): void {
    act(() => {
      Object.defineProperty(window, "innerHeight", {configurable: true, value: height});
      window.dispatchEvent(new Event("resize"));
    });
  }

  beforeEach(() => {
    host = new FakeSkyscriptHost();
    bridge = new ConsoleBridge(host);
  });

  afterEach(() => {
    bridge.dispose();
    Object.defineProperty(window, "innerHeight", {configurable: true, value: fullHeight});
    vi.useRealTimers();
  });

  it("shows what ATC just said", async () => {
    await mountReady();
    transmit("Runway 05 cleared for takeoff");

    expect(await screen.findByText("Runway 05 cleared for takeoff")).toBeInTheDocument();
  });

  it("shows the last three transmissions and no more", async () => {
    await mountReady();
    for (const text of ["one", "two", "three", "four"]) {
      transmit(text);
    }

    await screen.findByText("four");
    expect(screen.queryByText("one")).not.toBeInTheDocument();
    expect(screen.getByText("two")).toBeInTheDocument();
  });

  it("lets a transmission go after a minute", async () => {
    vi.useFakeTimers({shouldAdvanceTime: true});
    await mountReady();
    transmit("Contact departure 124.3");
    await screen.findByText("Contact departure 124.3");

    await act(async () => {
      vi.advanceTimersByTime(61_000);
    });

    expect(screen.queryByText("Contact departure 124.3")).not.toBeInTheDocument();
  });

  // Empty, a strip that takes no clicks; with a line up, one full height.
  it("asks for its full height while it has lines, and a strip when it has none", async () => {
    vi.useFakeTimers({shouldAdvanceTime: true});
    await mountReady();
    expect(lastAsked()).toBe(1);

    transmit("Contact departure 124.3");
    await waitFor(() => expect(lastAsked()).toBe(HUD_FEED_HEIGHT_PX));

    await act(async () => {
      vi.advanceTimersByTime(61_000);
    });
    await waitFor(() => expect(lastAsked()).toBe(1));
  });

  // A line arriving or leaving while others are up must not resize anything:
  // that resize is what made the HUD bounce.
  it("does not resize as lines come and go", async () => {
    await mountReady();
    transmit("one");
    await screen.findByText("one");
    const before = askedHeights().length;

    transmit("two");
    transmit("three");
    transmit("four");
    await screen.findByText("four");

    expect(askedHeights().length).toBe(before);
  });

  // Drawn into a window that has not grown yet, the line would be stretched
  // from a 1px strip for a frame or two.
  it("draws nothing until its window has grown", async () => {
    resizeWindow(1);
    await mountReady();
    transmit("Runway 05 cleared for takeoff");

    await waitFor(() => expect(lastAsked()).toBe(HUD_FEED_HEIGHT_PX));
    expect(screen.queryByText("Runway 05 cleared for takeoff")).not.toBeInTheDocument();

    resizeWindow(HUD_FEED_HEIGHT_PX);
    expect(await screen.findByText("Runway 05 cleared for takeoff")).toBeInTheDocument();
  });

  // No plugin to grow it -- an older one, a browser -- must not hold the radio
  // back for good.
  it("draws anyway if the window never grows", async () => {
    vi.useFakeTimers({shouldAdvanceTime: true});
    resizeWindow(1);
    await mountReady();
    transmit("Runway 05 cleared for takeoff");

    await act(async () => {
      vi.advanceTimersByTime(1_500);
    });

    expect(await screen.findByText("Runway 05 cleared for takeoff")).toBeInTheDocument();
  });
});
