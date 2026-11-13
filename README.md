# zoal-atc — the X-Plane plugin

`zoal-atc` is the X-Plane plugin half of the ZOAL AI air traffic control system.
It is the *edge* process that runs in the cockpit: it hears the pilot, samples the
simulator, speaks the controllers' replies on the COM radios, draws the in-sim
panel and HUD, and talks to a `zoal-atc-console` server over a WebSocket.

**It owns no ATC logic.** Every clearance, instruction and refusal is authorized
by the console's deterministic state machines. The plugin reports what the pilot
said and what the simulator is doing, and renders and speaks what comes back.

> **Not an approved operating procedure.** The phraseology, separation standards,
> airport procedures and authority rules this system produces are a software
> artifact. Validate them against your operating jurisdiction before any
> operational use, and do not present generated ATC behavior as operationally
> certified.

## Project status

zoal-atc is a working hobby project, not a finished product. Before you fly with
it, know that:

- **A SimBrief flight plan is required.** The console builds its picture of
  your flight from your SimBrief OFP: route, altitudes, departure and arrival.
  Without one, ATC doesn't know where you're going.
- **Only IFR is supported.** VFR flights, flight following and pattern work are
  not modelled yet.
- **It aims to sound like real ATC.** Phraseology and procedures are modelled
  as closely as we could manage on FAA and NAV CANADA publications. It is still
  an approximation (see the notice above).
- **Expect bugs and edge cases.** Unusual routes, procedures, airports or
  readbacks can confuse it. Issue reports are welcome.
- **The server side is closed for now.** The console (state machines, NLU,
  navdata) is in a private repo. It will be open-sourced if there is interest,
  so let us know if you want it.
- **No cloud AI subscription is needed.** The language model runs locally. If
  you host the console yourself, run the LLM on a different machine from
  X-Plane so the two don't compete for CPU, GPU and memory. Our testing used an
  M3 MacBook as that second machine. **Ask us if you are interested to host the server by yourself**

## Quick start

1. Create a [SimBrief](https://www.simbrief.com/) account, if you don't have
   one, and file an IFR flight plan for your flight.
2. Download the archive for your platform from
   [Releases](https://github.com/x-z7a/zoal-atc/releases) and unzip it into
   `<X-Plane>/Resources/plugins/`.
3. In X-Plane, bind `zoal_atc/ptt` ("zoal-atc: Push-to-Talk") to a key or button.
4. Open the panel from **Plugins → zoal-atc → Toggle Panel**. Enter your SimBrief
   pilot ID on **Settings → Flight plan**, then hold push-to-talk and call the
   controller.

Requires **X-Plane 12.4.4 or newer**. Out of the box the plugin connects to the
hosted console at `ws://atc.zoal.app/plugin`, so a fresh install needs no
configuration. See **[INSTALL.md](INSTALL.md)** for platforms, configuration,
SimBrief, authentication and troubleshooting.

## What it does

| Area | In short |
| --- | --- |
| **Push-to-talk** | Captures the microphone with miniaudio while the PTT command is held, and records which COM radio and frequency was keyed. |
| **Speech-to-text** | Transcribes each transmission in the cockpit with whisper.cpp (Metal on macOS, CPU elsewhere), prompted by context the console sends (callsign, stations, runways, route). Audio never leaves the aircraft — only the words do. |
| **Controller voices** | Speaks every reply with Kokoro v1.0 through sherpa-onnx, in a stable voice per position chosen from a catalogue of accents, band-limited to sound like VHF, on the audio bus of the COM radio tuned to that channel. |
| **Telemetry** | Samples own-ship state, TCAS traffic and aircraft identity at a rate the console commands, with a local fallback. |
| **Weather** | Sends the newest METAR from X-Plane's real-weather download on airport change, on reconnect and every ten minutes. |
| **Panel** | A resizable Dear ImGui window: own-ship readouts, the radio log, a composer to type a transmission or tune COM1, and Settings (theme, console token, SimBrief pilot ID, flight-plan import). |
| **HUD** | A compact bar at the top right — callsign, console status, a menu to reload the plan, start a new flight or open the panel — and the last three transmissions. |
| **Themes** | Graphite, Daylight and Night, shared by panel and HUD. |

Navdata (airports, procedures, airspace, fixes), NLU and ATC response text all
live in the console; the plugin ships none of it.

The panel and HUD are drawn natively with X-Plane's panel-graphics ImGui helpers
rather than browser windows, because clicking any X-Plane browser window blanks
other plugins' UIs (for example Airfoillabs' XJet sidebar).

## How it fits together

```
 pilot ──PTT──▶ miniaudio ──▶ whisper.cpp ──transcript──┐
                                                         │
 X-Plane datarefs ──▶ telemetry / METAR ─────────────────┼──▶ WebSocket ──▶ zoal-atc-console
                                                         │                   (state machines,
 panel / HUD ◀── console.event ◀─────────────────────────┤                    NLU, navdata)
                                                         │
 COM radio ◀── VHF shaping ◀── Kokoro (sherpa-onnx) ◀────┘ atc_reply
```

The panel and HUD never talk to the console directly; every request goes
through the plugin's own socket, so credentials stay in the plugin.

## Documentation

- **[INSTALL.md](INSTALL.md)** — installing a release, configuring the console
  endpoint, tokens, SimBrief, the config file and troubleshooting.
- **[DEVELOPMENT.md](DEVELOPMENT.md)** — building from source, tests, the
  off-screen panel preview, source layout, wire contracts, voices and releases.

## Relationship to the console

This repo is consumed as a git submodule by the private console repo, which owns
the NLU, the deterministic state machines and the ATC response text. The wire
contracts between them are mirrored by hand and tested on both sides — see
[DEVELOPMENT.md](DEVELOPMENT.md#wire-contracts).

## License

GPL-3.0-or-later, with an **X-Plane SDK linking exception** — see
[`LICENSE`](LICENSE) and [`LICENSE-EXCEPTION`](LICENSE-EXCEPTION). Third-party
components (Dear ImGui, FreeType, Inter, miniaudio, whisper.cpp, sherpa-onnx and
the models) are downloaded at build time; their notices travel in every release
under `licenses/`, and are listed in
[`licenses-third-party/THIRD-PARTY.txt`](licenses-third-party/THIRD-PARTY.txt).
