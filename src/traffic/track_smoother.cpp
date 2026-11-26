#include "zoal_atc/traffic/track_smoother.hpp"

#include <algorithm>
#include <cmath>

namespace zoal_atc::traffic {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kKtsToMps = 0.514444;
constexpr double kFpmToFtps = 1.0 / 60.0;
// Local flat-earth scale. Good to well under a metre over the few kilometres
// two consecutive reports can be apart.
constexpr double kMetresPerDegLat = 111132.0;

double metres_per_deg_lon(double lat_deg) {
  return 111320.0 * std::cos(lat_deg * kDegToRad);
}

double wrap_360(double deg) {
  double wrapped = std::fmod(deg, 360.0);
  return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

double lerp(double a, double b, double f) { return a + (b - a) * f; }

// Cubic Hermite on one axis: positions p0, p1, velocities v0, v1 (per second),
// over an interval of dt seconds, at fraction f.
double hermite(double p0, double v0, double p1, double v1, double dt,
               double f) {
  const double f2 = f * f;
  const double f3 = f2 * f;
  const double h00 = 2 * f3 - 3 * f2 + 1;
  const double h10 = f3 - 2 * f2 + f;
  const double h01 = -2 * f3 + 3 * f2;
  const double h11 = f3 - f2;
  return h00 * p0 + h10 * dt * v0 + h01 * p1 + h11 * dt * v1;
}

struct Velocity {
  double east_mps;
  double north_mps;
};

Velocity velocity_of(const Sample &s) {
  const double speed = s.groundspeed_kts * kKtsToMps;
  const double track = s.track_deg * kDegToRad;
  return {speed * std::sin(track), speed * std::cos(track)};
}

} // namespace

double blend_angle_deg(double from, double to, double fraction) {
  double delta = std::fmod(to - from + 540.0, 360.0) - 180.0;
  return wrap_360(from + delta * fraction);
}

Sample interpolate(const Sample &a, const Sample &b, double t_s) {
  const double dt = b.t_s - a.t_s;
  if (dt <= 0.0) {
    return b;
  }
  const double f = std::clamp((t_s - a.t_s) / dt, 0.0, 1.0);

  // Work in metres east/north of `a`.
  const double m_lon = metres_per_deg_lon(a.lat_deg);
  const double bx = (b.lon_deg - a.lon_deg) * m_lon;
  const double by = (b.lat_deg - a.lat_deg) * kMetresPerDegLat;
  const Velocity va = velocity_of(a);
  const Velocity vb = velocity_of(b);

  Sample out = f < 0.5 ? a : b;
  out.t_s = t_s;
  const double x = hermite(0.0, va.east_mps, bx, vb.east_mps, dt, f);
  const double y = hermite(0.0, va.north_mps, by, vb.north_mps, dt, f);
  out.lat_deg = a.lat_deg + y / kMetresPerDegLat;
  out.lon_deg = a.lon_deg + (m_lon != 0.0 ? x / m_lon : 0.0);
  out.alt_ft_msl =
      hermite(a.alt_ft_msl, a.vertical_speed_fpm * kFpmToFtps, b.alt_ft_msl,
              b.vertical_speed_fpm * kFpmToFtps, dt, f);

  out.heading_deg = blend_angle_deg(a.heading_deg, b.heading_deg, f);
  out.track_deg = blend_angle_deg(a.track_deg, b.track_deg, f);
  out.pitch_deg = lerp(a.pitch_deg, b.pitch_deg, f);
  out.roll_deg = lerp(a.roll_deg, b.roll_deg, f);
  out.groundspeed_kts = lerp(a.groundspeed_kts, b.groundspeed_kts, f);
  out.vertical_speed_fpm = lerp(a.vertical_speed_fpm, b.vertical_speed_fpm, f);
  out.gear = lerp(a.gear, b.gear, f);
  out.flaps = lerp(a.flaps, b.flaps, f);
  out.speedbrake = lerp(a.speedbrake, b.speedbrake, f);
  out.thrust = lerp(a.thrust, b.thrust, f);
  // Discrete state (on_ground, lights) switches at the midpoint, from the
  // copy above.
  return out;
}

Sample extrapolate(const Sample &from, double t_s) {
  Sample out = from;
  const double dt = t_s - from.t_s;
  out.t_s = t_s;
  if (dt <= 0.0) {
    return out;
  }
  const Velocity v = velocity_of(from);
  out.lat_deg += v.north_mps * dt / kMetresPerDegLat;
  const double m_lon = metres_per_deg_lon(from.lat_deg);
  if (m_lon != 0.0) {
    out.lon_deg += v.east_mps * dt / m_lon;
  }
  if (!from.on_ground) {
    out.alt_ft_msl += from.vertical_speed_fpm * kFpmToFtps * dt;
  }
  return out;
}

bool TrackSmoother::add(const Sample &sample) {
  if (!std::isfinite(sample.t_s)) {
    return false;
  }
  auto pos = std::upper_bound(
      samples_.begin(), samples_.end(), sample.t_s,
      [](double t, const Sample &s) { return t < s.t_s; });
  if (pos != samples_.begin() && std::prev(pos)->t_s == sample.t_s) {
    return false; // duplicate
  }
  if (pos == samples_.begin() && samples_.size() >= config_.max_samples) {
    return false; // older than everything we are keeping
  }
  samples_.insert(pos, sample);
  while (samples_.size() > config_.max_samples) {
    samples_.pop_front();
  }
  return true;
}

std::optional<Sample> TrackSmoother::evaluate(double t_s) const {
  if (samples_.empty()) {
    return std::nullopt;
  }
  if (t_s <= samples_.front().t_s) {
    Sample out = samples_.front();
    out.t_s = t_s;
    return out;
  }
  const Sample &last = samples_.back();
  if (t_s >= last.t_s) {
    const double ahead = std::min(t_s - last.t_s, config_.max_extrapolation_s);
    return extrapolate(last, last.t_s + ahead);
  }
  auto after = std::upper_bound(
      samples_.begin(), samples_.end(), t_s,
      [](double t, const Sample &s) { return t < s.t_s; });
  const Sample &b = *after;
  const Sample &a = *std::prev(after);
  if (b.t_s - a.t_s > config_.max_interpolation_gap_s) {
    // Bridge a long outage by holding the earlier report's extrapolation,
    // bounded like any other, until the later one is reached.
    const double ahead = std::min(t_s - a.t_s, config_.max_extrapolation_s);
    return extrapolate(a, a.t_s + ahead);
  }
  return interpolate(a, b, t_s);
}

void TrackSmoother::prune_before(double t_s) {
  // Keep one report at or before t_s: it is the left end of the interval
  // that t_s falls in.
  while (samples_.size() > 1 && samples_[1].t_s <= t_s) {
    samples_.pop_front();
  }
}

} // namespace zoal_atc::traffic
