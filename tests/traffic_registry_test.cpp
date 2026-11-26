#include "test_support.hpp"

#include "zoal_atc/traffic/clock_sync.hpp"
#include "zoal_atc/traffic/traffic_registry.hpp"

#include <iostream>

using namespace zoal_atc::traffic;
using test::near;
using test::require;

namespace {

Target target(const std::string &id, double t, const std::string &type = "B738") {
  Target out;
  out.identity.id = id;
  out.identity.kind = TrafficKind::Ai;
  out.identity.callsign = id;
  out.identity.icao_type = type;
  out.sample.t_s = t;
  out.sample.lat_deg = 37;
  out.sample.lon_deg = -122;
  out.sample.alt_ft_msl = 1000;
  return out;
}

TrafficFrame frame_at(double server_t, std::vector<Target> targets,
                      std::vector<std::string> removed = {}) {
  TrafficFrame frame;
  frame.server_time_s = server_t;
  frame.targets = std::move(targets);
  frame.removed = std::move(removed);
  return frame;
}

void clock_takes_the_fastest_packet() {
  ClockSync clock;
  clock.observe(100.0, 1000.30); // 300 ms late
  clock.observe(101.0, 1001.05); // 50 ms late: better
  clock.observe(102.0, 1002.40); // slow packet must not move it up much
  near(clock.offset(), 900.05, 0.01,
       "clock did not keep the least-delayed observation");
}

void added_remodelled_relabelled_removed() {
  TrafficRegistry registry;
  auto c1 = registry.apply(frame_at(100, {target("a", 100), target("b", 100)}), 10);
  require(c1.added.size() == 2, "new tracks not reported as added");

  auto relabel = target("a", 101);
  relabel.identity.callsign = "UAL1";
  auto c2 = registry.apply(frame_at(101, {relabel, target("b", 101, "A320")}), 11);
  require(c2.relabelled.size() == 1 && c2.relabelled[0].id == "a", "callsign change not a relabel");
  require(c2.remodelled.size() == 1 && c2.remodelled[0].id == "b", "type change not a remodel");

  auto c3 = registry.apply(frame_at(102, {}, {"a"}), 12);
  require(c3.removed.size() == 1 && registry.size() == 1, "explicit removal ignored");
}

void silence_expires_a_track() {
  TrafficRegistry::Config config;
  config.stale_after_s = 5;
  TrafficRegistry registry(config);
  registry.apply(frame_at(100, {target("a", 100)}), 10);
  require(registry.expire(14).removed.empty(), "expired too early");
  require(registry.expire(16).removed.size() == 1, "never expired");
}

void own_aircraft_is_never_drawn() {
  TrafficRegistry registry;
  registry.set_own_id("participant:me");
  auto c = registry.apply(frame_at(100, {target("participant:me", 100)}), 10);
  require(c.added.empty() && registry.size() == 0, "own aircraft was added");
}

void draws_behind_the_console_clock() {
  TrafficRegistry::Config config;
  config.interpolation_delay_s = 1.0;
  TrafficRegistry registry(config);
  auto t0 = target("a", 100);
  auto t1 = target("a", 101);
  t1.sample.lat_deg = 37.001;
  registry.apply(frame_at(100, {t0}), 10);
  registry.apply(frame_at(101, {t1}), 11);
  // Local 11.5 is console 101.5; drawing 1 s behind lands at 100.5.
  auto s = registry.state("a", 11.5);
  require(s.has_value(), "no state for a known track");
  require(s->lat_deg > 37.0 && s->lat_deg < 37.001, "did not draw between reports");
}

} // namespace

int main() {
  clock_takes_the_fastest_packet();
  added_remodelled_relabelled_removed();
  silence_expires_a_track();
  own_aircraft_is_never_drawn();
  draws_behind_the_console_clock();
  std::cout << "traffic_registry_test: ok\n";
  return 0;
}
