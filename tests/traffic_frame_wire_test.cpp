#include "test_support.hpp"

#include "zoal_atc/traffic/traffic_frame.hpp"

#include <iostream>

using namespace zoal_atc::traffic;
using test::near;
using test::require;

namespace {

TrafficFrame sample_frame() {
  TrafficFrame frame;
  frame.seq = 42;
  frame.server_time_s = 1764000000.123;
  Target ai;
  ai.identity = {"ai:UAL123", TrafficKind::Ai, "UAL123", "B738", "UAL", "", 0xA1B2C3, "KSFO"};
  ai.sample.t_s = 1764000000.100;
  ai.sample.lat_deg = 37.6188056;
  ai.sample.lon_deg = -122.3754167;
  ai.sample.alt_ft_msl = 3200;
  ai.sample.heading_deg = 284;
  ai.sample.groundspeed_kts = 210;
  ai.sample.lights = kLightNav | kLightBeacon | kLightLanding;
  ai.sample.flaps = 0.5;
  ai.render_delay_s = 0.0;
  Target pilot;
  pilot.identity = {"participant:abc", TrafficKind::Participant, "N172SP", "C172", "", "", 0};
  pilot.sample.t_s = 1764000000.050;
  pilot.sample.lat_deg = 37.5;
  pilot.sample.lon_deg = -122.2;
  pilot.sample.alt_ft_msl = 12;
  pilot.sample.on_ground = true;
  pilot.render_delay_s = 1.5;
  frame.targets = {ai, pilot};
  frame.removed = {"ai:SWA9"};
  return frame;
}

void round_trips() {
  auto parsed = parse_traffic_frame(encode_traffic_frame(sample_frame()));
  require(parsed.has_value(), "encoded frame did not parse");
  require(parsed->seq == 42, "seq lost");
  near(parsed->server_time_s, 1764000000.123, 1e-6, "server time lost ms precision");
  require(parsed->targets.size() == 2, "targets lost");
  const auto &ai = parsed->targets[0];
  require(ai.identity.kind == TrafficKind::Ai, "kind lost");
  require(ai.identity.callsign == "UAL123", "callsign lost");
  require(ai.identity.mode_s == 0xA1B2C3, "mode s lost");
  require(ai.identity.destination == "KSFO", "destination lost");
  require(parsed->targets[1].identity.destination.empty(), "destination invented");
  near(ai.sample.t_s, 1764000000.100, 1e-6, "target time lost ms precision");
  near(ai.sample.lat_deg, 37.6188056, 1e-7, "latitude lost precision");
  require(ai.sample.lights == (kLightNav | kLightBeacon | kLightLanding), "lights lost");
  require(parsed->targets[1].sample.on_ground, "on_ground lost");
  require(parsed->targets[1].identity.kind == TrafficKind::Participant, "participant kind lost");
  require(parsed->removed.size() == 1 && parsed->removed[0] == "ai:SWA9", "removed lost");
  require(ai.render_delay_s.has_value(), "render delay lost");
  near(*ai.render_delay_s, 0.0, 1e-9, "AI render delay wrong");
  require(parsed->targets[1].render_delay_s.has_value(), "pilot render delay lost");
  near(*parsed->targets[1].render_delay_s, 1.5, 1e-9, "pilot render delay wrong");
}

void other_frames_are_not_traffic() {
  require(!parse_traffic_frame(R"({"type":"atc_reply","text":"traffic"})"),
          "a non-traffic frame parsed as traffic");
  require(!parse_traffic_frame("not json"), "garbage parsed as traffic");
}

// One aircraft with no position must not take the rest of the sky with it.
void a_bad_target_is_skipped() {
  const std::string json =
      R"({"type":"traffic","seq":1,"server_time_ms":1000,"targets":[)"
      R"({"id":"ai:BAD","kind":"ai","t_ms":1000},)"
      R"({"id":"ai:OK","kind":"ai","t_ms":1000,"lat_deg":1,"lon_deg":2,"alt_ft_msl":3}],"removed":[]})";
  auto parsed = parse_traffic_frame(json);
  require(parsed.has_value(), "frame with one bad target was rejected");
  require(parsed->targets.size() == 1 && parsed->targets[0].identity.id == "ai:OK",
          "bad target was not skipped");
}

void defaults_are_sensible() {
  const std::string json =
      R"({"type":"traffic","seq":1,"server_time_ms":1000,"targets":[)"
      R"({"id":"x","t_ms":1000,"lat_deg":1,"lon_deg":2,"alt_ft_msl":3,"heading_deg":90,"on_ground":true}]})";
  auto parsed = parse_traffic_frame(json);
  require(parsed && parsed->targets.size() == 1, "minimal target rejected");
  const auto &s = parsed->targets[0].sample;
  near(s.track_deg, 90, 1e-9, "track did not default to heading");
  near(s.gear, 1.0, 1e-9, "gear did not default down on the ground");
  require(parsed->targets[0].identity.kind == TrafficKind::Unknown, "missing kind not unknown");
  require(!parsed->targets[0].render_delay_s.has_value(),
          "a console that predates render_delay_ms named a delay");
}

} // namespace

int main() {
  round_trips();
  other_frames_are_not_traffic();
  a_bad_target_is_skipped();
  defaults_are_sensible();
  std::cout << "traffic_frame_test: ok\n";
  return 0;
}
