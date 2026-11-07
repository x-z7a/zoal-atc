import {act, render, screen, waitFor} from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {afterEach, beforeEach, describe, expect, it, vi} from "vitest";

import {ConsoleBridge} from "../bridge/ConsoleBridge";
import {FakeSkyscriptHost} from "../bridge/fakeHost";
import {ConsoleProvider} from "../state/ConsoleProvider";
import {HudApp} from "./HudApp";

// The HUD over a fake Skyscript host, as a pilot would see it in the corner of
// the screen with the panel closed.
describe("the HUD", () => {
  let host: FakeSkyscriptHost;
  let bridge: ConsoleBridge;

  async function mountReady() {
    const result = render(
      <ConsoleProvider bridge={bridge}>
        <HudApp />
      </ConsoleProvider>,
    );
    await waitFor(() => expect(host.postedChannels).toContain("console.ready"));
    host.emitStatus({connected: true, subscribed: true});
    return result;
  }

  function transmit(text: string, kind = "atc", at = new Date().toISOString()): void {
    act(() => host.emitEvent("comm_log", {kind, text, at, spokenBy: "tower"}));
  }

  beforeEach(() => {
    host = new FakeSkyscriptHost();
    bridge = new ConsoleBridge(host);
  });

  afterEach(() => {
    bridge.dispose();
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

  it("says when the console is unreachable", async () => {
    await mountReady();
    act(() => host.emitStatus({connected: false, subscribed: false}));

    expect(await screen.findByText("console unreachable")).toBeInTheDocument();
  });

  it("tells the plugin how tall it is drawing", async () => {
    await mountReady();

    // jsdom lays nothing out, so the height is zero and nothing is reported;
    // the channel is what matters here, and that it is never sent a zero.
    const layouts = host.posts.filter((post) => post.channel === "hud.layout");
    for (const post of layouts) {
      expect((post.payload as {height: number}).height).toBeGreaterThan(0);
    }
  });

  describe("drawer", () => {
    async function openDrawer() {
      await mountReady();
      await userEvent.click(screen.getByRole("button", {name: "Menu"}));
    }

    it("reloads the flight plan", async () => {
      await openDrawer();
      await userEvent.click(screen.getByRole("button", {name: "Reload plan"}));

      await waitFor(() =>
        expect(host.requests.some((request) => request.action === "refresh_flight_plan")).toBe(
          true,
        ),
      );
      const reload = host.requests.find((request) => request.action === "refresh_flight_plan");
      host.respondTo(reload!.requestId, {hasPlan: true, origin: "NZAA", destination: "NZQN"});

      expect(await screen.findByText("Flight plan reloaded")).toBeInTheDocument();
    });

    // It throws the clearance away, so one press only arms it.
    it("asks before starting a new flight", async () => {
      await openDrawer();
      await userEvent.click(screen.getByRole("button", {name: "New flight"}));

      expect(host.requests.some((request) => request.action === "end_flight_session")).toBe(false);
      await userEvent.click(screen.getByRole("button", {name: "Confirm new flight"}));

      await waitFor(() =>
        expect(host.requests.some((request) => request.action === "end_flight_session")).toBe(
          true,
        ),
      );
    });

    it("opens the full panel through the plugin", async () => {
      await openDrawer();
      await userEvent.click(screen.getByRole("button", {name: "Open panel"}));

      await waitFor(() =>
        expect(host.requests.some((request) => request.action === "open_panel")).toBe(true),
      );
    });

    it("shows a refusal rather than pretending", async () => {
      await openDrawer();
      await userEvent.click(screen.getByRole("button", {name: "Reload plan"}));
      await waitFor(() => expect(host.lastRequestId).toBeGreaterThan(0));
      const reload = host.requests.find((request) => request.action === "refresh_flight_plan");
      host.failRequest(reload!.requestId, "no SimBrief pilot ID");

      expect(await screen.findByText("no SimBrief pilot ID")).toBeInTheDocument();
    });
  });
});
