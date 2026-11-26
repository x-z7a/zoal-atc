#include "test_support.hpp"
#include "zoal_atc/traffic/track_smoother.hpp"
#include <iostream>
#include <string>

using namespace zoal_atc::traffic;
using test::near;
using test::require;

namespace {
Sample at(double t, double alt, double vs_fpm, bool ground = false) {
  Sample s;
  s.t_s = t;
  s.lat_deg = 45;
  s.lon_deg = -75;
  s.alt_ft_msl = alt;
  s.vertical_speed_fpm = vs_fpm;
  s.groundspeed_kts = 140;
  s.track_deg = 270;
  s.heading_deg = 270;
  s.on_ground = ground;
  return s;
}

void level_step_stays_level() {
  Sample a = at(100, 1000, -1500);
  Sample b = at(101, 1000, 0);
  for (int i = 0; i <= 20; ++i) {
    double t = 100.0 + i / 20.0;
    Sample o = interpolate(a, b, t);
    near(o.alt_ft_msl, 1000.0, 1e-9, "level step at t=" + std::to_string(t));
  }
}

void descent_never_overshoots() {
  Sample a = at(100, 1000, -1500);
  Sample b = at(101, 975, 0);
  for (int i = 0; i <= 20; ++i) {
    double t = 100.0 + i / 20.0;
    Sample o = interpolate(a, b, t);
    require(o.alt_ft_msl <= 1000.0 + 1e-9 && o.alt_ft_msl >= 975.0 - 1e-9,
            "descent overshoot at t=" + std::to_string(t));
  }
}

void wrong_sign_vs_never_overshoots() {
  Sample a = at(100, 1000, 3000);
  Sample b = at(101, 980, 3000);
  for (int i = 0; i <= 20; ++i) {
    double t = 100.0 + i / 20.0;
    Sample o = interpolate(a, b, t);
    require(980 - 1e-9 <= o.alt_ft_msl && o.alt_ft_msl <= 1000 + 1e-9,
            "wrong sign vs overshoot at t=" + std::to_string(t));
  }
}

void huge_vs_never_overshoots() {
  Sample a = at(100, 1000, -20000);
  Sample b = at(101, 990, -20000);
  double prev_alt = 1000.0;
  for (int i = 0; i <= 20; ++i) {
    double t = 100.0 + i / 20.0;
    Sample o = interpolate(a, b, t);
    require(990 - 1e-9 <= o.alt_ft_msl && o.alt_ft_msl <= 1000 + 1e-9,
            "huge vs overshoot at t=" + std::to_string(t));
    require(o.alt_ft_msl <= prev_alt + 1e-9,
            "huge vs not non-increasing at t=" + std::to_string(t));
    prev_alt = o.alt_ft_msl;
  }
}

void consistent_vs_still_curves() {
  Sample a = at(100, 1000, -1500);
  Sample b = at(101, 975, -1500);
  Sample o = interpolate(a, b, 100.5);
  near(o.alt_ft_msl, 987.5, 1e-6, "consistent vs curve");
}

void rolling_on_the_ground_is_linear() {
  Sample a = at(100, 300, 500, true);
  Sample b = at(101, 310, -500, true);
  Sample o = interpolate(a, b, 100.25);
  near(o.alt_ft_msl, 302.5, 1e-9, "ground roll linear");
}

} // namespace

int main() {
  level_step_stays_level();
  descent_never_overshoots();
  wrong_sign_vs_never_overshoots();
  huge_vs_never_overshoots();
  consistent_vs_still_curves();
  rolling_on_the_ground_is_linear();
  std::cout << "track smoother monotone tests passed\n";
  return 0;
}
