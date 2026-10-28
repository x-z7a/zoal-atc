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
panel. It ships no browser: since SkyScript v0.12.0 the panel is an X-Plane
browser window, so **X-Plane 12.4.4 or newer is required**.

Out of the box the plugin talks to the hosted console at `ws://atc.zoal.app/plugin`,
so a fresh install needs no configuration. To point it at a console you run
yourself, see *Console endpoint* below.

## What it does

- **Push-to-talk capture.** An SDK-free `PushToTalkController` with finite states
  (`idle`, `recording`, `faulted`) and explicit outcomes for duplicate press,
  stray release, short transmission, max-duration cutoff and recorder failure.
  Microphone capture is cross-platform through `miniaudio`.
- **Telemetry sampling.** The starter dataref catalog sampled into `telemetry`
  frames, including TCAS traffic and authoritative ownship callsign, ICAO type
  and Mode S identity from TCAS slot 0. Non-finite or out-of-range traffic
  positions are discarded before serialization. The rate is
  **console-controlled**: the plugin decodes the `telemetry_control` frame and
  samples at the commanded interval, falling back to a local policy when no
  command has arrived or the connection drops.
- **Navdata.** Hash-first sending of scoped `apt.dat` blocks, CIFP, airspace and
  VRP data, answering `navdata_*_request`, plus METAR.
- **The in-sim panel.** A React + TypeScript app hosted by SkyScript in an X-Plane browser window, with
  Home, Settings and Debug tabs. The panel never talks to the console directly —
  everything goes through the plugin's socket over the `console.request` /
  `console.event` bridge, so credentials stay out of the page.
- **Notifications.** The plugin raises a toast from its cached `atc_reply` so it
  works with the panel closed and never opened. A visible panel raises nothing:
  the rule is "notify about what the pilot cannot already see".

## Console endpoint

The console does not have to be on the same machine. The plugin resolves its
endpoint at `XPluginStart`, last source winning:

1. `ws://atc.zoal.app/plugin` — the default: the hosted console, which is what a
   downloaded release should reach without being configured first.
2. `<X-Plane>/Output/preferences/zoal_atc.cfg`
3. `ZOAL_ATC_CONSOLE_URL` / `ZOAL_ATC_CONSOLE_TOKEN` environment variables.

Running a console locally is the case that needs configuring now. Put
`url = ws://127.0.0.1:8765/plugin` in the config file (the port is not optional —
omitting it means the scheme default, 80), or export `ZOAL_ATC_CONSOLE_URL` for a
terminal-launched sim.

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

## Push-to-talk

The plugin is a WebSocket client. A push-to-talk turn is heard in the cockpit
and only its words leave the aircraft:

- `audio_start`: `session_id`, `sequence`, `sample_rate_hz`, `channels`,
  `format`, and `transmit_com` and `frequency_hz` read from the radio the instant
  the button goes down, so the console knows which channel the pilot keyed
- `transcript`: the words, their timing, the confidence and the model
- `audio_cancel`: a short or faulted turn, or one that could not be transcribed

whisper.cpp (pinned in `scripts/whisper-version.txt`) transcribes on its own
thread — Metal on macOS, the CPU elsewhere — so the X-Plane command and
flight-loop callbacks only enqueue the turn. The console sends an `stt_context`
with the prompt to transcribe with (callsign, stations, places, runways, route),
which is what turns "C21" back into "AC21". The hello declares `local_stt` once
the model has loaded. The recording never leaves the aircraft: a turn that
cannot be transcribed is cancelled, because the console does not transcribe.

## Controller voices

Every reply is said by Kokoro v1.0 through sherpa-onnx (pinned in
`scripts/sherpa-onnx-version.txt`), in the voice of the position sending it.
The console names a `voice` on every `atc_reply`, stable per station, so Ground
and Tower sound like two people. Voice n is entry n-1 of the voice catalogue
(`src/tts/voice_catalogue_table.inc`): 1-54 are Kokoro's own speakers in the
model's order — American, British, and the Spanish, French, Hindi, Italian,
Japanese, Portuguese and Mandarin speakers, who speak English with their accent
— and 55 onward are blends of two of them, for the accents Kokoro has few
voices for. The console chooses from the accent of the field's country; British
and Indian English are spoken with the British lexicon, loaded only the first
time one is needed.

The catalogue is made on the pilot's machine the first time the plugin starts,
from the bundled model: the blended table is written beside the stock one as
`voices-zoal.bin`, and `model.onnx` is given the catalogue's speaker count by
appending one metadata entry, because sherpa-onnx refuses a table of a size the
model does not declare. Nothing extra is downloaded. Only then does the hello
declare `voice_catalogue_2`, and a console sends a voice past 28 only to a
client that declared it. Each voice speaks at its own speed, measured so all of
them keep a controller's pace (about 205 words a minute), and words Kokoro's
lexicons lack — "niner", "ILS", "RNAV", "VOR" — are added in
`lexicon-aviation-*.txt`, since a word in no lexicon is left out of the speech.
`scripts/voice-catalogue.py` regenerates the table, the speeds and the
aviation lexicon. Synthesis starts the moment a reply arrives, on its own thread, a
sentence at a time, so the first sentence of a long clearance is ready while
the rest is still being made. Each sentence is shaped into what a VHF receiver
passes, 300 to 3,000 Hz with a little compression, and plays on the audio bus of
the COM radio tuned to the channel the controller said it on (`frequency_hz`),
so Tower on COM2 comes out of COM2 and that radio's volume knob controls it.
With both radios on the channel, or neither, it is the one the pilot has
selected.

Nothing else in the cockpit speaks. The `speak_ack` is sent when the audio has
actually played, with `ok:true`; a reply whose voices are not loaded, whose
synthesis failed or took more than twelve seconds to start, or which the radio
would not play is acknowledged `ok:false`, so the console knows the pilot did
not hear it.

## Models

Both models ship inside the plugin folder, under `models/` — about 560MB, of
which Kokoro's model, voices and English lexicons are 380MB. `make install-plugin` and the release targets
fetch them once into `.cache/models` (checksums pinned in
`scripts/ensure-models.sh`) and copy them in; `make models` fetches them alone.

## Build

Requires **CMake ≥ 3.24**, a **C++17 toolchain** (MinGW on Windows), **Node 20+**
for the panel, and the **`gh` CLI** to fetch the pinned SkyScript release.

```sh
make            # list targets
make gate       # core tests + panel build + panel tests — the pre-done gate
```

| Target | What it does |
| --- | --- |
| `make test` | Builds and runs the SDK-free C++ core tests (no SDK, no SkyScript) |
| `make gui-build` / `make gui-test` | Builds / tests the in-sim panel |
| `make plugin` | Builds the `.xpl`, bootstrapping the X-Plane SDK into `sdk/`, `miniaudio.h` into `vendor/`, SkyScript into `.cache/skyscript/`, whisper.cpp into `.cache/whisper.cpp-*`, and sherpa-onnx into `.cache/sherpa-onnx-*` |
| `make install-plugin` | Builds and installs into X-Plane (`LOCAL_XPLANE_PLUGIN_DIR=` to override the path) |
| `make models` | Fetches the transcription model and the controller voices into `.cache/models` |
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
