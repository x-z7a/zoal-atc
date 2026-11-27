# Taxi route protocol: follow the greens

Status: **implemented**. Console: `internal/wire/taxi_route.go`,
`internal/guidance`, Ground's `guidance.go` (`~/git/atc`,
`docs/architecture/taxi-guidance.md`). Plugin: `zoal_atc::taxi`
(`include/zoal_atc/taxi`, `src/taxi`) and `src/xplane/taxi_lights.cpp`. Issue
x-z7a/atc#1041.

Once Ground has cleared this aircraft along a taxi route and the pilot has read
it back, the cockpit lights it: green lead-on lights on the stretch ahead of the
aircraft, and a red stop bar at the hold short. Only this cockpit's route is
drawn, and only in this sim, so two pilots at one field each see their own
greens.

The plugin decides nothing about ATC. The console chooses the route, where it
stops and when the lights go out; the plugin chooses how much of it to light
and where the aircraft is along it. A stop bar stays red until the console
sends the next route.

## Negotiation

The aircraft adds `taxi_route_v1` to `capabilities` in its `hello`. The console
sends `taxi_route` frames only on sockets that declared it. The plugin declares
it unless the pilot switched the lights off or its light objects are missing.

## Console → aircraft: `taxi_route`

Sent after every turn Ground decides about the flight, and when the cockpit
tunes Ground. The same route is sent again and again; a route that has not
changed changes nothing.

```json
{"type":"taxi_route","rev":7,"station":"CYOW_GND","route_id":"tx-1","airport":"CYOW",
 "destination_runway":true,
 "points":[{"lat_deg":45.3225,"lon_deg":-75.6692},
           {"lat_deg":45.3230,"lon_deg":-75.6680,"hold_short":"14"}],
 "taxiways":["A","B"],"destination":"14","scenery":"Global Airports"}
```

| key | meaning |
| --- | --- |
| `rev` | the station's state version the route was read from |
| `station` | the position that cleared it |
| `route_id` | the instruction lit; empty when dark |
| `airport` | the field, ICAO |
| `points` | the route in order, from where it was given to where it stops. A point with `hold_short` is a stop bar, and is the last |
| `taxiways`, `destination` | as said, for display |
| `scenery` | the scenery the console drew the route from: a custom scenery package's folder name, or `Global Airports` |

A frame with no `points` is **dark**: that station's lights go out.

The points nest. The plugin reads them with `json_raw_field` +
`json_array_elements` and each element on its own, and the envelope leads with
`type`, its scalars before the arrays and its free text last, so the flat key
search never crosses from one into the other.

### Which route is lit

- A lit route from any station replaces the one shown, except one from the
  same station with an older `rev` (two consoles' carriers can deliver out of
  order).
- A dark frame puts the lights out only when it comes from the station whose
  route is shown, with a `rev` no older.

## Drawing

- apt.dat edges are straight node-to-node lines, so each corner is rounded
  (radius up to 40 m, never more than half of either leg) before the lights
  are placed: every 15 m on straights, every 7.5 m in turns.
- The aircraft is placed along the route by its nearest point, searching only
  ahead of 30 m behind where it was. More than 60 m off the route, nothing is
  lit.
- Only the 400 m ahead of the aircraft are lit; lights behind it are taken
  away. The stop bar is lit once it is within those 400 m.
- Each light is an instanced object (`objects/taxi_lead_on_green.obj`,
  `objects/taxi_stop_bar.obj`) placed on the terrain with a probe, and placed
  again every five seconds because the sim's local origin moves.

## The pilot's setting

`taxi_lights` in `zoal_atc.cfg`:

| value | |
| --- | --- |
| `always` (default) | light every route, whatever scenery the console drew it from |
| `matching` | light a route only when this sim draws the field from the scenery the frame names: the first pack in `Custom Scenery/scenery_packs.ini` whose `apt.dat` has the field, or `Global Airports` |
| `off` | never; the plugin does not declare `taxi_route_v1` |

A console reading other scenery than the pilot's has other pavement, and
`always` can put lights on the grass. That is the pilot's call.
