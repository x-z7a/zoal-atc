import {useEffect, useState} from "react";

import {ACTIONS, LOCAL_ACTIONS} from "../bridge/actions";
import {EVENTS} from "../bridge/types";
import {
  DEFAULT_NOTIFICATIONS,
  type ConnectionView,
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

  // The connection settings get their own runner so a failed console save does
  // not clear the token's error, or the other way round. They are answered by
  // the plugin, so the two never share a failure mode either.
  // Its `busy` is deliberately not read: see the token row below, which stays
  // usable while a save is in flight because it is the way back from a bad one.
  const {
    run: runLocal,
    error: localError,
    clearError: clearLocalError,
  } = useConsoleAction();
  const [connection, setConnection] = useState<ConnectionView | undefined>(undefined);

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

  // Read on the same terms: the Skyscript host has to be up, but the console
  // does not. This read answers while the socket is down, which is the whole
  // point of the section.
  useEffect(() => {
    if (boot !== "ready") {
      return;
    }
    let cancelled = false;
    void runLocal(LOCAL_ACTIONS.connectionSettings)
      .then((payload) => {
        if (!cancelled) {
          setConnection(payload as ConnectionView);
        }
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [boot, runLocal]);

  function saveToken(token: string): void {
    clearLocalError();
    void runLocal(LOCAL_ACTIONS.saveConnectionSettings, {token})
      .then((payload) => setConnection(payload as ConnectionView))
      .catch(() => {});
  }

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
      <h2 className="section-title">Connection</h2>

      <div className="setting">
        <span className="label">Console</span>
        <strong>{connection?.url || "—"}</strong>
        <p className="setting-help">
          Where this plugin connects. Fixed here — change it in{" "}
          <code>Output/preferences/zoal_atc.cfg</code> if you run your own console.
        </p>
      </div>

      <div className="setting">
        {/* Keyed on the loaded token so the field remounts with it once the
            plugin answers, rather than sitting empty over a stored value. */}
        <FieldRow
          key={connection ? "loaded" : "loading"}
          label="Access token"
          placeholder="Leave empty if the console is open"
          // Not plain "Save": the SimBrief row below has one, and two buttons
          // reading the same word on one screen is ambiguous to a screen reader
          // even where the layout makes it obvious to an eye.
          submitLabel="Save token"
          secret
          initialValue={connection?.token ?? ""}
          clearOnSubmit={false}
          // Deliberately not gated on the save in flight, unlike every other
          // row here. Saving a token drops the socket on purpose, so this is
          // the request most likely to lose its answer -- and locking the field
          // until an answer that is not coming would take away the only control
          // that repairs a mistyped credential. Re-saving is harmless: it
          // rewrites one line and re-dials.
          disabled={boot !== "ready"}
          onSubmit={saveToken}
        />
        <p className="setting-help">
          Authorises this aircraft with the console. Saving reconnects, so the status above tells
          you straight away whether it was accepted. Unlike everything else here this is stored by
          the plugin, not the console — it has to work while the console is unreachable, because it
          is how it becomes reachable. Save it empty to clear it.
        </p>
        <ErrorText message={localError} />
      </div>

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
