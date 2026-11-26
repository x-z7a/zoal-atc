#include "test_support.hpp"
#include "zoal_atc/traffic/ground_fit.hpp"
#include <iostream>

using zoal_atc::traffic::GroundFit;
using test::near;
using test::require;

namespace {
void first_ground_call_lands_on_terrain() {
  GroundFit g;
  near(g.update(105.0, 100.0, true, 0.0), 100.0, 1e-9, "first_ground_call_lands_on_terrain line 12");
  near(g.offset(), -5.0, 1e-9, "first_ground_call_lands_on_terrain line 13");
}

void console_ground_below_mesh_is_lifted() {
  GroundFit g;
  near(g.update(96.0, 100.0, true, 0.0), 100.0, 1e-9, "console_ground_below_mesh_is_lifted line 18");
}

void touchdown_settles_at_the_rate() {
  GroundFit g;
  near(g.update(200.0, 100.0, false, 0.02), 200.0, 1e-9, "touchdown_settles_at_the_rate line 23");
  double y1 = g.update(106.0, 100.0, true, 1.0);
  near(y1, 103.0, 1e-9, "touchdown_settles_at_the_rate line 25");
  double y2 = g.update(106.0, 100.0, true, 1.0);
  near(y2, 100.0, 1e-9, "touchdown_settles_at_the_rate line 27");
  double y3 = g.update(106.0, 100.0, true, 1.0);
  near(y3, 100.0, 1e-9, "touchdown_settles_at_the_rate line 29");
}

void never_below_terrain_while_settling() {
  GroundFit g;
  g.update(200.0, 100.0, false, 0.02);
  double y = g.update(97.0, 100.0, true, 0.1);
  near(y, 100.0, 1e-9, "never_below_terrain_while_settling line 36");
}

void liftoff_fades_the_correction() {
  GroundFit g;
  g.update(105.0, 100.0, true, 0.0);
  near(g.update(105.0, 100.0, false, 0.02), 100.0, 1e-9, "liftoff_fades_the_correction line 42");
  near(g.update(135.0, 100.0, false, 0.02), 135.0 - 4.94, 1e-9, "liftoff_fades_the_correction line 43");
  double last = 0.0;
  for (int i = 0; i < 200; ++i) {
    last = g.update(200.0, 100.0, false, 0.1);
  }
  near(last, 200.0, 1e-9, "liftoff_fades_the_correction line 48");
  near(g.offset(), 0.0, 1e-9, "liftoff_fades_the_correction line 49");
}

void airborne_far_above_is_untouched() {
  GroundFit g;
  near(g.update(3000.0, 100.0, false, 0.02), 3000.0, 1e-9, "airborne_far_above_is_untouched line 54");
  near(g.offset(), 0.0, 1e-9, "airborne_far_above_is_untouched line 55");
}

void airborne_below_terrain_is_lifted() {
  GroundFit g;
  near(g.update(90.0, 100.0, false, 0.02), 100.0, 1e-9, "airborne_below_terrain_is_lifted line 60");
}

void negative_dt_does_not_move() {
  GroundFit g;
  g.update(200.0, 100.0, false, 0.02);
  near(g.update(106.0, 100.0, true, -1.0), 106.0, 1e-9, "negative_dt_does_not_move line 66");
  near(g.offset(), 0.0, 1e-9, "negative_dt_does_not_move line 67");
}
} // namespace

int main() {
  first_ground_call_lands_on_terrain();
  console_ground_below_mesh_is_lifted();
  touchdown_settles_at_the_rate();
  never_below_terrain_while_settling();
  liftoff_fades_the_correction();
  airborne_far_above_is_untouched();
  airborne_below_terrain_is_lifted();
  negative_dt_does_not_move();
  std::cout << "ground fit tests passed\n";
  return 0;
}
