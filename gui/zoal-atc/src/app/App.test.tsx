import {render, screen, waitFor, within} from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {afterEach, beforeEach, describe, expect, it} from "vitest";

import {ConsoleBridge} from "../bridge/ConsoleBridge";
import {FakeSkyscriptHost} from "../bridge/fakeHost";
import {ConsoleProvider} from "../state/ConsoleProvider";
import {App} from "./App";

// The whole panel over a fake Skyscript host: no X-Plane, no plugin, no
// console. Everything below is what a pilot would see.
describe("the panel", () => {
  let host: FakeSkyscriptHost;
  let bridge: ConsoleBridge;

  const SEARCH = "?plugin_version=0.1.0&skyscript=ready";

  function mount(search = SEARCH) {
    return render(
      <ConsoleProvider bridge={bridge}>
        <App search={search} />
      </ConsoleProvider>,
    );
  }

  async function mountReady(search = SEARCH) {
    const result = mount(search);
    await waitFor(() => expect(host.postedChannels).toContain("console.ready"));
    return result;
  }

  function connected(): void {
    host.emitStatus({connected: true, subscribed: true});
  }

  beforeEach(() => {
    host = new FakeSkyscriptHost();
    bridge = new ConsoleBridge(host);
  });

  afterEach(() => {
    bridge.dispose();
  });

  it("shows the plugin version the plugin put in the URL", async () => {
    await mountReady();

    expect(screen.getByText("zoal-atc 0.1.0")).toBeInTheDocument();
    expect(screen.getByRole("heading", {name: "Radio Panel"})).toBeInTheDocument();
  });

  it("opens on Home", async () => {
    await mountReady();

    expect(screen.getByRole("tab", {name: "Home"})).toHaveAttribute("aria-selected", "true");
  });

  describe("own-ship", () => {
    it("shows what the console publishes", async () => {
      await mountReady();
      connected();

      host.emitEvent("flight_snapshot", {callsign: "GABCD", lifecycle: "taxi_out"});
      host.emitEvent("telemetry", {
        frequencyMhz: 121.9,
        standbyMhz: 118.8,
        altitudeFtMsl: 374,
        groundspeedKts: 12,
        onGround: true,
        airport: "CYOW",
        surveillance: "current",
        trafficCount: 3,
      });
      host.emitEvent("facility_snapshot", {count: 2, flights: []});

      await screen.findByText("GABCD");
      expect(screen.getByText("taxi_out")).toBeInTheDocument();
      expect(screen.getByText("121.900")).toBeInTheDocument();
      expect(screen.getByText("sby 118.800")).toBeInTheDocument();
      expect(screen.getByText("on ground")).toBeInTheDocument();
      expect(screen.getByText("3 contacts")).toBeInTheDocument();
      expect(screen.getByText("2 aircraft")).toBeInTheDocument();
    });

    // An absent feed is neither occupied nor clear (phase 21).
    it("never reports an empty sky from a feed it does not have", async () => {
      await mountReady();
      connected();

      host.emitEvent("telemetry", {
        altitudeFtMsl: 0,
        groundspeedKts: 0,
        onGround: true,
        surveillance: "unavailable",
        trafficCount: 0,
      });

      await screen.findByText("unavailable");
      expect(screen.queryByText("0 contacts")).toBeNull();
    });
  });

  describe("the radio", () => {
    it("keeps transmissions in the order they were heard", async () => {
      await mountReady();
      connected();

      host.emitEvent("comm_log", {kind: "pilot", text: "Ottawa ground, GABCD, ready to taxi"});
      host.emitEvent("comm_log", {kind: "atc", text: "GABCD, taxi to runway 07 via alpha"});

      const log = await screen.findByRole("list", {name: "Radio log"});
      const rows = within(log).getAllByRole("listitem");
      expect(rows[0]).toHaveTextContent("ready to taxi");
      expect(rows[1]).toHaveTextContent("taxi to runway 07 via alpha");
    });

    // A clearance appearing in two cockpits reads as normal operation unless
    // the copy is marked (phase 22 M7).
    it("marks a party-line copy as someone else's", async () => {
      await mountReady();
      connected();

      host.emitEvent("comm_log", {
        kind: "atc",
        text: "GWXYZ, cleared to land runway 07",
        overheard: true,
        addressedTo: "GWXYZ",
      });

      const row = (await screen.findByText("GWXYZ, cleared to land runway 07")).closest("li");
      expect(row).toHaveClass("is-overheard");
      expect(within(row as HTMLElement).getByText("to GWXYZ")).toBeInTheDocument();
    });

    // Which seat is talking is the first thing a pilot needs from the log.
    it("says which controller spoke", async () => {
      await mountReady();
      connected();

      host.emitEvent("comm_log", {
        kind: "atc",
        text: "GABCD, taxi to runway 07 via alpha",
        spokenBy: "ground",
      });
      host.emitEvent("comm_log", {
        kind: "atc",
        text: "GABCD, cleared for takeoff runway 07",
        spokenBy: "tower",
      });

      expect(await screen.findByText("Ground")).toBeInTheDocument();
      expect(screen.getByText("Tower")).toBeInTheDocument();
    });

    it("labels this cockpit with its own callsign", async () => {
      await mountReady();
      connected();

      host.emitEvent("flight_snapshot", {callsign: "GABCD", lifecycle: "taxi_out"});
      host.emitEvent("comm_log", {kind: "pilot", text: "ready to taxi"});

      const row = (await screen.findByText("ready to taxi")).closest("li");
      expect(row).toHaveClass("from-pilot");
      expect(within(row as HTMLElement).getByText("GABCD")).toBeInTheDocument();
    });
  });

  describe("tuning", () => {
    it("sends the frequency and target the console expects", async () => {
      await mountReady();
      connected();

      await userEvent.type(screen.getByLabelText("Tune COM1"), "118.800");
      await userEvent.click(screen.getByRole("button", {name: "Tune"}));

      await waitFor(() => {
        expect(host.requests.some((request) => request.action === "tune_radio")).toBe(true);
      });
      const tune = host.requests.find((request) => request.action === "tune_radio");
      expect(tune?.payload).toEqual({mhz: 118.8, target: "active"});
    });

    // The panel is a command source, not the decider. When the console refuses,
    // its words are what the pilot sees.
    it("shows the console's refusal verbatim", async () => {
      await mountReady();
      connected();

      await userEvent.type(screen.getByLabelText("Tune COM1"), "999.999");
      await userEvent.click(screen.getByRole("button", {name: "Tune"}));

      await waitFor(() => expect(host.lastRequestId).toBeGreaterThan(0));
      host.failRequest(host.lastRequestId, "frequency 999.999 is outside the VHF band");

      expect(
        await screen.findByText("frequency 999.999 is outside the VHF band"),
      ).toBeInTheDocument();
    });
  });

  describe("transmitting by keyboard", () => {
    it("sends what was typed and clears the box", async () => {
      await mountReady();
      connected();

      const box = screen.getByLabelText("Transmit");
      await userEvent.type(box, "  ready to taxi  ");
      await userEvent.click(screen.getByRole("button", {name: "Send"}));

      await waitFor(() => {
        expect(host.requests.some((request) => request.action === "submit_text")).toBe(true);
      });
      const sent = host.requests.find((request) => request.action === "submit_text");
      expect(sent?.payload).toEqual({text: "ready to taxi"});
      expect(box).toHaveValue("");
    });

    it("does not transmit an empty box", async () => {
      await mountReady();
      connected();

      await userEvent.click(screen.getByRole("button", {name: "Send"}));

      expect(host.requests.some((request) => request.action === "submit_text")).toBe(false);
    });
  });

  // The console already remembers this aircraft's pilot ID, so wanting the plan
  // on the panel should not mean a trip to Settings.
  describe("loading the flight plan from Home", () => {
    it("imports with the remembered ID in one press", async () => {
      await mountReady();
      connected();

      await userEvent.click(screen.getByRole("button", {name: "SimBrief"}));

      await waitFor(() => {
        expect(
          host.requests.some((request) => request.action === "refresh_flight_plan"),
        ).toBe(true);
      });
      const load = host.requests.find((request) => request.action === "refresh_flight_plan");
      expect(load?.payload).toEqual({pilotId: ""});
    });

    it("offers a reload once a plan is loaded", async () => {
      await mountReady();
      connected();

      host.emitEvent("flight_plan", {hasPlan: true, origin: "CYOW", destination: "CYGK"});

      expect(await screen.findByRole("button", {name: "Reload"})).toBeInTheDocument();
      expect(screen.getByText("CYOW -> CYGK")).toBeInTheDocument();
    });
  });

  describe("the two silences", () => {
    // A dead console means nobody is listening to the radio; stale telemetry
    // means the numbers stopped arriving. Saying the wrong one hides the cause.
    it("says the console is unreachable, not that telemetry is stale", async () => {
      await mountReady();
      connected();
      host.emitEvent("telemetry", {altitudeFtMsl: 3000, groundspeedKts: 100, onGround: false});

      host.emitStatus({connected: false, subscribed: false});

      expect(
        await screen.findByText("console unreachable - values are last known"),
      ).toBeInTheDocument();
      expect(screen.queryByText(/no telemetry for/)).toBeNull();
    });

    it("dims the readouts while the console is gone", async () => {
      const {container} = await mountReady();
      connected();
      await waitFor(() => {
        expect(container.querySelector(".panel")).not.toHaveClass("console-unreachable");
      });

      host.emitStatus({connected: false, subscribed: false});

      await waitFor(() => {
        expect(container.querySelector(".panel")).toHaveClass("console-unreachable");
      });
    });

    // A pilot who pressed pause has not lost their feed.
    it("says paused rather than raising a stale alarm", async () => {
      await mountReady();
      connected();

      host.emitEvent("telemetry", {
        altitudeFtMsl: 3000,
        groundspeedKts: 0,
        onGround: false,
        paused: true,
      });

      expect(await screen.findByText("paused")).toBeInTheDocument();
    });
  });

  describe("tabs", () => {
    // A chat's input belongs at the bottom of the chat, and there is only one
    // of it -- Debug does not carry a second.
    it("puts the transmit box with the radio, and nowhere else", async () => {
      await mountReady();
      connected();

      expect(screen.getByLabelText("Transmit")).toBeInTheDocument();

      await userEvent.click(screen.getByRole("tab", {name: "Debug"}));

      expect(screen.queryByLabelText("Transmit")).toBeNull();
    });

    // The reason ConsoleStore exists. facility_snapshot only fires when an
    // aircraft connects, so without a remembered value a tab opened afterwards
    // would show an empty bay until somebody else took off.
    it("shows values that arrived before the tab was opened", async () => {
      await mountReady();
      connected();

      host.emitEvent("facility_snapshot", {
        count: 1,
        flights: [
          {
            flightId: "flight-1",
            callsign: "GABCD",
            lifecycle: "cruise",
            service: "center",
            frequencyMhz: 133.4,
            onGround: false,
            altitudeFt: 7000,
            hasTelemetry: true,
            telemetryAgeSeconds: 1,
          },
        ],
      });

      await userEvent.click(screen.getByRole("tab", {name: "Debug"}));

      expect(screen.getByText("cruise")).toBeInTheDocument();
      expect(screen.getByText("133.400")).toBeInTheDocument();
    });

    // The console publishes these and the pre-React panel handled neither.
    it("shows the last turn on Debug", async () => {
      await mountReady();
      connected();

      host.emitEvent("transcript", {text: "ready to taxi"});
      host.emitEvent("atc_reply", {text: "taxi via alpha", category: "instruction", sent: true});

      await userEvent.click(screen.getByRole("tab", {name: "Debug"}));

      expect(screen.getByText("ready to taxi")).toBeInTheDocument();
      expect(screen.getByText("taxi via alpha")).toBeInTheDocument();
      expect(screen.getByText("instruction")).toBeInTheDocument();
    });

    it("offers the SimBrief pilot ID on Settings", async () => {
      await mountReady();
      connected();

      await userEvent.click(screen.getByRole("tab", {name: "Settings"}));

      expect(screen.getByLabelText("SimBrief pilot ID")).toBeInTheDocument();
    });
  });

  describe("the debug log", () => {
    async function openDebug() {
      await mountReady();
      connected();
      await userEvent.click(screen.getByRole("tab", {name: "Debug"}));
      await waitFor(() => {
        expect(host.requests.some((request) => request.action === "debug_tail")).toBe(true);
      });
      return host.requests.filter((request) => request.action === "debug_tail").pop();
    }

    // Pulled rather than pushed, and only while the tab is mounted: the log is
    // verbose and nobody wants it streaming behind a tab nobody is looking at.
    it("asks for the log when the tab is opened, and not before", async () => {
      await mountReady();
      connected();
      expect(host.requests.some((request) => request.action === "debug_tail")).toBe(false);

      await userEvent.click(screen.getByRole("tab", {name: "Debug"}));

      await waitFor(() => {
        expect(host.requests.some((request) => request.action === "debug_tail")).toBe(true);
      });
    });

    it("shows the records the console returns, newest first", async () => {
      const read = await openDebug();
      host.respondTo(read?.requestId ?? 0, [
        {ts: "2026-08-13T20:00:00Z", category: "ai_call", fields: {event: "judge", latency_ms: 900}},
        {ts: "2026-08-13T20:00:05Z", category: "delivery", fields: {event: "commit"}},
      ]);

      const rows = await screen.findAllByRole("listitem");
      const debugRows = rows.filter((row) => row.textContent?.includes("event="));
      expect(debugRows[0]).toHaveTextContent("event=commit");
      expect(debugRows[1]).toHaveTextContent("event=judge");
    });

    it("re-reads under the category the chip names", async () => {
      const read = await openDebug();
      host.respondTo(read?.requestId ?? 0, []);

      await userEvent.click(screen.getByRole("button", {name: "Model"}));

      await waitFor(() => {
        const pulls = host.requests.filter((request) => request.action === "debug_tail");
        expect(pulls[pulls.length - 1]?.payload).toMatchObject({category: "ai_call"});
      });
    });

    it("says so plainly when a category is empty", async () => {
      const read = await openDebug();
      host.respondTo(read?.requestId ?? 0, []);

      expect(
        await screen.findByText("No records under this category yet."),
      ).toBeInTheDocument();
    });
  });

  describe("settings", () => {
    async function openSettings() {
      await mountReady();
      connected();
      await userEvent.click(screen.getByRole("tab", {name: "Settings"}));
      // The tab reads what is stored on the way in.
      await waitFor(() => {
        expect(host.requests.some((request) => request.action === "settings")).toBe(true);
      });
      const read = host.requests.filter((request) => request.action === "settings").pop();
      return read?.requestId ?? 0;
    }

    // A field that starts empty when a value is remembered invites the pilot to
    // retype what the console already has, and to wonder which one it is using.
    it("shows the pilot ID the console remembers for this aircraft", async () => {
      const readId = await openSettings();

      host.respondTo(readId, {simbriefPilotId: "516408"});

      await waitFor(() => {
        expect(screen.getByLabelText("SimBrief pilot ID")).toHaveValue("516408");
      });
    });

    it("saves a new pilot ID under the action the console pins", async () => {
      const readId = await openSettings();
      host.respondTo(readId, {simbriefPilotId: ""});
      await waitFor(() => expect(screen.getByLabelText("SimBrief pilot ID")).toHaveValue(""));

      await userEvent.type(screen.getByLabelText("SimBrief pilot ID"), "123456");
      await userEvent.click(screen.getByRole("button", {name: "Save"}));

      await waitFor(() => {
        expect(host.requests.some((request) => request.action === "save_settings")).toBe(true);
      });
      const save = host.requests.find((request) => request.action === "save_settings");
      // The whole settings block goes each time, so saving one field cannot
      // quietly reset another to a default the panel never showed.
      expect(save?.payload).toEqual({
        simbriefPilotId: "123456",
        notifications: {enabled: true, corner: "top_right", timeoutSecs: 8, sound: true},
      });
    });

    // What the console kept, not what the panel sent.
    it("shows the value the console stored", async () => {
      const readId = await openSettings();
      host.respondTo(readId, {simbriefPilotId: ""});
      await waitFor(() => expect(screen.getByLabelText("SimBrief pilot ID")).toHaveValue(""));

      await userEvent.type(screen.getByLabelText("SimBrief pilot ID"), "  123456  ");
      await userEvent.click(screen.getByRole("button", {name: "Save"}));

      await waitFor(() => expect(host.lastRequestId).toBeGreaterThan(readId));
      host.respondTo(host.lastRequestId, {simbriefPilotId: "123456"});

      await waitFor(() => {
        expect(screen.getByLabelText("SimBrief pilot ID")).toHaveValue("123456");
      });
    });

    describe("notifications", () => {
      // A pilot who has never opened Settings still gets told: the failure mode
      // of the alternative is a missed clearance.
      it("is on before anybody chooses anything", async () => {
        const readId = await openSettings();
        host.respondTo(readId, {simbriefPilotId: ""});

        expect(await screen.findByLabelText("Tell me when ATC calls")).toBeChecked();
      });

      it("shows what the console stored", async () => {
        const readId = await openSettings();
        host.respondTo(readId, {
          simbriefPilotId: "",
          notifications: {enabled: false, corner: "bottom_left", timeoutSecs: 15, sound: false},
        });

        await waitFor(() => {
          expect(screen.getByLabelText("Tell me when ATC calls")).not.toBeChecked();
        });
        expect(screen.getByLabelText("Corner")).toHaveValue("bottom_left");
        expect(screen.getByLabelText("Seconds on screen")).toHaveValue("15");
      });

      // A toggle that has to be confirmed with a button is one that gets left
      // half-set.
      it("saves the moment it is switched", async () => {
        const readId = await openSettings();
        host.respondTo(readId, {simbriefPilotId: ""});
        await waitFor(() =>
          expect(screen.getByLabelText("Tell me when ATC calls")).toBeChecked(),
        );

        await userEvent.click(screen.getByLabelText("Tell me when ATC calls"));

        await waitFor(() => {
          expect(host.requests.some((request) => request.action === "save_settings")).toBe(true);
        });
        const save = host.requests.find((request) => request.action === "save_settings");
        expect(save?.payload).toMatchObject({notifications: {enabled: false}});
      });

      // With the window closed nothing on a toast can be clicked, so there is
      // no "until I dismiss it" to offer.
      it("offers no way to make a notification permanent", async () => {
        const readId = await openSettings();
        host.respondTo(readId, {simbriefPilotId: ""});

        const timeout = await screen.findByLabelText("Seconds on screen");
        const values = [...timeout.querySelectorAll("option")].map((option) => option.value);
        expect(values).not.toContain("0");
      });
    });

    it("shows the console's refusal rather than pretending it saved", async () => {
      const readId = await openSettings();
      host.respondTo(readId, {simbriefPilotId: ""});
      await waitFor(() => expect(screen.getByLabelText("SimBrief pilot ID")).toHaveValue(""));

      await userEvent.click(screen.getByRole("button", {name: "Save"}));
      await waitFor(() => expect(host.lastRequestId).toBeGreaterThan(readId));
      host.failRequest(host.lastRequestId, "no flight for this connection");

      expect(await screen.findByText("no flight for this connection")).toBeInTheDocument();
    });
  });

  describe("without a Skyscript host", () => {
    // Opened in an ordinary browser. The panel says so and says where to look,
    // rather than sitting on "connecting" forever.
    it("says so, and where to look", async () => {
      render(
        <ConsoleProvider bridge={new ConsoleBridge(undefined)}>
          <App search={SEARCH} />
        </ConsoleProvider>,
      );

      expect(
        await screen.findByText(/cannot reach the X-Plane panel bridge/),
      ).toBeInTheDocument();
      expect(screen.queryByRole("tab", {name: "Home"})).toBeNull();
    });
  });

  it("asks for the flight plan on the way up", async () => {
    await mountReady();

    await waitFor(() => {
      expect(host.requests.some((request) => request.action === "flight_plan")).toBe(true);
    });
  });
});
