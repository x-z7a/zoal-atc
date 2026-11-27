# zoal-mp wire protocol (proposal)

Status: **implemented** on branch `traffic-v1` of the console (`~/git/atc`:
`internal/wire/traffic.go`, `internal/traffic`) and branch `traffic` of this repo
(`src/xplane/traffic_layer.cpp`). `tools/mockconsole` still speaks it for work
without a console. Everything here is additive to the shipped `/plugin`
protocol (`internal/wire`), negotiated by a hello capability, so an aircraft
that does not ask for traffic never receives it.

## Negotiation

The aircraft adds `traffic_v1` to `capabilities` in its `hello`. The console
sends `traffic` frames only on sockets that declared it.

zoal-mp standalone dials `/plugin` with its own installation id. After the
merge, zoal-atc's existing connection declares `traffic_v1` and nothing else
changes. Until then, running both plugins at once gives the console two flights
for one simulator (the gateway names the second `<id>#2`); see
[design.md](design.md#open-questions).

## Console → aircraft: `traffic`

Sent about once a second per aircraft, carrying everything near it. The target
array **nests**, which the existing client's flat key search cannot read. zoal-mp
reads it with `json_raw_field` + `json_array_elements` and parses each element
on its own, so the rule becomes: envelope keys before `targets`, and target keys
read only inside their element.

```json
{"type":"traffic","seq":1842,"server_time_ms":1764100000123,
 "targets":[
  {"id":"ai:UAL123","kind":"ai","t_ms":1764100000100,"mode_s":12648416,
   "lat_deg":37.61,"lon_deg":-122.37,"alt_ft_msl":3200,
   "heading_deg":284,"pitch_deg":2.5,"roll_deg":0,
   "groundspeed_kts":210,"vertical_speed_fpm":1500,"track_deg":283,"on_ground":false,
   "gear":0,"flaps":0.25,"speedbrake":0,"thrust":0.8,"lights":7,
   "icao_type":"B738","airline":"UAL","livery":"","destination":"KSFO","callsign":"UAL123"}
 ],
 "removed":["ai:SWA9"]}
```

| Field | Meaning |
| --- | --- |
| `seq` | Per-socket counter, for diagnostics only. |
| `server_time_ms` | Console wall clock when the frame was written. The plugin estimates its clock offset from it (min-filter, see `ClockSync`). |
| `targets[].id` | Stable console track id. `participant:<flight>` for another pilot, `ai:<callsign>` for console-flown traffic. Never this aircraft's own. |
| `targets[].kind` | `participant` or `ai`. |
| `targets[].t_ms` | When this state was **true**: the participant's report time or the AI's simulated time, not the send time. Required. |
| `targets[].mode_s` | 24-bit address. For AI, assigned by the console from a reserved block (below). |
| position | `lat_deg`, `lon_deg`, `alt_ft_msl` are required; the target is skipped without them. |
| motion | `heading_deg`, `track_deg` (defaults to heading), `pitch_deg`, `roll_deg`, `groundspeed_kts`, `vertical_speed_fpm`. Velocities matter: the smoother uses them as curve tangents. |
| config | `gear` 0–1 (defaults down on the ground), `flaps`, `speedbrake`, `thrust` 0–1; `lights` bitmask nav=1 beacon=2 strobe=4 taxi=8 landing=16. |
| identity | `icao_type`, `airline` (ICAO operator), `livery`, `callsign`. Free text last. |
| `targets[].destination` | The field the aircraft filed to, for its tag (`B738 → KSFO`). Additive: empty or absent where nothing was filed, and a console that predates it sends none. A label only; it never changes the model. |
| `removed` | Track ids that are gone. Absence from `targets` does **not** remove a track (frames may be partial); the plugin also expires a track silent for 15 s. |

## Aircraft → console: render fields on `telemetry`

Other pilots can only draw this aircraft well if it reports more than ATC
needs. These keys are added inside the nested `telemetry` body; a console that
predates them ignores them.

| Field | Source dataref |
| --- | --- |
| `track_deg` | `sim/flightmodel/position/hpath` |
| `pitch_deg`, `roll_deg` | `.../theta`, `.../phi` |
| `gear`, `flaps`, `speedbrake`, `thrust` | `flightmodel2` deploy ratios, `throttle_ratio_all` |
| `lights` | `sim/cockpit/electrical/*_lights_on`, same bitmask as above |
| `livery` | last folder of `sim/aircraft/view/acf_livery_path` |

zoal-mp standalone reports `traffic_status: "unavailable"` because it does not
read TCAS, so the console counts it as an impaired reporter rather than as an
empty sky.

## Echo suppression (console-side requirement)

XPMP2 publishes the aircraft it draws to X-Plane's TCAS. A merged zoal-atc then
reports them back in `telemetry.traffic`, and without care the console would
see its own AI aircraft as observed **non-participants**, the very class this
project is meant to replace.

- AI aircraft get `mode_s` from a block the console reserves (the mock uses
  `0xC0FFE0+n`). Surveillance fusion drops any observation whose address is in
  the console's live AI allocation, and keeps the AI track's own state as the
  truth.
- A participant's `mode_s` is whatever their simulator reports. Fusion already
  promotes an observation that matches a participant, so the echo of a pilot
  drawn by XPMP2 merges into that participant.
- With XPMP2 holding TCAS, X-Plane's own AI aircraft are not drawn, so in
  practice the only non-participants left are those from other traffic plugins
  (LiveTraffic and similar) that the user also runs.
