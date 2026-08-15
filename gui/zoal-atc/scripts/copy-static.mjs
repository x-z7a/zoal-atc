// Copy the one static file the bundler does not produce.
//
// Everything else -- JS and CSS alike -- goes through esbuild, so there is no
// list here to forget to update. That mattered: this script used to name each
// static file, and a new one was invisible until someone remembered to add it.
import {copyFile, mkdir} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");

await mkdir(path.join(root, "dist"), {recursive: true});
await copyFile(path.join(root, "src/index.html"), path.join(root, "dist/index.html"));
