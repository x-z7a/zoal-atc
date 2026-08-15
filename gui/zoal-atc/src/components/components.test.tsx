import {render, screen, within} from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {describe, expect, it, vi} from "vitest";

import type {BayRow, CommLogEntry} from "../bridge/views";
import {BayTable} from "./BayTable";
import {CommLog} from "./CommLog";
import {ErrorText} from "./ErrorText";
import {FieldRow} from "./FieldRow";
import {StaleBanner} from "./StaleBanner";
import {Tabs, TabPanel} from "./Tabs";
import {Tile} from "./Tile";

describe("Tile", () => {
  it("shows the label, the value and the qualifier", () => {
    render(<Tile label="COM1" value="119.400" detail="sby 135.200" />);

    expect(screen.getByText("COM1")).toBeInTheDocument();
    expect(screen.getByText("119.400")).toBeInTheDocument();
    expect(screen.getByText("sby 135.200")).toBeInTheDocument();
  });

  it("omits the detail line when there is no qualifier", () => {
    const {container} = render(<Tile label="Plugin" value="0.1.0" />);

    expect(container.querySelectorAll(".label")).toHaveLength(1);
  });
});

describe("CommLog", () => {
  function entry(overrides: Partial<CommLogEntry> = {}): CommLogEntry {
    return {kind: "atc", text: "taxi via alpha", ...overrides};
  }

  // Side, colour and a name: three cues, because this is read at a glance in
  // flight and a pilot who has to parse a line to find out whether it was
  // theirs has already lost time.
  it("puts this cockpit on one side and ATC on the other", () => {
    render(
      <CommLog
        entries={[
          entry({kind: "atc", text: "taxi via alpha"}),
          entry({kind: "pilot", text: "alpha"}),
        ]}
      />,
    );

    expect(screen.getByText("taxi via alpha").closest("li")).toHaveClass("from-atc");
    expect(screen.getByText("alpha").closest("li")).toHaveClass("from-pilot");
  });

  it("names the seat that spoke", () => {
    render(
      <CommLog
        entries={[
          entry({spokenBy: "ground", text: "taxi via alpha"}),
          entry({spokenBy: "tower", text: "cleared for takeoff"}),
          entry({spokenBy: "clearance_delivery", text: "cleared to CYGK"}),
        ]}
      />,
    );

    expect(screen.getByText("Ground")).toBeInTheDocument();
    expect(screen.getByText("Tower")).toBeInTheDocument();
    expect(screen.getByText("Clearance Delivery")).toBeInTheDocument();
  });

  // A console that predates the field, or a transmission from no seat at all.
  it("falls back to ATC when no seat is named", () => {
    render(<CommLog entries={[entry({text: "roger"})]} />);

    expect(screen.getByText("ATC")).toBeInTheDocument();
  });

  it("calls this cockpit by its callsign once the console has heard one", () => {
    const {rerender} = render(<CommLog entries={[entry({kind: "pilot", text: "alpha"})]} />);
    expect(screen.getByText("You")).toBeInTheDocument();

    rerender(<CommLog entries={[entry({kind: "pilot", text: "alpha"})]} ownCallsign="GABCD" />);
    expect(screen.getByText("GABCD")).toBeInTheDocument();
  });

  // An overheard copy is another aircraft's clearance. Rendered identically to
  // an addressed one, a routing bug reads as normal operation (phase 22 M7).
  it("marks an overheard copy and says who it was for", () => {
    render(
      <CommLog entries={[entry({overheard: true, addressedTo: "GWXYZ", text: "cleared to land"})]} />,
    );

    const row = screen.getByText("cleared to land").closest("li");
    expect(row).toHaveClass("is-overheard");
    expect(within(row as HTMLElement).getByText("to GWXYZ")).toBeInTheDocument();
  });

  it("says overheard even when the addressee is unknown", () => {
    render(<CommLog entries={[entry({overheard: true, text: "cleared to land"})]} />);

    expect(screen.getByText("overheard")).toBeInTheDocument();
  });

  it("does not tag an addressed transmission", () => {
    const {container} = render(<CommLog entries={[entry()]} />);

    expect(container.querySelector(".bubble-tag")).toBeNull();
  });

  // Following the radio is the default; the button only exists once the pilot
  // has scrolled away from the bottom, which jsdom's lack of layout means is
  // never here.
  it("does not nag while it is following the radio", () => {
    const {rerender} = render(<CommLog entries={[entry({text: "one"})]} />);
    rerender(<CommLog entries={[entry({text: "one"}), entry({text: "two"})]} />);

    expect(screen.queryByRole("button", {name: /new transmission/})).toBeNull();
  });
});

describe("StaleBanner", () => {
  it("renders nothing when everything is current", () => {
    const {container} = render(<StaleBanner freshness={{kind: "fresh", message: ""}} />);

    expect(container.querySelector(".stale-banner")).toBeNull();
  });

  it.each([
    {kind: "paused" as const, message: "paused" as const},
    {kind: "stale" as const, message: "no telemetry for 9s"},
    {kind: "unreachable" as const, message: "console unreachable - values are last known"},
  ])("says $message", (freshness) => {
    render(<StaleBanner freshness={freshness} />);

    expect(screen.getByText(freshness.message)).toBeInTheDocument();
  });
});

