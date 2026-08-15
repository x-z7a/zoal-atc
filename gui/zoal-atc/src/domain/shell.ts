// The two facts the plugin passes the page in its URL.
//
// panel_paths.cpp builds the homepage as
//   file:///.../apps/zoal-atc/index.html?plugin_version=<ver>&skyscript=ready
// and those are the only query parameters the page reads. Taking the query
// string as an argument rather than reaching for window.location is what makes
// this testable without a DOM.

export type ShellParams = {
  pluginVersion: string;
  skyscript: string;
};

export function readShellParams(search: string): ShellParams {
  const params = new URLSearchParams(search);
  return {
    pluginVersion: params.get("plugin_version") || "unknown",
    skyscript: params.get("skyscript") || "ready",
  };
}
