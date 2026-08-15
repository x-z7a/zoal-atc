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

// Every aircraft this facility has on frequency.
//
// Awareness, never a second controller station: the panel only ever displays
// this (phase 23 invariants). The console already sends every field here on
// each facility_snapshot -- the pre-React panel received all of it and rendered
// the count alone.
export function BayTable({rows}: Props) {
  if (rows.length === 0) {
    return <p className="bay-empty">No aircraft connected.</p>;
  }

  return (
    <div className="bay-scroll">
      <table className="bay">
        <thead>
          <tr>
            <th>Callsign</th>
            <th>Phase</th>
            <th>Service</th>
            <th>Field</th>
            <th>COM1</th>
            <th>Altitude</th>
            <th>Owes</th>
            <th>Feed</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((row) => (
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
      </table>
    </div>
  );
}
