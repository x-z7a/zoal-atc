#include "zoal_atc/telemetry/ownship_identity.hpp"
#include "zoal_atc/telemetry/telemetry_frame.hpp"

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

std::vector<char> blob_of(const std::vector<std::string> &slots) {
  std::vector<char> blob(64 * kTcasStringSlotWidth, '\0');
  for (std::size_t s = 0; s < slots.size(); ++s) {
    for (std::size_t i = 0; i < slots[s].size() && i < kTcasStringSlotWidth; ++i) {
      blob[s * kTcasStringSlotWidth + i] = slots[s][i];
    }
  }
  return blob;
}

void identity_comes_from_slot_zero() {
  const auto got = ownship_identity(0x00001F, blob_of({"JIA5419", "ACA123"}),
                                    blob_of({"SR22", "B738"}));
  require(got.mode_s_id == 0x00001F, "ownship Mode S");
  require(got.callsign == "JIA5419", "ownship callsign comes from slot 0");
  require(got.icao_type == "SR22", "ownship type comes from slot 0");
}

// An unterminated 8-byte record must not run into the next one, and non-ASCII
// bytes must not corrupt the frame.
void records_are_fixed_width_and_sanitised() {
  auto blob = blob_of({"XXXXXXXX", "", "NEXTSLT"});
  blob[1 * kTcasStringSlotWidth] = static_cast<char>(0x01);
  blob[1 * kTcasStringSlotWidth + 1] = static_cast<char>(0xFF);
  require(tcas_string_at(blob, 0) == "XXXXXXXX", "stops at the slot boundary");
  require(tcas_string_at(blob, 1).empty(), "unprintable bytes yield nothing");
  require(tcas_string_at(blob, 2) == "NEXTSLT", "the following slot is intact");
  require(tcas_string_at(blob, 99).empty(), "out-of-range index yields empty");
  require(tcas_string_at(blob, -1).empty(), "negative index yields empty");
}

// Into Sydney slot 0 went blank on the ground after landing, and the console
// was left with a flight called nothing.
void a_blank_slot_keeps_the_last_callsign() {
  std::string last;
  require(held_callsign("", last).empty(), "nothing held yet is nothing");
  require(held_callsign("ANZ1551", last) == "ANZ1551", "read as it is");
  require(held_callsign("", last) == "ANZ1551", "a blank slot keeps the callsign");
  require(held_callsign("ANZ1552", last) == "ANZ1552", "a new one replaces it at once");
}

// The plugin no longer reports the traffic around it. The block stays on the
// frame, because a console that predates this reads its status, and it says
// what is true: there is no feed.
void the_frame_reports_no_traffic_feed() {
  TelemetrySnapshot s;
  const std::string frame = build_telemetry_frame(s, "sess-1");
  require_contains(frame, "\"traffic_status\":\"unavailable\"", "status key");
  require_contains(frame, "\"traffic\":[]", "empty array still present");
}

} // namespace

int main() {
  identity_comes_from_slot_zero();
  records_are_fixed_width_and_sanitised();
  a_blank_slot_keeps_the_last_callsign();
  the_frame_reports_no_traffic_feed();
  std::cout << "ownship_identity_test: ok\n";
  return 0;
}
