#include "test_support.hpp"

#include "zoal_atc/traffic/track_smoother.hpp"

#include <cmath>
#include <iostream>

using namespace zoal_atc::traffic;
using test::near;
using test::require;

namespace {

// Due east at 0N, 0E: one degree of longitude is ~111 km, so 100 m/s is
// ~0.000898 deg/s.
Sample eastbound(double t, double lon) {
  Sample s;
  s.t_s = t;
  s.lat_deg = 0.0;
  s.lon_deg = lon;
  s.alt_ft_msl = 5000;
  s.heading_deg = 90;
  s.track_deg = 90;
  s.groundspeed_kts = 100.0 / 0.514444;
  return s;
}

void straight_line_interpolates_linearly() {
  TrackSmoother smoother;
  const double step = 100.0 / 111320.0;
  smoother.add(eastbound(0, 0));
  smoother.add(eastbound(1, step));
  auto mid = smoother.evaluate(0.5);
  require(mid.has_value(), "no state between two reports");
  near(mid->lon_deg, step / 2, 1e-8, "constant velocity did not stay linear");
  near(mid->lat_deg, 0, 1e-9, "straight track drifted north");
}

void heading_takes_the_short_way() {
  near(blend_angle_deg(350, 10, 0.5), 0, 1e-9, "350->10 did not cross north");
  near(blend_angle_deg(10, 350, 0.25), 5, 1e-9, "10->350 went the long way");
}

void extrapolation_is_bounded() {
  TrackSmoother::Config config;
  config.max_extrapolation_s = 2.0;
  TrackSmoother smoother(config);
  smoother.add(eastbound(0, 0));
  auto at2 = smoother.evaluate(2.0);
  auto at10 = smoother.evaluate(10.0);
  require(at2 && at10, "no extrapolated state");
  near(at10->lon_deg, at2->lon_deg, 1e-12, "extrapolated past the bound");
  require(at2->lon_deg > 0, "did not extrapolate along the track");
}

void late_reports_are_ordered_and_duplicates_dropped() {
  TrackSmoother smoother;
  require(smoother.add(eastbound(2, 0.002)), "report rejected");
  require(smoother.add(eastbound(1, 0.001)), "late report rejected");
  require(!smoother.add(eastbound(1, 0.001)), "duplicate accepted");
  auto s = smoother.evaluate(1.5);
  require(s && s->lon_deg > 0.001 && s->lon_deg < 0.002, "late report not placed in order");
}

// A turn reported at its ends should be drawn as an arc that bulges outside
// the chord, not cut across it.
void turns_follow_the_velocity() {
  Sample a = eastbound(0, 0);
  Sample b;
  b = a;
  b.t_s = 10;
  b.lon_deg = 1000.0 / 111320.0;
  b.lat_deg = -1000.0 / 111132.0;
  b.heading_deg = b.track_deg = 180; // now southbound
  Sample mid = interpolate(a, b, 5);
  // The chord's midpoint is (500 m E, 500 m S). An arc from eastbound to
  // southbound passes north-east of it.
  const double east_m = mid.lon_deg * 111320.0;
  const double north_m = mid.lat_deg * 111132.0;
  require(east_m > 500 && north_m > -500, "turn cut inside the chord");
}

void prune_keeps_the_left_end() {
  TrackSmoother smoother;
  for (int i = 0; i < 5; ++i) {
    smoother.add(eastbound(i, i * 0.001));
  }
  smoother.prune_before(2.5);
  require(smoother.size() == 3, "prune removed the interval's left end");
  require(smoother.evaluate(2.5).has_value(), "cannot draw after prune");
}

} // namespace

int main() {
  straight_line_interpolates_linearly();
  heading_takes_the_short_way();
  extrapolation_is_bounded();
  late_reports_are_ordered_and_duplicates_dropped();
  turns_follow_the_velocity();
  prune_keeps_the_left_end();
  std::cout << "track_smoother_test: ok\n";
  return 0;
}
