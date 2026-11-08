// The facts the plugin passes the page in its URL.
//
// panel_paths.cpp builds the homepage as
//   file:///.../apps/zoal-atc/index.html?plugin_version=<ver>&skyscript=ready
// and skyscript_panel.cpp adds &view=hud for the HUD's bar and &view=hud-feed for
// its transmissions. Those are the only query parameters the page reads. Taking the query
// string as an argument rather than reaching for window.location is what makes
// this testable without a DOM.

// Which page this window is. The panel and the HUD are one bundle, so the
// plugin says which to draw rather than shipping two.
export type ShellView = "panel" | "hud" | "hud-feed";

export type ShellParams = {
  pluginVersion: string;
  skyscript: string;
  view: ShellView;
};

function readView(view: string | null): ShellView {
  return view === "hud" || view === "hud-feed" ? view : "panel";
}

export function readShellParams(search: string): ShellParams {
  const params = new URLSearchParams(search);
  return {
    pluginVersion: params.get("plugin_version") || "unknown",
    skyscript: params.get("skyscript") || "ready",
    view: readView(params.get("view")),
  };
}
