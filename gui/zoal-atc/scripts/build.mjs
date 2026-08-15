// Bundle the panel into the files the release manifest checks for.
//
// The page loads from file:// inside X-Plane's CEF, so the output is a classic
// IIFE rather than an ES module: no module resolution, no CORS surface, one
// <script> tag. It is deliberately one JS file -- a second one that nothing
// verifies is how a release ships a panel that throws on load, which is exactly
// what bridge.js was until phase 24 (scripts/verify-release-tree.sh listed
// index.html, main.js and styles.css, never bridge.js).
import {build} from "esbuild";
import {stat} from "node:fs/promises";
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
