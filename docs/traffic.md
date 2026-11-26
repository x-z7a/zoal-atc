# zoal-mp design

## Goal

1. **Multiplayer like Plane-Pals.** Pilots connected to the same console see
   each other in the sim, rendered with CSL models via XPMP2, smoothly.
2. **AI traffic as good as Virtual Airline Traffic.** Busy, believable traffic
   with airline-correct models and liveries, real configurations (gear, flaps,
   lights) and no visible stepping.
3. **AI traffic that ATC controls.** The traffic comes from atc.zoal.app, so the
   console's controllers fly it. AI aircraft obey the same clearances, sequencing
   and runway authority as real pilots, and they replace the observed
   non-participants the console currently can only watch.
4. **Merge into zoal-atc.** This repo is a staging ground. Its layout mirrors
   zoal-atc's so that merging means moving directories, not rewriting.

## Shape

```text
           atc.zoal.app (console)                     X-Plane + zoal-mp
 ┌───────────────────────────────────────┐     ┌──────────────────────────────────┐
 │ surveillance picture (participants)   │     │ ConsoleLink  reader ─► queue      │
 │ AI traffic simulator  ◄─ controllers  │─────►      (threads) writer ◄─ slot    │
 │ traffic publisher (per-socket, ~1 Hz) │ ws  │          │                       │
 │      ▲ telemetry + render fields      │◄────│ flight loop: drain ─► Registry   │
 └──────┴────────────────────────────────┘     │   TrafficRenderer ─► XPMP2 ─► TCAS│
                                               │   OwnshipSampler ─► telemetry     │
                                               └──────────────────────────────────┘
```

### Plugin (this repo)

| Layer | Where | Depends on |
| --- | --- | --- |
| Core, SDK-free and unit-tested | `include/zoal_mp`, `src/traffic`, `src/ownship` | zoal-atc transport only |
| Simulator shell | `src/xplane` | XPLM, XPMP2, core |
| Dev tools | `tools/mockconsole` (Go), `tools/traffic_probe` (C++) | none / core |

- **`TrafficFrame`**: the `traffic` frame parser and encoder (docs/protocol.md).
- **`ClockSync`**: maps console time to local monotonic time. It keeps the
  smallest offset seen (least network delay) and lets it creep up 2 ms/s to
  follow drift.
- **`TrackSmoother`**: this is what makes traffic look good. It draws 1.5 s
  in the past so almost every frame falls between two real reports. It
  interpolates position along a cubic Hermite curve with the reported
  velocities as tangents, so turns are arcs and not polygons. Heading takes the
  short way round, and attitude and configuration are blended. Past the newest
  report it dead-reckons for at most 3 s and then holds.
- **`TrafficRegistry`**: one smoother per track. It reports added, remodelled,
  relabelled and removed tracks, and expires tracks that have gone silent.
- **`TrafficRenderer` / `ConsoleAircraft`**: one `XPMP2::Aircraft` per track.
  XPMP2 calls `UpdatePosition` each frame, which asks the registry for its
  state. Participants get TCAS priority and green labels.
- **`ConsoleLink`**: a reader thread (which also handles reconnects) and a
  writer thread that sends telemetry at 2 Hz. It is the only network code.
  After the merge it goes away, because zoal-atc's connection already exists.

- **Traffic tab** (panel): every drawn aircraft, nearest first, with distance,
  bearing, altitude, relative altitude, groundspeed and vertical speed, and a
  All / Pilots / AI filter. A tap *tracks* one (pinned in a live card above
  the list, its in-sim label turns cyan); *Follow* (or a double tap) chases it
  with the outside camera (`XPLMControlCamera`, released by any view change),
  with nearer/farther and orbit buttons. `traffic::nearby` and
  `traffic::chase_pose` (core, tested) do the geometry; `gui::traffic_panel`
  the words; `traffic_layer` holds the tracked/followed ids and the camera.

- **Traffic tags** replace XPMP2's bitmap labels: a full-screen ImGui overlay
  (`ImguiWindow` overlay mode: no decoration, no input handlers, so every
  click passes through) drawn in the HUD's glass and theme. Near aircraft
  (< 3 km from the camera) show callsign, type, altitude, vertical speed,
  groundspeed and distance from us; to 12 km callsign and altitude; to 30 km
  the callsign, fading over the last fifth. The tracked one is always full.
  `gui::traffic_label` (words, detail, fade) and `traffic::project_to_screen`
  are core and tested; `xplane/traffic_labels` places them using XPMP2's drawn
  position, so tag and model move together. `zoal_atc/traffic_labels_toggle`
  (Plugins > zoal-atc > Toggle Traffic Tags) hides them. Where the overlay
  cannot be made (X-Plane before 12.4.4), XPMP2's labels stay on.

