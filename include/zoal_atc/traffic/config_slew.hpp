#ifndef ZOAL_ATC_TRAFFIC_CONFIG_SLEW_HPP
#define ZOAL_ATC_TRAFFIC_CONFIG_SLEW_HPP

#include "zoal_atc/traffic/target.hpp"

namespace zoal_atc::traffic {

// Moves a drawn aircraft's gear, flaps and spoilers at no more than their
// real travel speeds, whatever the feed says (#52). The console sends these
// moving at their own rates now (x-z7a/atc#1127), and so does a pilot's
// simulator, but a pilot whose client predates the render fields is drawn
// with the gear following on_ground, which flipped up in one report at
// lift-off; and any feed blended between two reports swings a step in it over
// one interval, half a second. Real gear takes about eight seconds.
//
// One per drawn aircraft, on the main thread. Pure: time is passed in.
class ConfigSlew {
public:
  struct Config {
    double gear_per_s = 1.0 / 8.0;
    double flaps_per_s = 0.1;
    double speedbrake_per_s = 1.0;
  };

  ConfigSlew() = default;
  explicit ConfigSlew(Config config) : config_(config) {}

  // `sample` with its gear, flaps and speedbrake moved toward what it says
  // by at most their rates over `dt_s`. The first call takes them at once.
  Sample update(Sample sample, double dt_s);

private:
  Config config_;
  bool started_ = false;
  double gear_ = 0.0, flaps_ = 0.0, speedbrake_ = 0.0;
};

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_CONFIG_SLEW_HPP
