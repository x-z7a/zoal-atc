import {StrictMode} from "react";
import {createRoot} from "react-dom/client";

import {App} from "./app/App";
import {readShellParams} from "./domain/shell";
import {applyTheme, cachedTheme} from "./domain/theme";
import {HudApp} from "./hud/HudApp";
import {ConsoleProvider} from "./state/ConsoleProvider";
import "./styles/index.css";

const container = document.getElementById("root");
// One bundle, two windows. The HUD's window is transparent wherever the page
// paints nothing, so its document must not paint a background at all -- which
// is decided on <html>, before React draws anything over it.
const shell = readShellParams(window.location.search);
if (shell.view === "hud") {
  document.documentElement.classList.add("hud-window");
}
// The last theme this browser saw, before the first paint; the plugin's stored
// one replaces it as soon as the bridge is up (useThemeSync).
applyTheme(cachedTheme());
if (container) {
  createRoot(container).render(
    <StrictMode>
      <ConsoleProvider>
        {shell.view === "hud" ? <HudApp /> : <App search={window.location.search} />}
      </ConsoleProvider>
    </StrictMode>,
  );
} else {
  // The plugin greps X-Plane's Log.txt for this prefix when the panel comes up
  // blank, so a missing mount point says so there rather than only in a CEF
  // console nobody has open.
  console.error("[zoal-atc panel] no #root element; index.html and main.js disagree");
}
