// The panel's colour themes.
//
// Both windows -- the panel and the HUD -- are separate pages, so the choice is
// stored by the plugin (panel_theme in zoal_atc.cfg, checked against the same
// names in panel_theme.cpp) and each page applies it to <html> as
// data-zoal-theme. tokens.css holds the colours.
//
// The page also keeps the last theme in localStorage, and applies that before
// React draws anything, so a window does not flash the default while the
// plugin's answer is on its way. The plugin's value always wins.

export const THEMES = [
  {id: "graphite", label: "Graphite", description: "Warm dark glass. The default."},
  {id: "daylight", label: "Daylight", description: "Light glass for a bright cockpit."},
  {id: "night", label: "Night", description: "Dim and low-glare, for flying in the dark."},
] as const;

export type ThemeId = (typeof THEMES)[number]["id"];

export const DEFAULT_THEME: ThemeId = "graphite";

const STORAGE_KEY = "zoal-atc.theme";

export function isThemeId(value: unknown): value is ThemeId {
  return THEMES.some((theme) => theme.id === value);
}

// themeOrDefault reads a value from anywhere -- the plugin, storage, a test --
// and always answers something drawable.
export function themeOrDefault(value: unknown): ThemeId {
  return isThemeId(value) ? value : DEFAULT_THEME;
}

export function cachedTheme(): ThemeId {
  try {
    return themeOrDefault(window.localStorage.getItem(STORAGE_KEY));
  } catch {
    return DEFAULT_THEME;
  }
}

export function currentTheme(): ThemeId {
  return themeOrDefault(document.documentElement.dataset.zoalTheme);
}

export function applyTheme(theme: ThemeId): void {
  document.documentElement.dataset.zoalTheme = theme;
  try {
    window.localStorage.setItem(STORAGE_KEY, theme);
  } catch {
    // Storage is a convenience against a flash on load, never the record.
  }
}
