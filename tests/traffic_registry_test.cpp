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

// An AI target arrives ahead of the console clock and says it is drawn at the
// present (render_delay_ms 0); a pilot's says nothing and is drawn at the
// registry's delay. Each is drawn at its own.
void each_target_is_drawn_at_its_own_delay() {
  TrafficRegistry::Config config;
  config.interpolation_delay_s = 1.5;
  TrafficRegistry registry(config);
  auto ai0 = target("ai", 100);
  auto ai1 = target("ai", 102);
  ai0.render_delay_s = 0.0;
  ai1.render_delay_s = 0.0;
  ai1.sample.lat_deg = 37.002;
  auto p0 = target("pilot", 99);
  auto p1 = target("pilot", 100);
  p1.sample.lat_deg = 37.002;
  // Server time 100: the AI's samples run to 102, the pilot's to 100. Both
  // are drawn at the middle of a pair, where the smoother's curve meets the
  // straight line.
  registry.apply(frame_at(100, {ai0, ai1, p0, p1}), 10);
  auto ai = registry.state("ai", 11.0); // console 101
  require(ai.has_value(), "no AI state");
  near(ai->lat_deg, 37.001, 1e-6, "AI not drawn at the present");
  auto pilot = registry.state("pilot", 11.0); // console 101 - 1.5 = 99.5
  require(pilot.has_value(), "no pilot state");
  near(pilot->lat_deg, 37.001, 1e-6, "pilot not drawn at the registry's delay");

  // A later frame that does not say keeps the delay the track was given.
  auto ai2 = target("ai", 103);
  ai2.sample.lat_deg = 37.003;
  registry.apply(frame_at(101, {ai2}), 11);
  auto later = registry.state("ai", 12.5); // console 102.5
  require(later.has_value(), "no AI state later");
  near(later->lat_deg, 37.0025, 1e-4, "AI's delay forgotten");
}

} // namespace

int main() {
  clock_takes_the_fastest_packet();
  added_remodelled_relabelled_removed();
  silence_expires_a_track();
  own_aircraft_is_never_drawn();
  draws_behind_the_console_clock();
  each_target_is_drawn_at_its_own_delay();
  std::cout << "traffic_registry_test: ok\n";
  return 0;
}
