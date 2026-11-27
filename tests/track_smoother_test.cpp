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
  require(smoother.size() == 4, "prune removed the interval's left end or the report before it");
  require(smoother.evaluate(2.5).has_value(), "cannot draw after prune");
}

Sample pitched(double t, double pitch) {
  Sample s = eastbound(t, t * 100.0 / 111320.0);
  s.pitch_deg = pitch;
  return s;
}

// The rate an angle is drawn changing at time t, by a small central step.
double pitch_rate(const Sample *before, const Sample &a, const Sample &b, double t) {
  const double h = 1e-4;
  return (interpolate(before, a, b, t + h).pitch_deg - interpolate(before, a, b, t - h).pitch_deg) / (2 * h);
}

// A nose rotating at a 1 Hz feed (#52): blended in a straight line, its rate
// stepped at every report, from one interval's rate to the next's. Drawn as a
// curve, the rate arriving at a report is the rate leaving it.
void an_angle_keeps_its_rate_across_a_report() {
  const Sample s0 = pitched(0, 0), s1 = pitched(1, 2), s2 = pitched(2, 6);
  const double arriving = pitch_rate(&s0, s0, s1, 1.0 - 1e-3);
  const double leaving = pitch_rate(&s0, s1, s2, 1.0 + 1e-3);
  near(arriving, leaving, 0.05, "the pitch rate stepped at the report");
  near(interpolate(&s0, s1, s2, 1.0).pitch_deg, 2.0, 1e-9, "the curve passes through the report");
  near(interpolate(&s0, s1, s2, 2.0).pitch_deg, 6.0, 1e-9, "and the next");
}

// A turn that stops or reverses keeps its rate across the report it does so
// at (#58): a monotone limit zeroed or scaled the rate there, which stepped
// the drawn rate at every stop and reversal.
void a_stop_or_reversal_keeps_its_rate_across_the_report() {
  const Sample s0 = pitched(0, 0), s1 = pitched(1, 3);
  const double arriving = pitch_rate(&s0, s0, s1, 1.0 - 1e-3);
  for (const double next : {1.0, 3.0, 3.2}) { // reversing, stopping, stopping hard
    const Sample s2 = pitched(2, next);
    near(pitch_rate(&s0, s1, s2, 1.0 + 1e-3), arriving, 0.05, "the pitch rate stepped at the report");
    near(interpolate(&s0, s1, s2, 2.0).pitch_deg, next, 1e-9, "and still meets the next report");
  }
}

// The curve carries the rate it arrived with past the next report by no more
// than that rate times the interval times 4/27, and comes back to it.
void an_angle_overshoots_by_no_more_than_its_rate_carries_it() {
  const Sample s0 = pitched(0, 0), s1 = pitched(1, 8), s2 = pitched(2, 8.5);
  const double bound = (8.0 - 0.5) * 1.0 * 4.0 / 27.0;
  for (int i = 0; i <= 100; ++i) {
    const double p = interpolate(&s0, s1, s2, 1.0 + i / 100.0).pitch_deg;
    require(p >= 8.0 - 1e-9 && p <= 8.5 + bound + 1e-9, "the pitch swung past what its rate carries it");
  }
}

// Nothing after b shapes [a, b]: a report landing while it is drawn changes
// nothing already on screen.
void a_later_report_does_not_change_the_curve() {
  TrackSmoother smoother;
  smoother.add(pitched(0, 0));
  smoother.add(pitched(1, 2));
  smoother.add(pitched(2, 6));
  const double before = smoother.evaluate(1.5)->pitch_deg;
  smoother.add(pitched(3, -4));
  near(smoother.evaluate(1.5)->pitch_deg, before, 1e-12, "a later report moved the curve being drawn");
}

// The smoother draws [a, b] with the report before a, after pruning too.
void the_smoother_uses_the_report_before() {
  TrackSmoother smoother;
  const Sample s0 = pitched(0, 0), s1 = pitched(1, 2), s2 = pitched(2, 6);
  smoother.add(s0);
  smoother.add(s1);
  smoother.add(s2);
  smoother.prune_before(1.5);
  near(smoother.evaluate(1.5)->pitch_deg, interpolate(&s0, s1, s2, 1.5).pitch_deg, 1e-12,
       "the report before the interval was not used");
}

} // namespace

int main() {
  an_angle_keeps_its_rate_across_a_report();
  a_stop_or_reversal_keeps_its_rate_across_the_report();
  an_angle_overshoots_by_no_more_than_its_rate_carries_it();
  a_later_report_does_not_change_the_curve();
  the_smoother_uses_the_report_before();
  straight_line_interpolates_linearly();
  heading_takes_the_short_way();
  extrapolation_is_bounded();
  late_reports_are_ordered_and_duplicates_dropped();
  turns_follow_the_velocity();
  prune_keeps_the_left_end();
  std::cout << "track_smoother_test: ok\n";
  return 0;
}
