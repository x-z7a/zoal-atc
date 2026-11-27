#ifndef ZOAL_ATC_TRAFFIC_TRACK_SMOOTHER_HPP
#define ZOAL_ATC_TRAFFIC_TRACK_SMOOTHER_HPP

#include "zoal_atc/traffic/target.hpp"

#include <cstddef>
#include <deque>
#include <optional>

namespace zoal_atc::traffic {

// TrackSmoother turns a stream of irregular, late, sometimes reordered position
// reports into a state that can be drawn every frame without visible stepping.
//
// It renders the past, not the present: the caller asks for a time a little
// behind the newest report (the interpolation delay), so that almost every
// frame falls between two real reports. Between them, position follows a cubic
// Hermite curve whose end tangents are the reported velocities, so a turning
// aircraft draws an arc rather than a polygon; heading, pitch and roll follow
// curves whose rates are continuous across reports (taken from the interval
// before), and configuration is blended. Past the newest report it dead-reckons along the
// last velocity for a bounded time and then holds, so a feed that stops shows
// as an aircraft that stops, not one that flies off.
//
// Pure: no clocks, no simulator. Time is always passed in.
class TrackSmoother {
public:
  struct Config {
    std::size_t max_samples = 16;
    double max_extrapolation_s = 3.0;
    // A gap between reports longer than this is not interpolated across: the
    // aircraft jumps rather than flying a curve through a long outage.
    double max_interpolation_gap_s = 10.0;
  };

  TrackSmoother() = default;
  explicit TrackSmoother(Config config) : config_(config) {}

  // Inserts a report in time order. A duplicate or older-than-everything-kept
  // report is ignored. Returns whether it was kept.
  bool add(const Sample &sample);

  // The state to draw at console time `t_s`, or nullopt before any report.
  [[nodiscard]] std::optional<Sample> evaluate(double t_s) const;

  // Drops reports no longer needed to draw times at or after `t_s`: it keeps
  // the left end of the interval `t_s` is in and the report before that.
  void prune_before(double t_s);

  [[nodiscard]] std::size_t size() const { return samples_.size(); }
  [[nodiscard]] const Sample *newest() const {
    return samples_.empty() ? nullptr : &samples_.back();
  }

private:
  Config config_;
  std::deque<Sample> samples_;
};

// Exposed for tests.
double blend_angle_deg(double from, double to, double fraction);
Sample interpolate(const Sample &a, const Sample &b, double t_s);
// Between a and b, with the report before a where there is one (nullptr where
// not): heading, pitch and roll are drawn as curves whose rate is continuous
// across reports, rather than as straight lines whose rate steps at every
// one. Nothing after b is used, so a report that lands while [a, b] is being
// drawn never changes it.
Sample interpolate(const Sample *before, const Sample &a, const Sample &b, double t_s);
Sample extrapolate(const Sample &from, double t_s);

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_TRACK_SMOOTHER_HPP
