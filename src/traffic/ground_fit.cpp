#include "zoal_atc/traffic/ground_fit.hpp"

#include <algorithm>
#include <cmath>

namespace zoal_atc::traffic {

double GroundFit::update(double drawn_y, double terrain_y, bool on_ground, double dt_s) {
  double target = 0.0;
  if (on_ground) {
    target = terrain_y - drawn_y;
    ground_offset_ = target;
  } else {
    // Height as it will be drawn with the last ground correction, so the fade
    // is measured from the ground the model actually left.
    const double height = drawn_y + ground_offset_ - terrain_y;
    const double keep =
        config_.fade_height_m > 0.0 ? std::clamp(1.0 - height / config_.fade_height_m, 0.0, 1.0)
                                    : 0.0;
    target = ground_offset_ * keep;
  }

  if (!started_ || !std::isfinite(offset_)) {
    offset_ = target;
    started_ = true;
  } else {
    const double step = config_.rate_mps * std::max(0.0, dt_s);
    offset_ += std::clamp(target - offset_, -step, step);
  }

  // Never through the ground: while a touchdown correction eases in, a console
  // ground below the mesh would otherwise sink the wheels into it.
  return std::max(drawn_y + offset_, terrain_y);
}

} // namespace zoal_atc::traffic
