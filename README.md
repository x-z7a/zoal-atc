# zoal-atc — the X-Plane plugin

`zoal-atc` is the X-Plane plugin half of the ZOAL AI air traffic control system.
It is a deliberately thin *edge* process: push-to-talk audio capture, simulator
data sampling, the in-sim panel, and transport to a `zoal-atc-console` server.

**It owns no ATC logic.** Every clearance, instruction and refusal is authorized
by the console's deterministic state machines. The plugin captures what the pilot
said and what the simulator is doing, sends both, and renders what comes back.

> **Not an approved operating procedure.** The phraseology, separation standards,
> airport procedures and authority rules this system produces are a software
> artifact. Validate them against your operating jurisdiction before any
> operational use, and do not present generated ATC behavior as operationally
> certified.

## Install

1. Download the release archive for your platform from
   [Releases](https://github.com/x-z7a/zoal-atc/releases) and unzip it.
2. Drop the `zoal-atc` folder into `<X-Plane>/Resources/plugins/`.
3. Launch X-Plane. In **Settings → Keyboard** (or Joystick), bind the command
   `zoal_atc/ptt` ("zoal-atc: Push-to-Talk") to your push-to-talk key or button.
4. Open the panel with **Plugins → zoal-atc → Toggle In-Sim Panel**, or bind
   `zoal_atc/gui_toggle`.

The archive contains the `.xpl`, the SkyScript runtime it links, and the in-sim
panel. It ships no CEF: since SkyScript v0.5.0 the library resolves the CEF
runtime that comes with X-Plane.

Out of the box the plugin talks to a console on `127.0.0.1:8765`. To point it at
a hosted console, see *Console endpoint* below.

## What it does

- **Push-to-talk capture.** An SDK-free `PushToTalkController` with finite states
  (`idle`, `recording`, `faulted`) and explicit outcomes for duplicate press,
  stray release, short transmission, max-duration cutoff and recorder failure.
  Microphone capture is cross-platform through `miniaudio`.
- **Telemetry sampling.** The starter dataref catalog sampled into `telemetry`
  frames, including TCAS traffic. The rate is **console-controlled**: the plugin
  decodes the `telemetry_control` frame and samples at the commanded interval,
  falling back to a local policy when no command has arrived or the connection
  drops.
- **Navdata.** Hash-first sending of scoped `apt.dat` blocks, CIFP, airspace and
  VRP data, answering `navdata_*_request`, plus METAR.
- **The in-sim panel.** A React + TypeScript app hosted by SkyScript/CEF, with
  Home, Settings and Debug tabs. The panel never talks to the console directly —
  everything goes through the plugin's socket over the `console.request` /
  `console.event` bridge, so credentials stay out of the page.
- **Notifications.** The plugin raises a toast from its cached `atc_reply` so it
  works with the panel closed and never opened. A visible panel raises nothing:
  the rule is "notify about what the pilot cannot already see".

## Console endpoint

The console does not have to be on the same machine. The plugin resolves its
endpoint at `XPluginStart`, last source winning:

1. `ws://127.0.0.1:8765/plugin` — the default, unchanged for a local console.
2. `<X-Plane>/Output/preferences/zoal_atc.cfg`
3. `ZOAL_ATC_CONSOLE_URL` / `ZOAL_ATC_CONSOLE_TOKEN` environment variables.

X-Plane is usually launched from Finder or Steam, where an exported variable
never reaches it, so the config file is the option to rely on; the variables are
for a terminal-launched development run.

**The plugin writes the config file itself on first run**, so it is discoverable
at the path that reads it rather than only here. The file it leaves is fully
commented — creating it changes nothing, and an untouched template resolves to
the same loopback default as no file at all. An existing file is never
overwritten, including one you deliberately emptied, and an install that cannot
be written to is logged and flown past.

Uncomment and edit:

```ini
# <X-Plane>/Output/preferences/zoal_atc.cfg
url   = ws://atc.zoal.app/plugin
token = your-shared-secret
```

Omitting the port means the scheme default (80), not the development port.
Unknown keys are ignored, so a config written for a newer plugin will not stop an
older one from starting, but a malformed `url` is refused outright rather than
falling back — connecting somewhere other than where you asked is worse than not
connecting. The resolved endpoint is written to the X-Plane log at startup (the
token is reported only as present or absent, never echoed).

`wss://` is **rejected**, not downgraded: this client speaks plain TCP and has no
TLS layer, so accepting it would put push-to-talk audio on the wire in the clear
while the config claimed otherwise.

### Authentication

The token can be set **in the sim**, on the panel's Settings tab under
*Connection*. It is the one setting the plugin stores itself rather than the
console, because it is the credential the console refuses the connection over —
gated on a live socket like everything else, the control that fixes a bad token
would be disabled exactly when it is needed. Saving rewrites the `token` line in
`zoal_atc.cfg`, keeping your comments and other keys, then reconnects so the
status line shows whether the console accepted it. Save it empty to clear it.
The console URL is deliberately read-only there: a mistyped endpoint takes you
off the air with no way back from inside the sim.

When `token` is set the plugin sends `Authorization: Bearer <token>` on the
upgrade. The console enforces it when `ZOAL_ATC_PLUGIN_TOKEN` is set in its own
environment, refusing the upgrade with `401` before a WebSocket exists. Both
unset leaves the endpoint open, which is the loopback default.

A console reachable from anything other than loopback **needs** this set —
otherwise anyone who can reach it can stream audio in and hear the ATC replies.
Note that over `ws://` the token crosses the network in cleartext: it stops
opportunistic scanning of an open endpoint, but it is not a substitute for TLS.

## Audio transport

The plugin is a WebSocket client. It reconnects lazily when a PTT event needs to
be sent and emits a bounded audio turn:

- `audio_start`: `session_id`, `sequence`, `sample_rate_hz`, `channels`, `format`
- `audio_chunk`: base64-encoded `pcm_s16le`
- `audio_end`: `duration_ms`
- `audio_cancel`: short/faulted PTT turn; the console discards the stream

One chunk is sent when PTT is released. JSON/base64 serialization and WebSocket
I/O run on an async worker so the X-Plane command and flight-loop callbacks only
enqueue PTT events. The protocol already accepts multiple chunks, so live
chunking can land without changing the console API.

## Build

Requires **CMake ≥ 3.24**, a **C++17 toolchain** (MinGW on Windows), **Node 20+**
for the panel, and the **`gh` CLI** to fetch the pinned SkyScript release.

```sh
make            # list targets
make gate       # core tests + panel build + panel tests — the pre-done gate
```

| Target | What it does |
| --- | --- |
| `make test` | Builds and runs the SDK-free C++ core tests (no SDK, no SkyScript, no CEF) |
| `make gui-build` / `make gui-test` | Builds / tests the in-sim panel |
| `make plugin` | Builds the `.xpl`, bootstrapping the X-Plane SDK into `sdk/`, `miniaudio.h` into `vendor/`, and SkyScript into `.cache/skyscript/` |
| `make install-plugin` | Builds and installs into X-Plane (`LOCAL_XPLANE_PLUGIN_DIR=` to override the path) |
| `make release-plugin` / `make archive-plugin` | Assembles / zips the release tree in `dist/` |
| `make release-all` | macOS + Windows + Linux merged into one tree (macOS host, needs Docker and `brew install mingw-w64`) |

`make install-plugin` assembles and verifies with the same two scripts that guard
the shipped artifact, so what you fly locally is what a release contains.

The SkyScript library is provisioned from a pinned GitHub release by
`scripts/ensure-skyscript-lib.sh`, which `make plugin` runs for you. The pin lives
in `scripts/skyscript-version.txt` — edit that one line to take a new SkyScript.
To see what the newest release is without changing the pin:

```sh
ZOAL_ATC_SKYSCRIPT_VERSION=latest ./scripts/ensure-skyscript-lib.sh
```

To use a prepared SkyScript tree instead, set `ZOAL_ATC_SKYSCRIPT_ROOT`. To use a
preinstalled X-Plane SDK, pass `ATC_XPLANE_SDK=/path/to/XPSDK`; the SDK root may
be the official layout containing `CHeaders/XPLM` and `Libraries`, or a flattened
folder containing `XPLM` and `Libraries`.

CI builds and tests the core, the plugin and the panel on macOS, Linux and
Windows for every pull request.

## Relationship to the console

This repo is consumed as a git submodule by the private console repo, which owns
the STT, NLU, the deterministic state machines and the ATC response text. Two
wire contracts are mirrored by hand across the language boundary and are checked
on both sides — change one and change the other in the same breath:

- the telemetry frame `src/telemetry/telemetry_frame.cpp` builds
  (`tests/telemetry_frame_test.cpp`, `tests/traffic_test.cpp`), and
- the GUI frame codec in `src/gui/gui_frames.cpp` (`tests/gui_frames_test.cpp`),
  whose action strings are pinned by `gui/zoal-atc/src/bridge/actions.ts`.

## License

GPL-3.0-or-later, with an **X-Plane SDK linking exception** — see [`LICENSE`](LICENSE)
and [`LICENSE-EXCEPTION`](LICENSE-EXCEPTION). SkyScript (MIT) and miniaudio
(public domain / MIT-0) are downloaded at build time; SkyScript's notice travels
in every release at `licenses/skyscript/LICENSE`.
