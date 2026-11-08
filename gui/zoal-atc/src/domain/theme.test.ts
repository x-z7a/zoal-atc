import {afterEach, describe, expect, it} from "vitest";

import {applyTheme, cachedTheme, currentTheme, isThemeId, themeOrDefault} from "./theme";

describe("themes", () => {
  afterEach(() => {
    delete document.documentElement.dataset.zoalTheme;
    window.localStorage.clear();
  });

  it("knows the three it ships", () => {
    expect(isThemeId("graphite")).toBe(true);
    expect(isThemeId("daylight")).toBe(true);
    expect(isThemeId("night")).toBe(true);
    expect(isThemeId("solarized")).toBe(false);
  });

  // A stale or hand-edited value must still draw something.
  it("falls back to graphite for anything else", () => {
    expect(themeOrDefault(undefined)).toBe("graphite");
    expect(themeOrDefault("Daylight")).toBe("graphite");
    expect(themeOrDefault("night")).toBe("night");
  });

  it("applies to <html> and remembers it for the next load", () => {
    applyTheme("daylight");

    expect(document.documentElement.dataset.zoalTheme).toBe("daylight");
    expect(currentTheme()).toBe("daylight");
    expect(cachedTheme()).toBe("daylight");
  });

  it("starts from graphite with nothing remembered", () => {
    expect(cachedTheme()).toBe("graphite");
    expect(currentTheme()).toBe("graphite");
  });
});
