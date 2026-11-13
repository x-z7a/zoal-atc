# Developing zoal-atc

## Prerequisites

- **CMake ≥ 3.24** and a **C++17 toolchain** (Clang/Xcode on macOS, GCC on
  Linux, MinGW on Windows — the release toolchain; MSVC is not what ships).
- `make`, `bash`, `curl`. On Windows: `choco install make mingw`.
- For `make release-all` on macOS: Docker and `brew install mingw-w64`.

No Node and no browser runtime — the panel and HUD are native Dear ImGui.

## Common tasks

```sh
make                 # list targets
make gate            # the pre-done gate: build and run the core tests (CI runs this)
make preview         # render the panel in every theme to build-preview/*.png
make install-plugin  # build and install into ~/X-Plane 12/Resources/plugins/zoal-atc
```

| Target | What it does |
| --- | --- |
| `make test` | Builds and runs the SDK-free C++ core tests in `build/` (no X-Plane SDK, no ImGui). |
| `make gate` | `make test`; the check to pass before calling work done. |
| `make preview` | Builds `tools/panel_preview` and renders Home and Settings in Graphite, Daylight and Night to `build-preview/` — the same `PanelSession` and drawing code the plugin runs, fed a scripted console. |
| `make plugin` | Builds `build-plugin/zoal_atc.xpl`, fetching its dependencies first (below). |
| `make models` | Fetches the whisper model and the Kokoro voices into `.cache/models`. |
| `make install-plugin` | `plugin` + `models`, then assembles and verifies the install with the release scripts. Override the destination with `LOCAL_XPLANE_PLUGIN_DIR=/path/to/plugins/zoal-atc`. |
| `make release-plugin` | Assembles the release tree in `dist/zoal-atc`. |
| `make archive-plugin` | Zips it to `dist/zoal-atc-<platform>-<arch>.zip`. |
| `make release-all` | macOS + Windows (MinGW) + Linux (Docker) merged into one tree; macOS host only. |
| `make clean` | Removes build outputs and `dist/`, keeping `.cache/`. |

`make install-plugin` uses the same `scripts/package-release.sh` and
`scripts/verify-release-tree.sh` that guard the shipped archive, so what you fly
locally is what a release contains.

## Dependencies

Everything is fetched by the build and pinned with a checksum; nothing third-party
is committed. Downloads land in git-ignored directories and survive `make clean`.

| Dependency | Used for | Pin | Location |
| --- | --- | --- | --- |
| X-Plane SDK 4.4.0 | Plugin API, panel-graphics ImGui helpers | `SDK_VERSION` in `Makefile` | `sdk/` |
| miniaudio | Microphone capture | `cmake/SetupMiniaudio.cmake` | `vendor/` |
| whisper.cpp | Speech-to-text | `scripts/whisper-version.txt` | `.cache/whisper.cpp-*` |
| sherpa-onnx | Kokoro TTS runtime | `scripts/sherpa-onnx-version.txt` | `.cache/sherpa-onnx-*` |
| Dear ImGui | Panel, HUD, preview | `scripts/imgui-version.txt` | `.cache/imgui-*` |
| FreeType | Hinted text rasterisation | `scripts/freetype-version.txt` | `.cache/freetype-*` |
| Inter | Typeface | `scripts/inter-version.txt` | `.cache/inter-*` |
| Models | whisper `small.en` q5_1, Kokoro multi-lang v1.0 (~560 MB) | `scripts/ensure-models.sh` | `.cache/models` |

To use a preinstalled X-Plane SDK, pass `ATC_XPLANE_SDK=/path/to/XPSDK`. The SDK
root may be the official layout (`CHeaders/XPLM`, `Libraries`) or a flattened
folder containing `XPLM` and `Libraries`. Each `ensure-*.sh` script re-checks its
pin on every build, so bumping a version file is enough to move a dependency.

macOS builds are Apple Silicon only (the sherpa-onnx flavour is
`osx-arm64-static-lib`). On Windows and Linux sherpa-onnx is shared, and its
libraries are shipped beside the `.xpl`.

## Source layout

The code is split so that almost everything builds and tests without X-Plane:

| Path | What lives there |
| --- | --- |
| `include/zoal_atc/`, `src/` (except `ui/`, `xplane/`) | **`zoal_atc_core`** — SDK-free, ImGui-free static library. Built for tests on all three platforms. |
| `src/ptt/` | `PushToTalkController` state machine (`idle` / `recording` / `faulted`, with explicit outcomes for duplicate press, stray release, short transmission, max-duration cutoff, recorder failure) and the miniaudio recorder. |
| `src/stt/` | whisper.cpp transcriber, run on its own thread, and the `transcript` frame. |
| `src/tts/` | Kokoro synthesiser, VHF shaping, the voice catalogue and the aviation lexicon. |
| `src/telemetry/` | Telemetry frame builder and TCAS traffic filtering. |
| `src/weather/` | METAR extraction from X-Plane's real-weather files. |
| `src/transport/` | WebSocket client, endpoint/config parsing, reconnect pacing, bounded queues, JSON helpers. |
| `src/gui/` | Panel and HUD *models*: `PanelSession`, `HudSession`, `GuiBridge` (the `console.request` / `console.event` bridge), frame codecs, themes, layout, font metrics. |
| `src/ui/` | ImGui drawing for the panel (`panel_view`) and a small widget kit — shared by the plugin and `panel_preview`. |
| `src/xplane/` | The only code that touches the SDK: plugin entry points, commands, menus, datarefs, the ImGui window host, panel and HUD windows. |
| `tools/panel_preview.cpp` | Off-screen panel renderer used by `make preview`. |
| `tests/` | One test executable per area, registered in `CMakeLists.txt` with `zoal_atc_add_test`. |
| `scripts/` | Dependency fetchers, packaging, release verification, cross-builds, `voice-catalogue.py`. |

