#include "zoal_atc/traffic/config_slew.hpp"

#include <algorithm>
#include <cmath>

namespace zoal_atc::traffic {

namespace {
double toward(double from, double to, double step) {
  return from + std::clamp(to - from, -step, step);
}
} // namespace

Sample ConfigSlew::update(Sample sample, double dt_s) {
  const double dt = std::isfinite(dt_s) ? std::max(0.0, dt_s) : 0.0;
  if (!started_) {
    gear_ = sample.gear;
    flaps_ = sample.flaps;
    speedbrake_ = sample.speedbrake;
    started_ = true;
    return sample;
  }
  gear_ = toward(gear_, sample.gear, config_.gear_per_s * dt);
  flaps_ = toward(flaps_, sample.flaps, config_.flaps_per_s * dt);
  speedbrake_ = toward(speedbrake_, sample.speedbrake, config_.speedbrake_per_s * dt);
  sample.gear = gear_;
  sample.flaps = flaps_;
  sample.speedbrake = speedbrake_;
  return sample;
}

} // namespace zoal_atc::traffic
