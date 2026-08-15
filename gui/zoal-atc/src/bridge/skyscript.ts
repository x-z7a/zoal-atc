// Finding the Skyscript host, and saying so clearly when it is not there.
//
// The panel comes up before the host object is injected, and it can also be
// opened in an ordinary browser where the host never arrives at all. Those are
// different situations and the diagnostics here exist so the difference is
// visible in X-Plane's Log.txt rather than only in a CEF console nobody has
// open.

import {PANEL_LOG_PREFIX, type SkyscriptHost} from "./types";

type WindowWithSkyscript = Window & {skyscript?: unknown};

let lastBadHostSummary = "";

export function panelWarn(message: string, details?: unknown): void {
  console.warn(PANEL_LOG_PREFIX, message, details ?? "");
}

export function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export function summarizeUnknown(value: unknown): string {
  if (value === undefined) {
    return "undefined";
  }
  if (value === null) {
    return "null";
  }
  const type = typeof value;
  if (type !== "object" && type !== "function") {
    return type;
  }
  const keys = Object.keys(value as Record<string, unknown>).slice(0, 10);
  return `${type}{${keys.join(",")}}`;
}

function isSkyscriptHost(value: unknown): value is SkyscriptHost {
  if (value === null || (typeof value !== "object" && typeof value !== "function")) {
    return false;
  }
  const host = value as Partial<SkyscriptHost>;
  return typeof host.postMessage === "function" && typeof host.onMessage === "function";
}

export function skyscriptDiagnostics(): Record<string, unknown> {
  const win = window as WindowWithSkyscript;
  return {
    href: window.location.href,
    readyState: document.readyState,
    hasSkyscriptProperty: "skyscript" in win,
    skyscript: summarizeUnknown(win.skyscript),
    skyscriptWindowKeys: Object.keys(window)
      .filter((key) => key.toLowerCase().includes("skyscript"))
      .slice(0, 20),
  };
}

export function currentSkyscriptHost(): SkyscriptHost | undefined {
  const candidate = (window as WindowWithSkyscript).skyscript;
  if (candidate === undefined) {
    return undefined;
  }
  if (isSkyscriptHost(candidate)) {
    return candidate;
  }
  // Warn once per distinct shape. A host that is present but wrong is a
  // different bug from one that never arrived, and repeating it every poll
  // would bury the line that says which.
  const summary = summarizeUnknown(candidate);
  if (summary !== lastBadHostSummary) {
    lastBadHostSummary = summary;
    panelWarn("window.skyscript exists but is not the expected host", skyscriptDiagnostics());
  }
  return undefined;
}

export function waitForSkyscriptHost(
  timeoutMs = 5000,
  intervalMs = 100,
): Promise<SkyscriptHost | undefined> {
  const immediate = currentSkyscriptHost();
  if (immediate) {
    return Promise.resolve(immediate);
  }

  return new Promise((resolve) => {
    const deadline = Date.now() + timeoutMs;
    const timer = window.setInterval(() => {
      const host = currentSkyscriptHost();
      if (host) {
        window.clearInterval(timer);
        resolve(host);
        return;
      }
      if (Date.now() >= deadline) {
        window.clearInterval(timer);
        panelWarn("Skyscript host did not appear before timeout", skyscriptDiagnostics());
        resolve(undefined);
      }
    }, intervalMs);
  });
}
