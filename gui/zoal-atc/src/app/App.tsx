import {useState} from "react";

import {StatusPill} from "../components/StatusPill";
import {StaleBanner} from "../components/StaleBanner";
import {Tabs, TabPanel, type TabDefinition} from "../components/Tabs";
import {describeStatus, isConsoleReachable} from "../domain/status";
import {readShellParams} from "../domain/shell";
import {useConsole} from "../state/ConsoleProvider";
import {useConsoleStatus, useTelemetryFreshness} from "../state/hooks";
import {DebugTab} from "../views/DebugTab";
import {HomeTab} from "../views/HomeTab";
import {SettingsTab} from "../views/SettingsTab";

const TABS: readonly TabDefinition[] = [
  {id: "home", label: "Home"},
  {id: "settings", label: "Settings"},
  {id: "debug", label: "Debug"},
];

type Props = {
  search: string;
};

export function App({search}: Props) {
  const shell = readShellParams(search);
  const {boot, bootDetail} = useConsole();
  const status = useConsoleStatus();
  const freshness = useTelemetryFreshness();

  // Home by default and held in memory only. The window is hidden and shown
  // rather than reloaded, so there is nothing to persist, and localStorage on a
  // file:// origin is a risk not worth taking for a tab index.
  const [active, setActive] = useState("home");

  const reachable = isConsoleReachable(status);
  const connection =
    boot === "no-host"
      ? "no Skyscript host"
      : boot === "host-error"
        ? "host error"
        : boot === "waiting-for-host"
          ? "waiting for host"
          : describeStatus(status);

  // Everything on screen is last-known while the console is gone, so the
  // readouts dim as a whole rather than one line saying so. The radio log keeps
  // full contrast: what was already said is still true.
  const className = `panel${reachable ? "" : " console-unreachable"}`;

  return (
    <main className={className}>
      <header className="mast">
        <div>
          <p className="eyebrow">zoal-atc {shell.pluginVersion}</p>
          <h1>Radio Panel</h1>
        </div>
        <div className="status-stack" aria-label="System status">
          <StatusPill ready={shell.skyscript === "ready"}>Skyscript {shell.skyscript}</StatusPill>
          <StatusPill ready={reachable}>Console {connection}</StatusPill>
        </div>
      </header>

      {boot === "no-host" ? (
        // Opened outside X-Plane. Say so, and say where to look, rather than
        // sitting on "connecting" forever.
        <p className="boot-error">
          This page cannot reach the X-Plane panel bridge. Check the browser console and
          X-Plane's Log.txt for [zoal-atc panel] and [zoal-atc gui] lines.
        </p>
      ) : (
        <>
          <Tabs tabs={TABS} active={active} onSelect={setActive} />
          <StaleBanner freshness={freshness} />
          {boot === "host-error" ? (
            <p className="boot-error">Skyscript host error: {bootDetail}</p>
          ) : null}

          <TabPanel id="home" active={active}>
            <HomeTab />
          </TabPanel>
          <TabPanel id="settings" active={active}>
            <SettingsTab />
          </TabPanel>
          <TabPanel id="debug" active={active}>
            <DebugTab pluginVersion={shell.pluginVersion} skyscript={shell.skyscript} />
          </TabPanel>
        </>
      )}
    </main>
  );
}
