import {StrictMode} from "react";
import {createRoot} from "react-dom/client";

import {App} from "./app/App";
import {ConsoleProvider} from "./state/ConsoleProvider";
import "./styles/index.css";

const container = document.getElementById("root");
if (container) {
  createRoot(container).render(
    <StrictMode>
      <ConsoleProvider>
        <App search={window.location.search} />
      </ConsoleProvider>
    </StrictMode>,
  );
} else {
  // The plugin greps X-Plane's Log.txt for this prefix when the panel comes up
  // blank, so a missing mount point says so there rather than only in a CEF
  // console nobody has open.
  console.error("[zoal-atc panel] no #root element; index.html and main.js disagree");
}
