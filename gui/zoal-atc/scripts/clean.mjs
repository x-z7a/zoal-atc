import {rm} from "node:fs/promises";
import {fileURLToPath} from "node:url";
import {join} from "node:path";

const root = fileURLToPath(new URL("..", import.meta.url));
await rm(join(root, "dist"), {recursive: true, force: true});
