import {describe, expect, it} from "vitest";

import {readShellParams} from "./shell";

describe("readShellParams", () => {
  const cases: Array<{name: string; search: string; pluginVersion: string; skyscript: string}> = [
    {
      name: "the URL the plugin actually builds",
      search: "?plugin_version=0.1.0&skyscript=ready",
      pluginVersion: "0.1.0",
      skyscript: "ready",
    },
    {
      name: "a development build with no version stamped",
      search: "?plugin_version=dev&skyscript=ready",
      pluginVersion: "dev",
      skyscript: "ready",
    },
    {
      name: "opened outside X-Plane, with no query at all",
      search: "",
      pluginVersion: "unknown",
      skyscript: "ready",
    },
    {
      name: "empty values fall back rather than rendering blank",
      search: "?plugin_version=&skyscript=",
      pluginVersion: "unknown",
      skyscript: "ready",
    },
    {
      name: "a host that reported something other than ready",
      search: "?plugin_version=0.1.0&skyscript=missing",
      pluginVersion: "0.1.0",
      skyscript: "missing",
    },
  ];

  for (const testCase of cases) {
    it(testCase.name, () => {
      expect(readShellParams(testCase.search)).toEqual({
        pluginVersion: testCase.pluginVersion,
        skyscript: testCase.skyscript,
      });
    });
  }
});