describe("ErrorText", () => {
  it("renders nothing when there is no error", () => {
    const {container} = render(<ErrorText message="" />);

    expect(container.firstChild).toBeNull();
  });

  it("puts the console's own words on screen", () => {
    render(<ErrorText message="frequency out of band" />);

    expect(screen.getByText("frequency out of band")).toBeInTheDocument();
  });
});

describe("Tabs", () => {
  const tabs = [
    {id: "home", label: "Home"},
    {id: "settings", label: "Settings"},
    {id: "debug", label: "Debug"},
  ];

  it("marks the active tab", () => {
    render(<Tabs tabs={tabs} active="home" onSelect={() => {}} />);

    expect(screen.getByRole("tab", {name: "Home"})).toHaveAttribute("aria-selected", "true");
    expect(screen.getByRole("tab", {name: "Debug"})).toHaveAttribute("aria-selected", "false");
  });

  it("selects on click", async () => {
    const onSelect = vi.fn();
    render(<Tabs tabs={tabs} active="home" onSelect={onSelect} />);

    await userEvent.click(screen.getByRole("tab", {name: "Settings"}));

    expect(onSelect).toHaveBeenCalledWith("settings");
  });

  // The window is used with a yoke in one hand.
  it("moves with the arrow keys, and wraps", async () => {
    const onSelect = vi.fn();
    const {rerender} = render(<Tabs tabs={tabs} active="home" onSelect={onSelect} />);

    await userEvent.click(screen.getByRole("tab", {name: "Home"}));
    onSelect.mockClear();
    await userEvent.keyboard("{ArrowRight}");
    expect(onSelect).toHaveBeenCalledWith("settings");

    rerender(<Tabs tabs={tabs} active="home" onSelect={onSelect} />);
    onSelect.mockClear();
    await userEvent.keyboard("{ArrowLeft}");
    expect(onSelect).toHaveBeenCalledWith("debug");
  });
});

describe("TabPanel", () => {
  // The answer to "React costs frames": the expensive views do not exist while
  // the pilot is on Home.
  it("mounts only the active panel", () => {
    render(
      <>
        <TabPanel id="home" active="home">
          <p>flying</p>
        </TabPanel>
        <TabPanel id="debug" active="home">
          <p>debugging</p>
        </TabPanel>
      </>,
    );

    expect(screen.getByText("flying")).toBeInTheDocument();
    expect(screen.queryByText("debugging")).toBeNull();
  });
});

describe("FieldRow", () => {
  it("hands over the trimmed value and clears", async () => {
    const onSubmit = vi.fn();
    render(<FieldRow label="Tune COM1" submitLabel="Tune" onSubmit={onSubmit} />);

    const input = screen.getByLabelText("Tune COM1");
    await userEvent.type(input, "  118.800  ");
    await userEvent.click(screen.getByRole("button", {name: "Tune"}));

    expect(onSubmit).toHaveBeenCalledWith("118.800");
    expect(input).toHaveValue("");
  });

  it("keeps the value for a field that shows a current setting", async () => {
    render(
      <FieldRow
        label="SimBrief pilot ID"
        submitLabel="Save"
        initialValue="123456"
        clearOnSubmit={false}
        onSubmit={() => {}}
      />,
    );

    await userEvent.click(screen.getByRole("button", {name: "Save"}));

    expect(screen.getByLabelText("SimBrief pilot ID")).toHaveValue("123456");
  });

  it("can be disabled while the console is unreachable", () => {
    render(<FieldRow label="Tune COM1" submitLabel="Tune" disabled onSubmit={() => {}} />);

    expect(screen.getByRole("button", {name: "Tune"})).toBeDisabled();
  });
});

describe("BayTable", () => {
  function row(overrides: Partial<BayRow> = {}): BayRow {
    return {flightId: "flight-1", callsign: "GABCD", ...overrides};
  }

  it("says so plainly when nobody is connected", () => {
    render(<BayTable rows={[]} />);

    expect(screen.getByText("No aircraft connected.")).toBeInTheDocument();
  });

  it("shows what the next controller would need to know", () => {
    render(
      <BayTable
        rows={[
          row({
            lifecycle: "taxi_out",
            service: "ground",
            airport: "CYOW",
            frequencyMhz: 121.9,
            onGround: true,
            hasTelemetry: true,
            telemetryAgeSeconds: 1.4,
          }),
        ]}
      />,
    );

    expect(screen.getByText("GABCD")).toBeInTheDocument();
    expect(screen.getByText("taxi_out")).toBeInTheDocument();
    expect(screen.getByText("121.900")).toBeInTheDocument();
    expect(screen.getByText("on ground")).toBeInTheDocument();
    expect(screen.getByText("1s")).toBeInTheDocument();
  });

  // The two ways a turn sits unfinished, plus authority the traffic picture
  // cannot show.
  it("lists what is outstanding", () => {
    render(
      <BayTable
        rows={[row({readbackPending: true, awaitingDelivery: true, heldRunway: "07"})]}
      />,
    );

    expect(screen.getByText("readback, delivery, rwy 07")).toBeInTheDocument();
  });

  // A row that stops ageing is a connection that has gone away without saying
  // so, and no feed at all must not read as a fresh zero.
  it("distinguishes no feed from a fresh one", () => {
    render(<BayTable rows={[row({hasTelemetry: false, telemetryAgeSeconds: 0})]} />);

    expect(screen.getByText("no feed")).toBeInTheDocument();
  });
});
