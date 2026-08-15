#ifndef ZOAL_ATC_TELEMETRY_TRAFFIC_HPP
#define ZOAL_ATC_TELEMETRY_TRAFFIC_HPP

#include <cstdint>
#include <string>
#include <vector>

// The traffic half of the dataref edge (docs/phase21/02-the-feed.md): turning
// X-Plane's TCAS target arrays into the bounded `traffic[]` wire block.
//
// Everything here is SDK-free and takes already-read arrays, so the extraction
// rules — which are where the traps live — are unit-testable without X-Plane.
// The XPLM reads themselves are glue in xplane_plugin.cpp.
//
// The plugin holds no ATC logic: it filters by distance to keep the frame small
// (the same bounding role apt.dat bbox scoping already plays in navdata) and
// makes no decision about the targets it keeps. Geometry, phase and every
// authorization stay console-side.
namespace zoal_atc::telemetry {

// Number of slots in every `sim/cockpit2/tcas/targets/…` array.
inline constexpr int kTcasSlots = 64;
// Width of one record in the `byte[512]` string arrays: 7 characters + NUL.
inline constexpr int kTcasStringSlotWidth = 8;

// TrafficStatus is the plugin's claim about the feed itself, and is the reason
// the console can tell "the sky is empty" from "I have no picture".
//
// `stale` is deliberately absent: a frame cannot know it is late, so the console
// derives that from frame age (docs/phase21/01-non-participating-traffic.md).
enum class TrafficStatus {
  // The datarefs did not resolve. Never conflate with zero aircraft.
  unavailable,
  // The datarefs resolved and no slot holds a valid target. Positive evidence.
  known_empty,
  // The datarefs resolved and at least one target is present.
  current,
};

const char *traffic_status_name(TrafficStatus status);

// TrafficTarget mirrors one entry of the `traffic[]` wire block. Values are
// already in wire units (degrees, feet, knots, ft/min).
//
// Only observations appear here. Everything derived — clock position, range,
// closure, phase, wake — is computed console-side by `internal/traffic`, which
// is pure and tested; duplicating any of it here would split the definition
// across two languages.
struct TrafficTarget {
  std::int32_t id = 0; // mode-S / ICAO hexcode; 0 when the sim publishes none
  std::string callsign;
  std::string icao_type;
  double lat_deg = 0;
  double lon_deg = 0;
  double alt_ft_msl = 0;
  double groundspeed_kts = 0;
  double vertical_speed_fpm = 0;
  double track_deg = 0;   // hpath, true
  double heading_deg = 0; // psi, true
  // Ground contact from `weight_on_wheels`, not inferred from altitude. The
  // console's on-surface test used an AGL epsilon, which excluded the exact
  // case it needed to catch — an aircraft parked on the runway reads AGL 0.
  bool on_ground = false;
};

// TrafficArrays is one raw read of the TCAS datarefs: the parallel arrays
// exactly as XPLMGetDatav* returns them. Short vectors are tolerated (a missing
// field reads as its zero value) so a partial dataref set degrades rather than
// failing the whole frame.
//
// `flight_id` and `icao_type` are the raw byte[512] blobs — fixed-width 8-byte
// records, NOT a packed list of NUL-separated strings.
struct TrafficArrays {
  std::vector<std::int32_t> modeS_id;
  std::vector<float> lat;
  std::vector<float> lon;
  std::vector<float> ele_m;
  std::vector<float> vertical_speed_fpm;
  std::vector<float> hpath_deg;
  std::vector<float> psi_deg;
  std::vector<float> v_msc; // total true speed, m/s
  std::vector<std::int32_t> weight_on_wheels;
  std::vector<char> flight_id;
  std::vector<char> icao_type;
};

// OwnPosition is the ownship reference the distance/vertical bounds are taken
// from. It is used only to filter and sort — never to derive geometry.
struct OwnPosition {
  double lat_deg = 0;
  double lon_deg = 0;
  double alt_ft_msl = 0;
};

// TrafficBounds keeps the frame small. Defaults sit well outside every decision
// range the console uses (advisory 5 NM, conflict 2 NM, go-around 1.2 NM), so
// nothing a controller could act on is ever filtered away.
struct TrafficBounds {
  double radius_nm = 30.0;
  double vertical_ft = 10000.0;
  // 32 of 64 slots. A hub with a full AI traffic set puts more than 16 aircraft
  // inside 30 NM, and the cap sorts nearest-first, so the ones it was dropping
  // were the ones furthest away — but the frame is small enough that there is
  // no reason to make the operator wonder about them (raised from 16 after
  // KCVG, 2026-08-11).
  int max_targets = 32;
};

// TrafficCensus accounts for every aircraft the sim had against every one the
// frame carries. Without it, "one target" and "the sim says four aircraft" are
// irreconcilable from the console side, and the difference could be a bound
// working as designed or the feed silently failing — which is the whole
// distinction phase 21 exists to keep visible.
struct TrafficCensus {
  // reported is the count dataref: aircraft in the sim's TCAS list, ownship
  // included. seen is the populated target slots actually found, ownship
  // excluded. They disagree when slots are stale or unpopulated.
  int reported = 0;
  int seen = 0;
  // Why the frame carries fewer targets than were seen.
  int dropped_range = 0;
  int dropped_vertical = 0;
  int dropped_cap = 0;
};

// TrafficExtract is one snapshot's worth of bounded targets plus the claim about
// the feed behind them.
struct TrafficExtract {
  std::vector<TrafficTarget> targets;
  TrafficStatus status = TrafficStatus::unavailable;
  // True when the count cap dropped targets that were otherwise in range — a
  // truncated picture that looked complete is how a controller ends up
  // separating from the wrong aircraft.
  bool truncated = false;
  // The full accounting. The bounds are outside every decision range, so the
  // dropped targets are not a safety matter; they are an observability one, and
  // reporting nothing about them made a working filter indistinguishable from a
  // broken feed (observed at KCVG 2026-08-11: four aircraft in the sim, one on
  // the console, and no way to tell why).
  TrafficCensus census;
};

// extract_targets turns one raw TCAS read into the wire block.
//
// `num_acf` is the count dataref. It is a COUNT, not a high-water mark —
// X-Plane documents that populated entries need not be consecutive — so it is
// used only to stop scanning early, never as a loop bound.
//
// `datarefs_resolved` is false when the SDK glue could not find the datarefs, in
// which case the result is `unavailable` with no targets. That distinction
// cannot be made here, which is why it is a parameter: reporting zero aircraft
// for a dataref that does not exist is precisely the bug this phase exists to
// prevent.
//
// Slot 0 is ownship and is never emitted.
TrafficExtract extract_targets(const TrafficArrays &arrays, int num_acf,
                               const OwnPosition &own,
                               const TrafficBounds &bounds,
                               bool datarefs_resolved);

// tcas_string_at reads one fixed-width record out of a byte[512] array,
// stopping at the first NUL and keeping only printable ASCII. An out-of-range
// index, or a slot holding nothing usable, yields "" — a legitimate "unknown",
// which the phraseology already handles by omitting the clause.
std::string tcas_string_at(const std::vector<char> &blob, int index,
                           int slot_width = kTcasStringSlotWidth);

// traffic_distance_nm is the bounding distance between two points. It is an
// equirectangular approximation, which is ample for a 30 NM cut-off and is
// deliberately NOT the console's geometry — that stays in internal/traffic.
double traffic_distance_nm(double lat1, double lon1, double lat2, double lon2);

} // namespace zoal_atc::telemetry

#endif // ZOAL_ATC_TELEMETRY_TRAFFIC_HPP