- **CSL folder**: `csl_path` in zoal_atc.cfg first, then our `Resources/CSL`
  and the IVAO / LiveTraffic / xPilot / X-Ivap folders (`traffic::choose_csl`).
  With none, traffic stays off and the Traffic tab becomes a picker: every
  `CSL` folder found under `Resources/plugins` (`discover_csl_folders`), a
  path field, and the system folder dialog (`xplane/folder_picker`, on its own
  thread). A folder with an `xsb_aircraft.txt` in or below it starts traffic at
  once, is written to `csl_path`, and the socket re-dials declaring
  `traffic_v1` (`WebSocketClient::add_capability`). While running, "CSL models"
  on the tab adds another folder; the old one's models stay until restart.

- **Ground fit** (`traffic::GroundFit`): the console's ground is a navdata
  field elevation, X-Plane's is the mesh, and XPMP2's clamp only lifts. Each
  aircraft probes the terrain and, on the ground, sits on it exactly; the
  correction eases in at 3 m/s at touchdown and fades over the first 60 m
  after liftoff. Never drawn below the terrain.
- **Monotone altitude**: the smoother limits its vertical tangents
  (Fritsch-Carlson), so a reported vertical speed that disagrees with the
  climb between two reports cannot bow the curve; on the ground altitude is
  linear. Consistent reports draw exactly as before.

Threading rule: the registry and all XPMP2 objects are touched only on the
simulator's main thread. The network threads hand over parsed frames and take
encoded strings, nothing else.

### Console (`~/git/atc`, branch `traffic-v1`)

`internal/traffic`, started by `cmd/console/traffic.go` (`-traffic`, on by default):

- **Publisher** — its own group on `plugin-telemetry`. Every report stores the
  pilot's drawable state (`TrafficStore`, Redis); a report from a `traffic_v1`
  client also sends that flight a frame (1 Hz, 80 nm, 60 targets, nearest
  first, never itself) through `gateway.NewEgressEnvelope`.
- **Simulator** — the mock AI fleet: eight flights each way on CYOW–CYYZ
  (Air Canada, Jazz, WestJet, Porter), on routes built from the navdata
  runways (CYOW departs 32 / lands 25, CYYZ departs 24L / lands 24R). Whichever
  console holds the Redis lease steps it every second, so a dying console hands
  the traffic on. Arrived aircraft turn around after 90 s.
- **Control** — a `traffic.Command` (altitude, heading, speed, direct-to,
  resume, release) on the `traffic-ai-commands` stream changes what an aircraft
  flies. Today the operator publishes it; the controllers will publish the same
  command later, without this package changing.

  ```sh
  curl localhost:8765/v1/traffic/ai                                   # the fleet
  curl -X POST localhost:8765/v1/traffic/ai/ACA451/command -d '{"altitude_ft":5000,"heading_deg":270}'
  curl -X POST localhost:8765/v1/traffic/ai/ACA451/command -d '{"resume":true}'
  ```

  `-traffic-hold` keeps every departure lined up until a command with
  `"release":true`.

Not done yet: the AI aircraft are not in the surveillance picture, so the
controllers neither see nor talk to them; and console-side echo suppression
(the plugin already drops its own injected aircraft from its TCAS report).

## Merge plan into zoal-atc

This repo now *is* zoal-atc: branch `traffic` on zoal-atc's history, so merging
is a pull request. Steps 1–5 below are done on that branch; what remains is
review, a Settings toggle, and documenting CSL models in INSTALL.md.

1. Move `include/zoal_mp` and `src/{traffic,ownship}` into zoal-atc's
   `zoal_atc_core`, keeping the `zoal_mp` namespace or renaming it to
   `zoal_atc::traffic`. Move the tests too.
2. Fold the render fields into zoal-atc's `telemetry_frame.cpp`, and delete
   `ownship_frame`.
3. Add `traffic_v1` to the capabilities zoal-atc's hello sends. Route `traffic`
   frames from its existing receive loop into the registry queue, and delete
   `ConsoleLink`.
4. Move `TrafficRenderer` into `src/xplane`, then init XPMP2 and load CSL in
   `xplane_plugin.cpp`. Add a Settings toggle to enable traffic.
5. Packaging: ship XPMP2's `Resources` (Doc8643, related.txt, Obj8DataRefs,
   MapIcons) and document where CSL packages go.

## Open questions

- **Running both plugins before the merge.** Two sockets for one sim give the
  console two flights. Options: the console pairs them by installation id
  (zoal-mp could reuse zoal-atc's id with a `traffic-only` capability that
  suppresses ATC for that socket), or we tell users to run only one of them.
  The proposal is the `traffic-only` capability.
- **CSL distribution.** Bluebell/X-CSL are large, separately licensed
  downloads. Should we link to them, or offer a curated small set?
- **Model matching.** Match on ICAO type + airline, with livery as a hint.
  Should the console send a CSL model id directly once it knows what users have
  installed?
- **Update rate.** 1 Hz with a 1.5 s delay is smooth but late. On the ground
  near other aircraft, 2–5 Hz with a smaller delay may be worth the bandwidth.
