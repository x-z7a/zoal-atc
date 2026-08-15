#include "zoal_atc/telemetry/traffic.hpp"
#include "zoal_atc/telemetry/telemetry_frame.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace zoal_atc::telemetry;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void require_contains(const std::string &haystack, const std::string &needle,
                      const std::string &message) {
  require(haystack.find(needle) != std::string::npos,
          message + " (missing `" + needle + "` in `" + haystack + "`)");
}

// A full-width set of arrays, all slots empty. Tests fill the slots they care
// about, which is also how the real feed arrives: sparse.
TrafficArrays empty_arrays() {
  TrafficArrays a;
  a.modeS_id.assign(kTcasSlots, 0);
  a.lat.assign(kTcasSlots, 0.f);
  a.lon.assign(kTcasSlots, 0.f);
  a.ele_m.assign(kTcasSlots, 0.f);
  a.vertical_speed_fpm.assign(kTcasSlots, 0.f);
  a.hpath_deg.assign(kTcasSlots, 0.f);
  a.psi_deg.assign(kTcasSlots, 0.f);
  a.v_msc.assign(kTcasSlots, 0.f);
  a.weight_on_wheels.assign(kTcasSlots, 0);
  a.flight_id.assign(kTcasSlots * kTcasStringSlotWidth, '\0');
  a.icao_type.assign(kTcasSlots * kTcasStringSlotWidth, '\0');
  return a;
}

void put_string(std::vector<char> &blob, int index, const std::string &value) {
  const int base = index * kTcasStringSlotWidth;
  for (int i = 0; i < kTcasStringSlotWidth; ++i) {
    blob[base + i] = '\0';
  }
  for (std::size_t i = 0; i < value.size() && i < kTcasStringSlotWidth - 1; ++i) {
    blob[base + static_cast<int>(i)] = value[i];
  }
}

// Places a target at `slot`, offset north of own by roughly `north_nm`.
void put_target(TrafficArrays &a, int slot, double own_lat, double own_lon,
                double north_nm, const std::string &callsign,
                const std::string &type) {
  a.modeS_id[slot] = 0x111000 + slot;
  a.lat[slot] = static_cast<float>(own_lat + north_nm / 60.0);
  a.lon[slot] = static_cast<float>(own_lon);
  a.ele_m[slot] = 300.f;
  a.v_msc[slot] = 60.f;
  a.hpath_deg[slot] = 90.f;
  a.psi_deg[slot] = 92.f;
  put_string(a.flight_id, slot, callsign);
  put_string(a.icao_type, slot, type);
}

const OwnPosition kOwn{45.0, -75.0, 1000.0};

// Slot 0 is the user's own aircraft. Emitting it would have the console issue
// ownship a traffic advisory about itself at zero range, which the conflict tier
// would immediately promote to a safety alert.
void test_skips_ownship_slot() {
  TrafficArrays a = empty_arrays();
  put_target(a, 0, kOwn.lat_deg, kOwn.lon_deg, 0.0, "OWNSHIP", "C172");
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 2.0, "ACA123", "B738");

  const TrafficExtract got = extract_targets(a, 2, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 1, "ownship slot must not be emitted");
  require(got.targets[0].callsign == "ACA123", "wrong target survived");
}

// X-Plane documents that populated entries need not be consecutive, so num_acf
// is a count and not a high-water mark. Looping 0..num_acf-1 would miss these.
void test_non_consecutive_slots() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 1.0, "AAA", "C172");
  put_target(a, 7, kOwn.lat_deg, kOwn.lon_deg, 2.0, "BBB", "C172");
  put_target(a, 40, kOwn.lat_deg, kOwn.lon_deg, 3.0, "CCC", "C172");

  const TrafficExtract got = extract_targets(a, 3, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 3,
          "sparse slots must all be found (got " +
              std::to_string(got.targets.size()) + ")");
  require(got.status == TrafficStatus::current, "status should be current");
}

// A count larger than the number of populated slots must not over-read or
// invent targets.
void test_overstated_count_is_safe() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 1.0, "AAA", "C172");

  const TrafficExtract got = extract_targets(a, 50, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 1, "only populated slots may be emitted");
}

