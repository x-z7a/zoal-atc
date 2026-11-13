# Installing and configuring zoal-atc

## Requirements

- **X-Plane 12.4.4 or newer.** The panel and HUD are drawn with the SDK 4.4
  panel-graphics ImGui helpers, which older versions do not have.
- **macOS (Apple Silicon), Windows x64 or Linux x64.**
- A microphone, and a key or joystick button for push-to-talk.
- About **600 MB** of disk for the plugin folder — the speech models are most of it.
- A **[SimBrief](https://www.simbrief.com/) account and an IFR flight plan**
  filed there. The console reads your route, altitudes and airports from the
  SimBrief OFP. Only IFR flights are supported for now.

No cloud AI subscription is needed. Speech recognition and controller voices run
in the plugin on your machine. The console's language model runs locally too:
on the hosted console, or on your own hardware if you run one yourself (see
[Running your own console](#running-your-own-console)).

> zoal-atc tries to model real-world ATC as described in FAA and NAV CANADA
> publications, but it is a work in progress with known bugs and edge cases.
> Please report anything odd.

## Install

1. Download the archive for your platform (`zoal-atc-<platform>-<arch>.zip`) from
   [Releases](https://github.com/x-z7a/zoal-atc/releases) and unzip it.
2. Move the `zoal-atc` folder into `<X-Plane>/Resources/plugins/`. Replace any
   older copy entirely rather than merging into it.
3. Launch X-Plane. In **Settings → Keyboard** (or **Joystick**), bind:
   - `zoal_atc/ptt` — "zoal-atc: Push-to-Talk" (hold to transmit)
   - optionally `zoal_atc/gui_toggle` — toggle the radio panel
   - optionally `zoal_atc/hud_toggle` — toggle the radio HUD
4. Open the panel with **Plugins → zoal-atc → Toggle Panel**. The HUD is shown at
   the top right of the screen; toggle it from **Plugins → zoal-atc → Toggle HUD**.

The folder you install looks like this:

```
zoal-atc/
  mac_x64/ | win_x64/ | lin_x64/   zoal-atc.xpl (+ speech runtime libraries on Windows/Linux)
  models/                          whisper (speech-to-text) and Kokoro (controller voices)
  fonts/                           Inter, the panel's and HUD's typeface
  licenses/                        third-party notices
  README.txt
```

On first start the plugin builds its voice catalogue from the bundled Kokoro
model (writing `voices-zoal.bin` beside the stock voices in
`models/kokoro-multi-lang-v1_0/`). Nothing is downloaded; the
plugin folder must be writable for this one-time step.

## Using it

- **Talk:** hold push-to-talk, speak, release. The transmission goes out on the
  COM radio you have selected to transmit on. The words appear in the panel's
  radio log and the HUD.
- **Listen:** controller replies play on the COM radio tuned to the frequency the
  controller used, so that radio's volume knob controls them. Each position has
  its own voice.
- **Type instead:** the panel's composer sends a typed transmission, or tunes
  COM1.
- **Flight plan (required):** before you call anyone, file an IFR plan on
  SimBrief and set your SimBrief pilot ID on **Settings → Flight plan**. The
  console fetches your newest OFP with that ID; until it has one, ATC has no
  route to clear you on. If you change the plan in SimBrief, re-fetch it with
  *Import now* (panel) or *Reload plan* (HUD menu).
- **New flight:** from the panel or the HUD menu, tells the console this is a
  different flight and clears the radio log.
- **Theme:** Graphite, Daylight or Night, on **Settings → Appearance**.

The panel can be dragged from anywhere that is not a control and resized from
its edges. It takes the keyboard only while you are typing in a field, and gives
it back on Enter, Escape or a click elsewhere. Cmd/Ctrl+V pastes.

## The console endpoint

By default the plugin connects to the hosted console at
`ws://atc.zoal.app/plugin`. You only need to configure anything if you run a
console yourself.

The endpoint is resolved at startup, last source winning:

1. the built-in default, `ws://atc.zoal.app/plugin`
2. `<X-Plane>/Output/preferences/zoal_atc.cfg`
3. the `ZOAL_ATC_CONSOLE_URL` / `ZOAL_ATC_CONSOLE_TOKEN` environment variables

X-Plane is usually launched from Finder, Explorer or Steam, where an exported
variable never reaches it, so **the config file is the option to rely on**. The
environment variables are for a terminal-launched development run.

For a console on the same machine:

```ini
url = ws://127.0.0.1:8765/plugin
```

The port is not optional — omitting it means the scheme default, 80.

`wss://` is **rejected**, not downgraded: the plugin has no TLS layer, and
accepting it would send push-to-talk data in the clear while the config claimed
otherwise. A malformed `url` is likewise refused rather than falling back to the
default.

## Running your own console

The console (`zoal-atc-console`) is not public yet. It will be open-sourced if
there is interest. When you do run one:

- **Run it on a different machine from X-Plane.** The console's language model
  needs a lot of CPU, GPU and memory, and so does the sim. Our testing ran the
  console on an M3 MacBook, with X-Plane on a separate machine.
- Set `url = ws://<console-host>:8765/plugin` in the config file below.
- Set a token on both sides. Once the console is reachable from your network
  and not just loopback, it should not be left open (see
  [Authentication](#authentication)).

## The config file

`<X-Plane>/Output/preferences/zoal_atc.cfg` is written by the plugin on first
run as a fully commented template, so it can be found where it is read. An
existing file is never overwritten. Unknown keys are ignored, so a file written
for a newer plugin does not stop an older one from starting.

| Key | Set by | Meaning |
| --- | --- | --- |
| `url` | you | Console endpoint. Commented out means the hosted console. |
| `token` | you or the panel | Shared secret for a console that requires one (see below). |
| `simbrief_id` | you or the panel | SimBrief pilot ID the console fetches your flight plan with. |
| `panel_theme` | the panel | `graphite`, `daylight` or `night`. |
| `client_id` | the plugin | Identity of this installation, generated on first run. The console uses it to tell aircraft apart; don't copy it to another machine. |

The resolved endpoint is written to X-Plane's `Log.txt` at startup (the token is
reported only as present or absent, never echoed).

## Authentication

Set the token in the sim on **Settings → Connection → Access token**, or as
`token = …` in the config file. Saving in the panel rewrites only the `token`
line, keeping your comments and other keys, then reconnects so the status line
shows whether the console accepted it. Save it empty to clear it. The console
URL is deliberately read-only in the panel: a mistyped endpoint would take you
off the air with no way back from inside the sim.

With a token set, the plugin sends `Authorization: Bearer <token>` on the
WebSocket upgrade. A console enforces it when `ZOAL_ATC_PLUGIN_TOKEN` is set in
its environment, refusing the upgrade with `401`. Both unset leaves the endpoint
open, which is fine on loopback.

A console reachable from anything other than loopback **needs** a token —
otherwise anyone who can reach it can send transmissions in and read the replies.
Over `ws://` the token crosses the network in cleartext: it stops opportunistic
access to an open endpoint, but it is not a substitute for TLS.

## Troubleshooting

Look for lines starting with `[zoal-atc]` in `<X-Plane>/Log.txt`.

| Symptom | Check |
| --- | --- |
| HUD shows **CONSOLE UNREACHABLE** | The `url` in the log line at startup; that a self-hosted console is running and its port is spelled out; that a token is set if the console requires one. |
| Nothing happens on push-to-talk | That `zoal_atc/ptt` is bound, and that X-Plane has microphone permission (macOS: System Settings → Privacy & Security → Microphone). |
| ATC doesn't know your route or won't clear you | That your SimBrief pilot ID is set, that your newest OFP is the flight you're flying and is IFR, then *Import now* / *Reload plan*. |
| You hear no replies | The COM radio tuned to the controller's frequency, its audio selector and volume. |
| Panel or HUD text looks wrong | That `fonts/` is present in the plugin folder; without it the plugin falls back to X-Plane's Roboto. |
| Plugin fails to load | X-Plane version (12.4.4+), and that the whole folder — `models/`, and on Windows/Linux the libraries beside the `.xpl` — was copied. |
