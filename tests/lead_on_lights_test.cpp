#include "test_support.hpp"
#include "zoal_atc/taxi/lead_on_lights.hpp"

#include <cmath>
#include <iostream>

using namespace zoal_atc::taxi;
using test::near;
using test::require;

namespace {

constexpr double kLat0 = 45.0;
constexpr double kLon0 = -75.0;
constexpr double kRadius = 6371008.8;
constexpr double kPi = 3.14159265358979323846;

RoutePoint at(double east_m, double north_m, std::string hold = {}) {
  RoutePoint p;
  p.lat_deg = kLat0 + north_m / kRadius * 180.0 / kPi;
  p.lon_deg = kLon0 + east_m / (kRadius * std::cos(kLat0 * kPi / 180.0)) * 180.0 / kPi;
  p.hold_short = std::move(hold);
  return p;
}

double metres_between(double lat1, double lon1, double lat2, double lon2) {
  const double n = (lat2 - lat1) * kPi / 180.0 * kRadius;
  const double e = (lon2 - lon1) * kPi / 180.0 * kRadius * std::cos(kLat0 * kPi / 180.0);
  return std::sqrt(n * n + e * e);
}

double heading_gap(double a, double b) {
  double d = std::fmod(std::fabs(a - b), 360.0);
  return d > 180.0 ? 360.0 - d : d;
}

void a_straight_route_is_lit_every_fifteen_metres() {
  auto plan = plan_lights({at(0, 0), at(0, 150)});
  near(plan.length_m, 150.0, 0.5, "straight length");
  require(plan.lights.size() == 11, "straight: a light every 15 m from 0 to 150, got " +
                                        std::to_string(plan.lights.size()));
  for (std::size_t i = 0; i < plan.lights.size(); ++i) {
    near(plan.lights[i].along_m, 15.0 * static_cast<double>(i), 0.5, "straight spacing");
    require(heading_gap(plan.lights[i].heading_deg, 0.0) < 1.0, "straight: heading north");
  }
  require(!plan.stop_bar.has_value(), "no hold short, no stop bar");
}

void a_corner_is_rounded_and_lit_closer() {
  auto plan = plan_lights({at(0, 0), at(0, 100), at(100, 100)});
  // 60 m, a quarter circle of radius 40, 60 m.
  near(plan.length_m, 120.0 + 40.0 * kPi / 2.0, 1.0, "rounded length");
  const auto corner = at(0, 100);
  std::size_t in_turn = 0;
  for (const auto &light : plan.lights) {
    require(metres_between(light.lat_deg, light.lon_deg, corner.lat_deg, corner.lon_deg) > 15.0,
            "no light on the grass inside the corner");
    if (light.along_m > 60.5 && light.along_m < 60.0 + 40.0 * kPi / 2.0 - 0.5) {
      ++in_turn;
    }
  }
  require(in_turn >= 7, "the turn is lit every 7.5 m, got " + std::to_string(in_turn));
  require(heading_gap(plan.lights.back().heading_deg, 90.0) < 2.0, "leaving east");
  for (std::size_t i = 1; i < plan.path.size(); ++i) {
    require(plan.path[i].along_m >= plan.path[i - 1].along_m, "path along increases");
  }
}

void short_legs_round_with_a_smaller_radius() {
  auto plan = plan_lights({at(0, 0), at(0, 10), at(10, 10)});
  require(plan.length_m < 20.0 && plan.length_m > 15.0, "short corner rounded within the legs");
}

void a_hold_short_ends_on_a_stop_bar() {
  auto plan = plan_lights({at(0, 0), at(0, 100), at(100, 100, "14")});
  require(plan.stop_bar.has_value(), "stop bar at the hold short");
  near(plan.stop_bar->along_m, plan.length_m, 0.5, "stop bar at the end");
  const auto end = at(100, 100);
  near(metres_between(plan.stop_bar->lat_deg, plan.stop_bar->lon_deg, end.lat_deg, end.lon_deg),
       0.0, 0.5, "stop bar on the hold");
  require(heading_gap(plan.stop_bar->heading_deg, 90.0) < 2.0, "stop bar faces along the route");
  require(plan.lights.back().along_m <= plan.length_m - 3.0, "no green on the stop bar");
}

void the_aircraft_is_placed_along_the_route() {
  auto plan = plan_lights({at(0, 0), at(0, 150)});
  const auto beside = at(5, 50);
  auto along = along_route(plan, beside.lat_deg, beside.lon_deg, 0.0);
  require(along.has_value(), "beside the route is on it");
  near(*along, 50.0, 0.5, "along");
  const auto far = at(200, 50);
  require(!along_route(plan, far.lat_deg, far.lon_deg, 0.0).has_value(), "off the route");
}

void only_the_stretch_ahead_is_lit() {
  auto plan = plan_lights({at(0, 0), at(0, 150, "14")});
  auto ahead = lights_ahead(plan, 50.0, 40.0);
  require(ahead.size() == 3, "60, 75 and 90, got " + std::to_string(ahead.size()));
  near(plan.lights[ahead.front()].along_m, 60.0, 0.5, "first ahead");
  require(!stop_bar_ahead(plan, 50.0, 40.0), "stop bar beyond reach");
  require(stop_bar_ahead(plan, 120.0, 40.0), "stop bar within reach");
}

void one_point_is_nothing() {
  auto plan = plan_lights({at(0, 0)});
  require(plan.lights.empty() && plan.path.empty() && !plan.stop_bar, "empty plan");
  require(!along_route(plan, kLat0, kLon0, 0.0).has_value(), "nowhere on nothing");
}

} // namespace

int main() {
  a_straight_route_is_lit_every_fifteen_metres();
  a_corner_is_rounded_and_lit_closer();
  short_legs_round_with_a_smaller_radius();
  a_hold_short_ends_on_a_stop_bar();
  the_aircraft_is_placed_along_the_route();
  only_the_stretch_ahead_is_lit();
  one_point_is_nothing();
  std::cout << "lead_on_lights tests passed\n";
  return 0;
}
