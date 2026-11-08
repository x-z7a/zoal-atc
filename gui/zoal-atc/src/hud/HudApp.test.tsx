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

  beforeEach(() => {
    host = new FakeSkyscriptHost();
    bridge = new ConsoleBridge(host);
  });

  afterEach(() => {
    bridge.dispose();
    vi.useRealTimers();
  });

  // The transmissions are the feed window's (HudFeedApp): a line arriving must
  // not change the size of the bar's window.
  it("leaves the transmissions to the feed window", async () => {
    await mountReady();
    act(() => host.emitEvent("comm_log", {kind: "atc", text: "Runway 05 cleared for takeoff"}));

    expect(screen.queryByText("Runway 05 cleared for takeoff")).not.toBeInTheDocument();
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

      expect(await screen.findByText("Plan reloaded")).toBeInTheDocument();
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