Keep logic in `zoal_atc_core` and out of `src/xplane/` wherever possible: that is
what lets CI test it in seconds on macOS, Linux and Windows, and lets `make
preview` show UI changes without starting the sim. Keyboard handling, window
drag/resize and anything else that depends on X-Plane's window system can only
be verified in the sim.

`tests/live_console_probe.cpp` drives the real `WebSocketClient` and `GuiBridge`
against a running console over a real socket (`live_console_probe <host> <port>
[token]`). It is not registered with CMake; build it by hand against
`zoal_atc_core` when debugging plugin↔console conversations.

## Runtime behaviour worth knowing

- **Threads.** X-Plane command and flight-loop callbacks only enqueue work.
  Transcription, synthesis and the socket each run on their own thread; a dead
  or absent console must never stall the sim.
- **Connection.** The hello declares capabilities: `local_stt` once the whisper
  model has loaded, and `voice_catalogue_2` once the voice catalogue is built.
  A console only sends voices past 28 to a client that declared the latter.
  Each connection carries the installation's `client_id` and, if set,
  `simbrief_id`.
- **Push-to-talk turn.** `audio_start` (session, sequence, format, and the
  `transmit_com` / `frequency_hz` read when the button went down), then either
  `transcript` (words, timing, confidence, model) or `audio_cancel`. The console
  sends an `stt_context` with the prompt to transcribe with. Audio never leaves
  the aircraft — the console does not transcribe.
- **Replies.** Each `atc_reply` names a `voice`, stable per station. Synthesis
  starts on arrival, a sentence at a time, is band-passed to 300–3,000 Hz with
  light compression, and plays on the bus of the COM radio tuned to the reply's
  `frequency_hz` (the selected radio if both or neither are). A `speak_ack` with
  `ok:true` is sent once the audio has played; a reply that could not be voiced,
  failed, took more than twelve seconds to start, or could not be played is
  acknowledged `ok:false`.
- **Telemetry rate** is console-controlled through `telemetry_control`; the
  plugin falls back to a local policy before the first command and after a
  disconnect. Non-finite or out-of-range traffic positions are dropped before
  serialisation. Own-ship callsign, ICAO type and Mode S come from TCAS slot 0.

## Wire contracts

The console is in another language and another repo, so these contracts are
mirrored by hand and checked on both sides. Change one and change the other in
the same breath:

- the telemetry frame built in `src/telemetry/telemetry_frame.cpp`
  (`tests/telemetry_frame_test.cpp`, `tests/traffic_test.cpp`);
- the GUI frame codec in `src/gui/gui_frames.cpp` (`tests/gui_frames_test.cpp`);
- the action and event names the panel and HUD use, in
  `src/gui/panel_session.cpp` and `src/gui/hud_session.cpp`.

## Voices

Voice *n* is entry *n*−1 of `src/tts/voice_catalogue_table.inc`. Voices 1–54 are
Kokoro's own speakers in model order (American, British, and Spanish, French,
Hindi, Italian, Japanese, Portuguese and Mandarin speakers speaking accented
English); 55 onward are blends of two speakers, for accents Kokoro has few voices
for. The console picks from the accent of the field's country; British and
Indian English use the British lexicon, loaded the first time it is needed.

The blended table is built on the pilot's machine on first start and written
beside the stock one as `voices-zoal.bin`; `model.onnx` is given the catalogue's
speaker count by appending one metadata entry, because sherpa-onnx refuses a
table larger than the model declares. Each voice has its own speed so all keep a
controller's pace (~205 wpm). Words missing from Kokoro's lexicons — "niner",
"ILS", "RNAV", "VOR" — are added in `lexicon-aviation-*.txt`, since a word in no
lexicon is silently dropped.

`scripts/voice-catalogue.py` regenerates the table, the speeds and the aviation
lexicon.

## CI and releases

- **CI** (`.github/workflows/ci.yml`) runs on every PR and push to `main`:
  `make test` and `make plugin` on Ubuntu, macOS and Windows. It uses `make`
  rather than raw CMake on purpose, so CI exercises the exact path a release does.
- **Releases** (`.github/workflows/release.yml`) run on a `v*` tag (or a manual
  dispatch for an existing tag): a draft release is created or reused, each
  platform runs `make archive-plugin`, and the `zoal-atc-*.zip` archives are
  uploaded. A published release that already has packages is never overwritten.
- The project version is in `CMakeLists.txt` (`project(... VERSION ...)`).

## Conventions

- Run `make gate` before considering a change done.
- Comments explain *why*; the reasoning behind non-obvious choices (Windows
  generator flags, `wss://` refusal, why the token is settable in-sim) lives next
  to the code it guards.
- Any new third-party component needs a pin with a checksum, an `ensure-*.sh`
  fetcher, and an entry in `licenses-third-party/THIRD-PARTY.txt`.
