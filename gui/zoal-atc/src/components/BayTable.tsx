import type {BayRow} from "../bridge/views";
import {formatFrequency} from "../domain/format";

type Props = {
  rows: readonly BayRow[];
};

function age(seconds: number | undefined, hasTelemetry: boolean | undefined): string {
  if (!hasTelemetry) {
    return "no feed";
  }
  return `${Math.round(seconds ?? 0)}s`;
}

// groupByFacility splits the bay into the fields being worked.
//
// Two aircraft at one airport are one facility's problem; two at different
// airports are separate organisations that share nothing and coordinate
// nothing. Shown as a single flat list they read as one controller working
// both, which is the assumption this panel most easily hides.
//
// Rows keep the order the console sent them in, and the groups follow the order
// the fields first appear, so nothing reshuffles under a reader between frames.
export function groupByFacility(rows: readonly BayRow[]): [string, BayRow[]][] {
  const groups = new Map<string, BayRow[]>();
  for (const row of rows) {
    const key = row.facility || row.airport || "unknown";
    const existing = groups.get(key);
    if (existing) {
      existing.push(row);
    } else {
      groups.set(key, [row]);
    }
  }
  return [...groups.entries()];
}

// Every aircraft this console has on frequency, grouped by the field working it.
//
// Awareness, never a second controller station: the panel only ever displays
// this (phase 23 invariants). The console already sends every field here on
// each facility_snapshot -- the pre-React panel received all of it and rendered
// the count alone.
export function BayTable({rows}: Props) {
  if (rows.length === 0) {
    return <p className="bay-empty">No aircraft connected.</p>;
  }

  const groups = groupByFacility(rows);

  return (
    <div className="bay-scroll">
      <table className="bay">
        <thead>
          <tr>
            <th>Callsign</th>
            <th>Phase</th>
            <th>Service</th>
            <th>Nearest</th>
            <th>COM1</th>
            <th>Altitude</th>
            <th>Owes</th>
            <th>Feed</th>
          </tr>
        </thead>
        {groups.map(([facility, facilityRows]) => (
        <tbody key={facility}>
          {/* Named only when there is more than one: a single-field console
              would otherwise gain a header row that says nothing. */}
          {groups.length > 1 ? (
            <tr className="bay-facility">
              <th colSpan={8} scope="colgroup">
                {facility} · {facilityRows.length}
              </th>
            </tr>
          ) : null}
          {facilityRows.map((row) => (
            <tr key={row.flightId} className={row.active ? "bay-active" : ""}>
              <td>{row.callsign || row.flightId}</td>
              <td>{row.lifecycle || "—"}</td>
              <td>{row.service || "—"}</td>
              <td>{row.airport || "—"}</td>
              <td>{formatFrequency(row.frequencyMhz)}</td>
              <td>
                {row.onGround
                  ? "on ground"
                  : `${Math.round(row.altitudeFt ?? 0).toLocaleString("en-US")} ft`}
              </td>
              {/* What the aircraft owes and what the console owes it: the two
                  ways a turn sits unfinished. HeldRunway is authority the
                  traffic picture cannot show -- between the clearance and the
                  roll, nothing else says the runway is spoken for. */}
              <td>
                {[
                  row.readbackPending ? "readback" : "",
                  row.awaitingDelivery ? "delivery" : "",
                  row.heldRunway ? `rwy ${row.heldRunway}` : "",
                ]
                  .filter(Boolean)
                  .join(", ") || "—"}
              </td>
              <td>{row.paused ? "paused" : age(row.telemetryAgeSeconds, row.hasTelemetry)}</td>
            </tr>
          ))}
        </tbody>
        ))}
      </table>
    </div>
  );
}
