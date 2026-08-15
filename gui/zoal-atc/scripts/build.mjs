// Bundle the panel into the files the release manifest checks for.
//
// The page loads from file:// inside X-Plane's CEF, so the output is a classic
// IIFE rather than an ES module: no module resolution, no CORS surface, one
// <script> tag. It is deliberately one JS file -- a second one that nothing
// verifies is how a release ships a panel that throws on load, which is exactly
// what bridge.js was until phase 24 (scripts/verify-release-tree.sh listed
// index.html, main.js and styles.css, never bridge.js).
import {build} from "esbuild";
import {readFile, stat} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");

// CEF draws inside the sim's frame budget and this bundle is parsed every time
// the window opens. The ceiling is not a style rule: it is the thing that
// notices when a convenience dependency quietly doubles the panel. Raise it
// deliberately, in a commit that says why.
const MAX_BUNDLE_BYTES = 300 * 1024;

await build({
  entryPoints: [path.join(root, "src/main.tsx")],
  outdir: path.join(root, "dist"),
  entryNames: "[name]",
  bundle: true,
  format: "iife",
  target: "es2020",
  minify: true,
  jsx: "automatic",
  logLevel: "warning",
  // React branches on this at module scope. Left undefined it does not fall
  // back to development -- it throws on `process`, and the panel is a blank
  // window with one line in the CEF console.
  define: {"process.env.NODE_ENV": '"production"'},
});

const bundle = path.join(root, "dist/main.js");
const {size} = await stat(bundle);
const kib = (size / 1024).toFixed(1);
if (size > MAX_BUNDLE_BYTES) {
  console.error(
    `dist/main.js is ${kib} KiB, over the ${(MAX_BUNDLE_BYTES / 1024).toFixed(0)} KiB budget.`,
  );
  process.exit(1);
}
console.log(`dist/main.js ${kib} KiB (budget ${(MAX_BUNDLE_BYTES / 1024).toFixed(0)} KiB)`);

// An unreachable console dims and un-clicks the panel's control rows, which is
// honest for every value the console owns and catastrophic for the one row the
// *plugin* answers: the access token, which exists to be usable while the
// console is unreachable, because it is how the console becomes reachable.
//
// This is checked on the shipped stylesheet rather than in a unit test because
// jsdom never applies it -- the bug that reached a cockpit had `disabled={false}`
// on every control and was invisible to the whole React suite. `.control-row-local`
// (FieldRow's `local` prop) is the exemption.
const css = await readFile(path.join(root, "dist/main.css"), "utf8");
const deadening = css
  .split("}")
  .filter(
    (rule) =>
      rule.includes(".console-unreachable") &&
      rule.includes(".control-row") &&
      rule.includes("pointer-events:none"),
  );
const unexempt = deadening.filter((rule) => !rule.includes(":not(.control-row-local)"));
if (deadening.length === 0 || unexempt.length > 0) {
  console.error(
    deadening.length === 0
      ? "dist/main.css: the console-unreachable control-row rule is gone; if that is intended, drop this check."
      : `dist/main.css: an unreachable console would deaden a plugin-answered row:\n  ${unexempt.join("}\n  ")}}`,
  );
  process.exit(1);
}
console.log("dist/main.css: plugin-answered control rows survive an unreachable console");
