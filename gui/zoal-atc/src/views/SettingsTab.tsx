import {useEffect, useState} from "react";

import {ACTIONS} from "../bridge/actions";
import {EVENTS} from "../bridge/types";
import {
  DEFAULT_NOTIFICATIONS,
  type FlightPlanView,
  type NotificationsSetting,
  type SettingsView,
} from "../bridge/views";
import {ErrorText} from "../components/ErrorText";
import {FieldRow} from "../components/FieldRow";
import {Toggle} from "../components/Toggle";
import {formatFlightPlanDetail, formatFlightPlanRoute} from "../domain/format";
import {useConsole} from "../state/ConsoleProvider";
import {useConsoleAction, useConsoleEvent} from "../state/hooks";

const CORNERS: ReadonlyArray<{id: string; label: string}> = [
  {id: "top_left", label: "Top left"},
  {id: "top_right", label: "Top right"},
  {id: "bottom_left", label: "Bottom left"},
  {id: "bottom_right", label: "Bottom right"},
];

// What you set once and stop thinking about.
//
// Until now there was nowhere to put a preference, so preferences lived in
// zoal_atc.cfg -- whose own template says "there is no UI on your side to type
// it into". This is that UI.
//
// Settings belong to this aircraft, not to the console. With two pilots
// connected, one saving here does not move the other's.
export function SettingsTab() {
  const {store, boot} = useConsole();
  const plan = useConsoleEvent<FlightPlanView>(EVENTS.flightPlan);
  const {run, error, clearError, busy} = useConsoleAction();

  const [settings, setSettings] = useState<SettingsView | undefined>(undefined);

  // Read what is stored before offering to change it: a field that starts empty
  // when a value is remembered invites the pilot to retype what the console
  // already has, and to wonder which one it is using.
  useEffect(() => {
    if (boot !== "ready") {
      return;
    }
    let cancelled = false;
    void run(ACTIONS.settings)
      .then((payload) => {
        if (!cancelled) {
          setSettings(payload as SettingsView);
        }
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
    // run changes identity with the bridge; re-reading on that is correct.
  }, [boot, run]);

  const notifications = settings?.notifications ?? DEFAULT_NOTIFICATIONS;

  function save(next: SettingsView): void {
    clearError();
    void run(ACTIONS.saveSettings, {
      simbriefPilotId: next.simbriefPilotId ?? "",
      notifications: next.notifications ?? DEFAULT_NOTIFICATIONS,
    })
      .then((payload) => setSettings(payload as SettingsView))
      .catch(() => {});
  }

  function savePilotID(simbriefPilotId: string): void {
    save({...settings, simbriefPilotId});
  }

  // A toggle that has to be confirmed with a button is a toggle that gets left
  // half-set. Changing one saves it.
  function saveNotifications(change: Partial<NotificationsSetting>): void {
    save({...settings, notifications: {...notifications, ...change}});
  }

  function importNow(): void {
    clearError();
    void run(ACTIONS.refreshFlightPlan, {pilotId: ""})
      .then((payload) => store.setEvent(EVENTS.flightPlan, payload))
      .catch(() => {});
  }

  return (
    <section className="controls" aria-label="Settings">
      <h2 className="section-title">Flight plan</h2>

      <div className="setting">
        {/* Keyed on the loaded value so the field remounts with it once the
            console answers, rather than sitting empty at whatever it was
            before the read returned. */}
        <FieldRow
          key={settings?.simbriefPilotId ?? "loading"}
          label="SimBrief pilot ID"
          placeholder="Pilot ID"
          submitLabel="Save"
          inputMode="numeric"
          initialValue={settings?.simbriefPilotId ?? ""}
          clearOnSubmit={false}
          disabled={busy || boot !== "ready"}
          onSubmit={savePilotID}
        />
        <p className="setting-help">
          Remembered for this aircraft, so an import needs no ID. Two pilots connected to one
          console each keep their own.
        </p>
      </div>

      <div className="setting">
        <span className="label">Imported plan</span>
        <strong>{formatFlightPlanRoute(plan)}</strong>
        <p className="setting-help">{formatFlightPlanDetail(plan) || "No plan imported yet."}</p>
        <div className="control-row">
          <button type="button" onClick={importNow} disabled={busy || boot !== "ready"}>
            Import now
          </button>
        </div>
      </div>

      <h2 className="section-title">Notifications</h2>

      <div className="setting">
        <Toggle
          label="Tell me when ATC calls"
          checked={notifications.enabled}
          disabled={busy || boot !== "ready"}
          onChange={(enabled) => saveNotifications({enabled})}
        />
        <p className="setting-help">
          A transmission appears over the cockpit while this window is closed. Nothing appears
          while it is open — the radio log is already on screen.
        </p>
      </div>

      <div className="setting">
        <Toggle
          label="Play a sound"
          checked={notifications.sound}
          disabled={busy || boot !== "ready" || !notifications.enabled}
          onChange={(sound) => saveNotifications({sound})}
        />
      </div>

      <div className="setting">
        <label className="label" htmlFor="notification-corner">
          Corner
        </label>
        <select
          id="notification-corner"
          value={notifications.corner}
          disabled={busy || boot !== "ready" || !notifications.enabled}
          onChange={(event) => saveNotifications({corner: event.target.value})}
        >
          {CORNERS.map((corner) => (
            <option key={corner.id} value={corner.id}>
              {corner.label}
            </option>
          ))}
        </select>
      </div>

      <div className="setting">
        <label className="label" htmlFor="notification-timeout">
          Seconds on screen
        </label>
        <select
          id="notification-timeout"
          value={String(notifications.timeoutSecs)}
          disabled={busy || boot !== "ready" || !notifications.enabled}
          onChange={(event) =>
            saveNotifications({timeoutSecs: Number.parseFloat(event.target.value)})
          }
        >
          {[4, 8, 15, 30].map((seconds) => (
            <option key={seconds} value={seconds}>
              {seconds}
            </option>
          ))}
        </select>
        {/* There is no "until I dismiss it": with the window closed nothing on
            the toast can be clicked, so it would never leave. */}
        <p className="setting-help">
          A notification cannot be clicked while the window is closed, so it always goes away on
          its own.
        </p>
      </div>

      <ErrorText message={error} />
    </section>
  );
}