// The byte arrays are 64 fixed-width 8-byte records, not packed NUL-separated
// strings: adjacent callsigns must not bleed into one another.
void test_fixed_width_string_records() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 1.0, "ACA123", "B738");
  put_target(a, 2, kOwn.lat_deg, kOwn.lon_deg, 2.0, "WJA456", "B38M");

  const TrafficExtract got = extract_targets(a, 2, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 2, "both targets expected");
  require(got.targets[0].callsign == "ACA123",
          "slot 1 callsign bled: `" + got.targets[0].callsign + "`");
  require(got.targets[1].callsign == "WJA456",
          "slot 2 callsign bled: `" + got.targets[1].callsign + "`");
  require(got.targets[0].icao_type == "B738", "slot 1 type wrong");
  require(got.targets[1].icao_type == "B38M", "slot 2 type wrong");
}

// An unterminated 8-byte record must not run into the next one, and non-ASCII
// bytes must not corrupt the frame.
void test_string_sanitising() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 1.0, "", "");
  // Fill slot 1's callsign entirely, leaving no NUL terminator.
  for (int i = 0; i < kTcasStringSlotWidth; ++i) {
    a.flight_id[kTcasStringSlotWidth + i] = 'X';
  }
  // Slot 2 holds junk bytes, and a recognisable callsign lives in slot 3 so an
  // over-read would be visible rather than merely long.
  put_target(a, 2, kOwn.lat_deg, kOwn.lon_deg, 2.0, "", "");
  a.flight_id[2 * kTcasStringSlotWidth] = static_cast<char>(0x01);
  a.flight_id[2 * kTcasStringSlotWidth + 1] = static_cast<char>(0xFF);
  put_target(a, 3, kOwn.lat_deg, kOwn.lon_deg, 3.0, "NEXTSLT", "C172");

  const TrafficExtract got = extract_targets(a, 3, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 3, "three targets expected");
  // The invariant is the slot boundary, not the length: an unterminated record
  // consumes its own 8 bytes and must not run into the following slot.
  require(got.targets[0].callsign == "XXXXXXXX",
          "unterminated record must stop at the slot boundary, got `" +
              got.targets[0].callsign + "`");
  require(got.targets[0].callsign.find("NEXT") == std::string::npos,
          "an unterminated record bled into the next slot");
  require(got.targets[1].callsign.empty(),
          "unprintable bytes must yield an empty callsign, got `" +
              got.targets[1].callsign + "`");
  require(got.targets[2].callsign == "NEXTSLT", "the following slot is intact");
}

// Ground contact comes from weight_on_wheels, never from an altitude epsilon.
void test_weight_on_wheels_drives_on_ground() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 0.5, "GND", "C172");
  a.weight_on_wheels[1] = 1;
  a.ele_m[1] = 114.f; // field elevation: AGL would be ~0
  put_target(a, 2, kOwn.lat_deg, kOwn.lon_deg, 1.0, "AIR", "C172");

  const TrafficExtract got = extract_targets(a, 2, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 2, "both targets expected");
  require(got.targets[0].on_ground, "weight_on_wheels target must read on_ground");
  require(!got.targets[1].on_ground, "airborne target must not read on_ground");
}

void test_radius_and_vertical_bounds() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 5.0, "NEAR", "C172");
  put_target(a, 2, kOwn.lat_deg, kOwn.lon_deg, 80.0, "FAR", "C172");
  put_target(a, 3, kOwn.lat_deg, kOwn.lon_deg, 6.0, "HIGH", "C172");
  a.ele_m[3] = 12000.f; // ~39,000 ft: far above the vertical cut

  const TrafficExtract got = extract_targets(a, 3, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 1,
          "only the near, co-altitude target survives (got " +
              std::to_string(got.targets.size()) + ")");
  require(got.targets[0].callsign == "NEAR", "wrong target survived");
  require(!got.truncated, "range/vertical drops must not set truncated");

  // The census is the whole point: a frame carrying one target out of three is
  // indistinguishable from a broken feed unless it says what happened to the
  // other two. Observed at KCVG 2026-08-11 — four aircraft in the sim, one on
  // the console, and no way to tell whether that was a filter or a failure.
  require(got.census.reported == 3, "the count dataref must be reported");
  require(got.census.seen == 3, "all three populated slots were seen");
  require(got.census.dropped_range == 1, "the 80 NM target is a range drop");
  require(got.census.dropped_vertical == 1, "the 39,000 ft target is a vertical drop");
  require(got.census.dropped_cap == 0, "nothing was capped here");
}

