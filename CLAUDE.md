# CLAUDE.md

Working brief for Claude Code in zoal-mp. Read `docs/design.md` and
`docs/protocol.md` first.

## What this is

An X-Plane plugin that draws other pilots and console-controlled AI traffic
with XPMP2, fed by the ZOAL ATC console. It is a staging repo, to be merged into
`~/git/zoal-atc`, so its layout, style and test conventions mirror that repo's.
The console lives in `~/git/atc` and has its own CLAUDE.md, whose rules apply to
any console-side work.

## Rules

- **The plugin decides nothing about ATC or traffic.** It renders what the
  console sends and reports what the sim is doing.
- **The core stays SDK-free.** Anything with behaviour goes in
  `include/zoal_mp` + `src/{traffic,ownship}` with tests in `tests/`.
  `src/xplane` is wiring only.
- **Threading.** The registry and every XPMP2 object belong to the sim's main
  thread. Network threads exchange only parsed frames and encoded strings.
- **Wire changes are additive and negotiated** (`traffic_v1`). Mirror any
  change in `docs/protocol.md` and `tools/mockconsole`.
- **Don't fork zoal-atc's transport.** It is compiled from `ZOAL_ATC_SOURCE`.
  Fix it there.
- **Pins.** XPMP2 is pinned by commit in `cmake/XPMP2.cmake`.

## Working

- `make gate` must pass before you call work done (core tests + `go vet`).
- `make plugin` must still build after any change to `src/xplane`.
- **Use qwen offload for implementation.** Claude designs: interfaces,
  skeletons, and per-file specs in `.qwen-offload/specs/`. The local qwen3
  workers write the files (`.claude/skills/qwen-offload`), and Claude verifies.
  Keep for yourself only design, cross-repo API checks and small edits.

## Manual testing

The user tests by running two X-Plane instances on one machine against the
real console (hosted, or the local one on Docker at `ws://127.0.0.1:8765/plugin`,
set via `url` in each install's `zoal-mp.cfg`). Each sim should see the other as
a green participant label and never itself.

- Each install keeps its own `plugins/zoal-mp/installation-id`. When one
  install is copied from the other, delete the copy's file, or the console
  sees one flight (takeover / `<id>#2`), not two.
- Both installs need a CSL package in `plugins/zoal-mp/Resources/CSL`.
- The console must serve `traffic_v1` first; until it does, nothing appears.
  `tools/mockconsole` (:8766) is the stand-in.
- Don't load zoal-atc in the same sims before the merge: each sim then
  becomes two flights (docs/design.md, open questions).
