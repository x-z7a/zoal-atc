#include "test_support.hpp"
#include "zoal_atc/traffic/config_slew.hpp"

#include <iostream>

using zoal_atc::traffic::ConfigSlew;
using zoal_atc::traffic::Sample;
using test::near;

namespace {
Sample config(double gear, double flaps, double speedbrake) {
  Sample s;
  s.gear = gear;
  s.flaps = flaps;
  s.speedbrake = speedbrake;
  return s;
}

void first_call_takes_the_configuration_at_once() {
  ConfigSlew slew;
  Sample s = slew.update(config(1.0, 0.5, 0.0), 0.016);
  near(s.gear, 1.0, 1e-12, "a parked aircraft first drawn with its gear down, not lowering it");
  near(s.flaps, 0.5, 1e-12, "flaps as reported on the first frame");
}

// An old client's gear follows on_ground and goes up in one report at
// lift-off: it is drawn travelling over eight seconds.
void gear_that_steps_travels_at_its_rate() {
  ConfigSlew slew;
  slew.update(config(1.0, 0.0, 0.0), 0.0);
  Sample s = slew.update(config(0.0, 0.0, 0.0), 1.0);
  near(s.gear, 1.0 - 1.0 / 8.0, 1e-12, "one second of gear travel");
  for (int i = 0; i < 7; ++i) {
    s = slew.update(config(0.0, 0.0, 0.0), 1.0);
  }
  near(s.gear, 0.0, 1e-12, "up after eight seconds");
}

void flaps_and_spoilers_travel_at_theirs() {
  ConfigSlew slew;
  slew.update(config(1.0, 0.0, 0.0), 0.0);
  Sample s = slew.update(config(1.0, 1.0, 1.0), 0.5);
  near(s.flaps, 0.05, 1e-12, "flaps at a tenth a second");
  near(s.speedbrake, 0.5, 1e-12, "spoilers in a second");
  near(s.gear, 1.0, 1e-12, "gear not asked to move does not");
}

// A feed already moving at a real rate is drawn as sent.
void a_continuous_feed_is_untouched() {
  ConfigSlew slew;
  slew.update(config(1.0, 0.25, 0.0), 0.0);
  Sample s = slew.update(config(1.0 - 0.1 / 8.0, 0.25, 0.0), 0.1);
  near(s.gear, 1.0 - 0.1 / 8.0, 1e-12, "gear moving at its own rate");
}

void a_bad_frame_time_moves_nothing() {
  ConfigSlew slew;
  slew.update(config(1.0, 0.0, 0.0), 0.0);
  near(slew.update(config(0.0, 0.0, 0.0), -1.0).gear, 1.0, 1e-12, "negative frame time");
}
} // namespace

int main() {
  first_call_takes_the_configuration_at_once();
  gear_that_steps_travels_at_its_rate();
  flaps_and_spoilers_travel_at_theirs();
  a_continuous_feed_is_untouched();
  a_bad_frame_time_moves_nothing();
  std::cout << "config_slew_test: ok\n";
  return 0;
}