// The census accounts for the cap too, so "32 targets" never silently means
// "32 of 40".
void test_census_counts_capped_targets() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 1.0, "ONE", "C172");
  put_target(a, 2, kOwn.lat_deg, kOwn.lon_deg, 2.0, "TWO", "C172");
  put_target(a, 3, kOwn.lat_deg, kOwn.lon_deg, 3.0, "THREE", "C172");

  TrafficBounds bounds;
  bounds.max_targets = 1;
  const TrafficExtract got = extract_targets(a, 3, kOwn, bounds, true);
  require(got.census.seen == 3, "three slots were populated");
  require(got.census.dropped_cap == 2, "two targets were dropped by the cap");
  require(got.targets.size() == 1, "one target survives the cap");
}

// An empty sky is a census of zero, not an absent one — and an unresolved
// dataref set reports nothing at all, because it counted nothing.
void test_census_on_an_empty_sky() {
  const TrafficExtract empty =
      extract_targets(empty_arrays(), 1, kOwn, TrafficBounds{}, true);
  require(empty.status == TrafficStatus::known_empty, "empty sky is known_empty");
  require(empty.census.reported == 1, "ownship alone is still a reported count");
  require(empty.census.seen == 0, "no target slots were populated");

  const TrafficExtract none =
      extract_targets(empty_arrays(), 4, kOwn, TrafficBounds{}, false);
  require(none.status == TrafficStatus::unavailable, "unresolved stays unavailable");
  require(none.census.seen == 0, "an unavailable feed counted nothing");
}

// Nearest-first, and the count cap reports itself.
void test_count_cap_sorts_and_reports() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 9.0, "FAR", "C172");
  put_target(a, 2, kOwn.lat_deg, kOwn.lon_deg, 3.0, "MID", "C172");
  put_target(a, 3, kOwn.lat_deg, kOwn.lon_deg, 1.0, "CLOSE", "C172");

  TrafficBounds bounds;
  bounds.max_targets = 2;
  const TrafficExtract got = extract_targets(a, 3, kOwn, bounds, true);
  require(got.targets.size() == 2, "cap must be honoured");
  require(got.targets[0].callsign == "CLOSE", "nearest must sort first");
  require(got.targets[1].callsign == "MID", "second nearest must sort second");
  require(got.truncated, "the count cap must report truncation");
}

// The two states that must never be confused.
void test_unresolved_datarefs_are_unavailable() {
  const TrafficArrays a = empty_arrays();
  const TrafficExtract got = extract_targets(a, 0, kOwn, TrafficBounds{}, false);
  require(got.status == TrafficStatus::unavailable,
          "unresolved datarefs must be unavailable");
  require(got.targets.empty(), "unavailable must carry no targets");
}

void test_empty_sky_is_known_empty() {
  const TrafficArrays a = empty_arrays();
  const TrafficExtract got = extract_targets(a, 0, kOwn, TrafficBounds{}, true);
  require(got.status == TrafficStatus::known_empty,
          "resolved datarefs with no targets are positive known_empty evidence");
  require(got.targets.empty(), "known_empty must carry no targets");
}

// A slot at the null island is an unpopulated slot, not an aircraft off Africa.
void test_null_island_slots_are_not_targets() {
  TrafficArrays a = empty_arrays();
  a.modeS_id[5] = 12345; // an id, but no position
  const TrafficExtract got = extract_targets(a, 1, kOwn, TrafficBounds{}, true);
  require(got.targets.empty(), "a positionless slot is not a target");
  require(got.status == TrafficStatus::known_empty, "and the sky reads empty");
}

// A target with no mode-S id is still a target: the id is identity, not
// validity, and X-Plane does not always publish one.
void test_zero_modes_id_still_emitted() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 2.0, "NOID", "C172");
  a.modeS_id[1] = 0;

  const TrafficExtract got = extract_targets(a, 1, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 1, "a zero mode-S id must not drop the target");
  require(got.targets[0].callsign == "NOID", "wrong target");
}

// Short arrays (a dataref that read fewer elements) must degrade, not crash.
void test_short_arrays_degrade() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 2.0, "AAA", "C172");
  a.weight_on_wheels.clear();
  a.psi_deg.clear();
  a.icao_type.clear();

  const TrafficExtract got = extract_targets(a, 1, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 1, "a partial dataref set must still yield the target");
  require(!got.targets[0].on_ground, "missing weight_on_wheels reads as airborne");
  require(got.targets[0].icao_type.empty(), "missing type reads as unknown");
}

