import {ACTIONS} from "../bridge/actions";
import {EVENTS} from "../bridge/types";
import type {BayView, FlightPlanView, PanelTelemetry, SessionView} from "../bridge/views";
import {CommLog} from "../components/CommLog";
import {Composer} from "../components/Composer";
import {ErrorText} from "../components/ErrorText";
import {Tile} from "../components/Tile";
import {
  formatAltitude,
  formatDroppedEvents,
  formatFacilityCount,
  formatFlightPlanDetail,
  formatFlightPlanRoute,
  formatFrequency,
  formatGroundspeed,
  formatStandby,
  formatTraffic,
  UNKNOWN,
} from "../domain/format";
import {useConsole} from "../state/ConsoleProvider";
import {useCommLog, useConsoleAction, useConsoleEvent, useConsoleStatus} from "../state/hooks";

// What the pilot flies with.
//
// Own-ship across the top, the radio filling everything left, and the things
// you do to the radio docked to the bottom of it. Everything that exists to
// explain the console rather than fly the aeroplane is on Debug, and everything
// set once is on Settings.
export function HomeTab() {
  const {store} = useConsole();
  const telemetry = useConsoleEvent<PanelTelemetry>(EVENTS.telemetry);
  const session = useConsoleEvent<SessionView>(EVENTS.flightSnapshot);
  const bay = useConsoleEvent<BayView>(EVENTS.facilitySnapshot);
  const plan = useConsoleEvent<FlightPlanView>(EVENTS.flightPlan);
  const entries = useCommLog();
  const status = useConsoleStatus();
  const {run, error, clearError, busy} = useConsoleAction();

  function tune(value: string): void {
    const mhz = Number.parseFloat(value);
    if (!Number.isFinite(mhz)) {
      // The only check the panel makes, and only because an unparseable string
      // is not a frequency at all. Whether 118.8 is allowed here is the
      // console's answer to give.
      return;
    }
    clearError();
    void run(ACTIONS.tuneRadio, {mhz, target: "active"}).catch(() => {});
  }

  function transmit(text: string): void {
    clearError();
    void run(ACTIONS.submitText, {text}).catch(() => {});
  }

  // One press, no typing: the console already remembers this aircraft's pilot
  // ID, and a pilot who wants their plan on the panel should not have to visit
  // Settings to say so.
  function importPlan(): void {
    clearError();
    void run(ACTIONS.refreshFlightPlan, {pilotId: ""})
      .then((payload) => store.setEvent(EVENTS.flightPlan, payload))
      .catch(() => {});
  }

  return (
    <>
      <section className="grid" aria-label="Aircraft status">
        <Tile label="Flight" value={session?.callsign || "waiting"} detail={session?.lifecycle} />
        <Tile
          label="COM1"
          value={formatFrequency(telemetry?.frequencyMhz)}
          detail={formatStandby(telemetry?.standbyMhz)}
        />
        <Tile
          label="Altitude"
          value={telemetry ? formatAltitude(telemetry) : UNKNOWN}
          detail={telemetry ? formatGroundspeed(telemetry.groundspeedKts) : undefined}
        />
        <Tile
          label="Traffic"
          value={telemetry ? formatTraffic(telemetry) : UNKNOWN}
          detail={telemetry?.airport}
        />
        <Tile label="Facility" value={formatFacilityCount(bay)} />
        <Tile
          label="Filed plan"
          value={formatFlightPlanRoute(plan)}
          detail={formatFlightPlanDetail(plan)}
          action={{
            label: plan?.hasPlan ? "Reload" : "SimBrief",
            onClick: importPlan,
            disabled: busy,
          }}
        />
      </section>

      <section className="feed radio" aria-label="Radio">
        <div className="feed-head">
          <span className="label">Radio</span>
          <span className="pulse" aria-hidden="true" />
          <span className="label">{formatDroppedEvents(status.droppedEvents)}</span>
        </div>

        <CommLog entries={entries} ownCallsign={session?.callsign} />

        <ErrorText message={error} />
        <Composer disabled={busy} onSend={transmit} onTune={tune} />
      </section>
    </>
  );
}
