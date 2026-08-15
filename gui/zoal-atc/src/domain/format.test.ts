import {describe, expect, it} from "vitest";

import type {PanelTelemetry} from "../bridge/views";
import {
  formatAltitude,
  formatDroppedEvents,
  formatFacilityCount,
  formatFlightPlanDetail,
  formatFlightPlanRoute,
  formatFrequency,
  formatGroundspeed,
  formatPosition,
  formatTraffic,
  positionTone,
  UNKNOWN,
} from "./format";

function telemetry(overrides: Partial<PanelTelemetry> = {}): PanelTelemetry {
  return {
    altitudeFtMsl: 0,
    heightAglFt: 0,
    groundspeedKts: 0,
    headingDeg: 0,
    onGround: true,
    trafficCount: 0,
    ...overrides,
  };
}

describe("formatFrequency", () => {
  it.each([
    {mhz: 118.8, expected: "118.800"},
    {mhz: 119.4, expected: "119.400"},
    {mhz: 135.275, expected: "135.275"},
    {mhz: undefined, expected: UNKNOWN},
    // A zero frequency is not a frequency, it is an unset radio.
    {mhz: 0, expected: UNKNOWN},
  ])("renders $mhz as $expected", ({mhz, expected}) => {
    expect(formatFrequency(mhz)).toBe(expected);
  });
});

describe("formatAltitude", () => {
  it("says on ground rather than a number when the aircraft is on the ground", () => {
    expect(formatAltitude(telemetry({onGround: true, altitudeFtMsl: 374}))).toBe("on ground");
  });

  it.each([
    {altitudeFtMsl: 3000, expected: "3,000 ft"},
    {altitudeFtMsl: 12500, expected: "12,500 ft"},
    {altitudeFtMsl: 900.4, expected: "900 ft"},
  ])("renders $altitudeFtMsl as $expected", ({altitudeFtMsl, expected}) => {
    expect(formatAltitude(telemetry({onGround: false, altitudeFtMsl}))).toBe(expected);
  });
});

describe("formatGroundspeed", () => {
  it.each([
    {knots: 0, expected: "0 kt"},
    {knots: 121.6, expected: "122 kt"},
  ])("renders $knots as $expected", ({knots, expected}) => {
    expect(formatGroundspeed(knots)).toBe(expected);
  });
});

// An absent feed is neither occupied nor clear (phase 21). A bare "0 contacts"
// would read as an empty sky, which is the one thing the panel must not say
// when it does not know.
describe("formatTraffic", () => {
  it("counts contacts only when surveillance is current", () => {
    expect(formatTraffic(telemetry({surveillance: "current", trafficCount: 3}))).toBe(
      "3 contacts",
    );
  });

  it.each([
    {surveillance: "stale", expected: "stale"},
    {surveillance: "unavailable", expected: "unavailable"},
    {surveillance: undefined, expected: "unavailable"},
  ])("says $expected rather than a count when surveillance is $surveillance", (testCase) => {
    expect(
      formatTraffic(telemetry({surveillance: testCase.surveillance, trafficCount: 0})),
    ).toBe(testCase.expected);
  });

  it("never reports an empty sky from a feed it does not have", () => {
    expect(formatTraffic(telemetry({surveillance: "unavailable", trafficCount: 0}))).not.toBe(
      "0 contacts",
    );
  });
});

describe("formatFacilityCount", () => {
  it.each([
    {count: 0, expected: "0 aircraft"},
    {count: 1, expected: "1 aircraft"},
    {count: 4, expected: "4 aircraft"},
  ])("renders $count as $expected", ({count, expected}) => {
    expect(formatFacilityCount({count})).toBe(expected);
  });

  it("says none rather than nothing before the first snapshot", () => {
    expect(formatFacilityCount(undefined)).toBe("0 aircraft");
  });
});

describe("flight plan", () => {
  it("shows origin and destination", () => {
    const plan = {hasPlan: true, origin: "CYOW", destination: "CYGK"};
    expect(formatFlightPlanRoute(plan)).toBe("CYOW -> CYGK");
  });

  it("says filed when a plan exists without airports", () => {
    expect(formatFlightPlanRoute({hasPlan: true})).toBe("filed");
  });

  it.each([{plan: undefined}, {plan: {hasPlan: false}}])(
    "shows the placeholder when there is no plan",
    ({plan}) => {
      expect(formatFlightPlanRoute(plan)).toBe(UNKNOWN);
      expect(formatFlightPlanDetail(plan)).toBe("");
    },
  );

  it("joins the details that are present", () => {
    expect(
      formatFlightPlanDetail({
        hasPlan: true,
        aircraftIcao: "C172",
        cruiseAltFt: 5500,
        firstFix: "PLATO",
        route: "V300 YOW",
      }),
    ).toBe("C172 | 5,500 ft | first PLATO | V300 YOW");
  });

  it("omits the details that are missing rather than leaving empty separators", () => {
    expect(formatFlightPlanDetail({hasPlan: true, aircraftIcao: "C172"})).toBe("C172");
  });
});

describe("formatPosition", () => {
  it.each([
    {position: "clearance_delivery", expected: "Clearance Delivery"},
    {position: "ground", expected: "Ground"},
    {position: "tower", expected: "Tower"},
    {position: "departure", expected: "Departure"},
    {position: "approach", expected: "Approach"},
    {position: "center", expected: "Center"},
    {position: "oceanic", expected: "Oceanic"},
    {position: "unicom_ctaf", expected: "UNICOM"},
  ])("names $position", ({position, expected}) => {
    expect(formatPosition(position)).toBe(expected);
  });

  // No seat is not a controller talking, and a position this panel predates
  // should still read as something true.
  it.each([
    {position: undefined},
    {position: ""},
    {position: "none"},
    {position: "flight_service_station"},
  ])("falls back to ATC for $position", ({position}) => {
    expect(formatPosition(position)).toBe("ATC");
  });
});

describe("positionTone", () => {
  // The seats a pilot switches between most should not share a colour.
  it("gives the surface, the tower and the radar seats distinct tones", () => {
    expect(positionTone("ground")).not.toBe(positionTone("tower"));
    expect(positionTone("tower")).not.toBe(positionTone("approach"));
    expect(positionTone("clearance_delivery")).not.toBe(positionTone("ground"));
  });

  it("keeps one tone for the radar seats, which are the same job", () => {
    expect(positionTone("approach")).toBe(positionTone("center"));
    expect(positionTone("center")).toBe(positionTone("departure"));
  });

  it("falls back rather than going unstyled", () => {
    expect(positionTone(undefined)).toBe("atc");
    expect(positionTone("flight_service_station")).toBe("atc");
  });
});

describe("formatDroppedEvents", () => {
  it("says nothing when nothing was dropped", () => {
    expect(formatDroppedEvents(0)).toBe("");
  });

  it("reports a drop count", () => {
    expect(formatDroppedEvents(7)).toBe("7 updates dropped");
  });
});