void test_unit_conversions() {
  TrafficArrays a = empty_arrays();
  put_target(a, 1, kOwn.lat_deg, kOwn.lon_deg, 2.0, "AAA", "C172");
  a.ele_m[1] = 304.8f;  // 1000 ft
  a.v_msc[1] = 51.4444f; // 100 kt

  const TrafficExtract got = extract_targets(a, 1, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 1, "target expected");
  require(std::fabs(got.targets[0].alt_ft_msl - 1000.0) < 1.0,
          "metres must convert to feet");
  require(std::fabs(got.targets[0].groundspeed_kts - 100.0) < 1.0,
          "m/s must convert to knots");
}

// The frame is the plugin<->console contract; these keys are what
// pluginws.TelemetrySnapshot decodes.
void test_frame_carries_traffic_block() {
  TelemetrySnapshot s;
  s.traffic_status = TrafficStatus::current;
  s.traffic_truncated = true;
  TrafficTarget t;
  t.id = 0x111001;
  t.callsign = "ACA123";
  t.icao_type = "B738";
  t.lat_deg = 45.32;
  t.lon_deg = -75.66;
  t.alt_ft_msl = 2100.0;
  t.groundspeed_kts = 140.0;
  t.vertical_speed_fpm = -640.0;
  t.track_deg = 68.0;
  t.heading_deg = 71.0;
  t.on_ground = false;
  s.traffic.push_back(t);

  const std::string frame = build_telemetry_frame(s, "sess-1");
  require_contains(frame, "\"traffic_status\":\"current\"", "status key");
  require_contains(frame, "\"traffic_truncated\":true", "truncated key");
  require_contains(frame, "\"callsign\":\"ACA123\"", "callsign key");
  require_contains(frame, "\"icao_type\":\"B738\"", "type key");
  require_contains(frame, "\"on_ground\":false", "on_ground key");
  require_contains(frame, "\"track_deg\":68", "track key");
}

// An unavailable feed must still say so on the wire — silence would be read as
// an empty sky by a console that only sees an absent array.
void test_frame_reports_unavailable() {
  TelemetrySnapshot s;
  s.traffic_status = TrafficStatus::unavailable;
  const std::string frame = build_telemetry_frame(s, "sess-1");
  require_contains(frame, "\"traffic_status\":\"unavailable\"", "status key");
  require_contains(frame, "\"traffic\":[]", "empty array still present");
}

void test_string_helper_bounds() {
  std::vector<char> blob(kTcasSlots * kTcasStringSlotWidth, '\0');
  require(tcas_string_at(blob, 99).empty(), "out-of-range index yields empty");
  require(tcas_string_at(blob, -1).empty(), "negative index yields empty");
}

// The shipped cap, not one a test invented. A hub with a full AI traffic set
// puts more than sixteen aircraft inside 30 NM, and the cap is nearest-first,
// so a low one quietly drops the far ones. 32 of the 64 slots.
void test_default_cap_is_thirty_two() {
  TrafficArrays a = empty_arrays();
  const int populated = 40;
  for (int i = 1; i <= populated; ++i) {
    put_target(a, i, kOwn.lat_deg, kOwn.lon_deg, 0.5 * i, "AC" + std::to_string(i), "C172");
  }

  const TrafficExtract got =
      extract_targets(a, populated + 1, kOwn, TrafficBounds{}, true);
  require(got.targets.size() == 32,
          "default cap must keep 32 targets (got " +
              std::to_string(got.targets.size()) + ")");
  require(got.census.seen == populated, "all forty populated slots were seen");
  require(got.census.dropped_cap == populated - 32,
          "the census must account for the eight the cap dropped");
  require(got.truncated, "a capped picture still reports truncation");
  // Nearest-first survives the raise: the closest target is still first.
  require(got.targets[0].callsign == "AC1", "nearest must still sort first");
}

} // namespace

int main() {
  test_skips_ownship_slot();
  test_non_consecutive_slots();
  test_overstated_count_is_safe();
  test_fixed_width_string_records();
  test_string_sanitising();
  test_weight_on_wheels_drives_on_ground();
  test_radius_and_vertical_bounds();
  test_census_counts_capped_targets();
  test_census_on_an_empty_sky();
  test_count_cap_sorts_and_reports();
  test_default_cap_is_thirty_two();
  test_unresolved_datarefs_are_unavailable();
  test_empty_sky_is_known_empty();
  test_null_island_slots_are_not_targets();
  test_zero_modes_id_still_emitted();
  test_short_arrays_degrade();
  test_unit_conversions();
  test_frame_carries_traffic_block();
  test_frame_reports_unavailable();
  test_string_helper_bounds();
  std::cout << "traffic tests passed\n";
  return 0;
}
