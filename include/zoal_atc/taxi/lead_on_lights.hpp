#ifndef ZOAL_ATC_TAXI_LEAD_ON_LIGHTS_HPP
#define ZOAL_ATC_TAXI_LEAD_ON_LIGHTS_HPP

#include "zoal_atc/taxi/taxi_route.hpp"

#include <cstddef>
#include <optional>
#include <vector>

// Where the lights of a taxi route go, and which of them to light.
//
// apt.dat joins its nodes with straight lines, so a route turning at a node
// would cut across the grass. The corners are rounded first, then lights are
// spaced along the rounded line: closer together in the turns, as real
// centreline lights are. Only the stretch just ahead of the aircraft is lit.
//
// Pure geometry, no SDK: positions are latitude and longitude, distances
// metres, headings degrees true (0 north, clockwise).
namespace zoal_atc::taxi {

struct Spacing {
  double straight_m = 15.0;
  double turn_m = 7.5;
  // The largest radius a corner is rounded with. A corner between two short
  // legs gets a smaller one, never cutting more than half of either leg.
  double max_corner_radius_m = 40.0;
};

struct Light {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double heading_deg = 0.0;
  // Distance along the rounded route from its first point.
  double along_m = 0.0;
};

// One vertex of the rounded route.
struct PathPoint {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double along_m = 0.0;
};

struct LightPlan {
  // The rounded route, densely enough that straight lines between
  // consecutive points stay on the arc (about every 2 m in a turn).
  std::vector<PathPoint> path;
  // Green lights in order along the route.
  std::vector<Light> lights;
  // The red stop bar where the route ends at a hold short, across the route
  // (heading is the route's direction there).
  std::optional<Light> stop_bar;
  double length_m = 0.0;
};

// The lights for a route. Fewer than two points: an empty plan.
LightPlan plan_lights(const std::vector<RoutePoint> &points, Spacing spacing = {});

// How far along the route the aircraft is: the along-distance of the nearest
// point on the path, searching only path segments that end at or beyond
// `previous_along_m - 30` so a route that passes near itself does not jump
// back. nullopt when the aircraft is more than max_off_route_m from every
// searched segment, or the plan has no path.
std::optional<double> along_route(const LightPlan &plan, double lat_deg, double lon_deg,
                                  double previous_along_m, double max_off_route_m = 60.0);

// Indices into plan.lights of the lights ahead: along_m in (along, along + ahead_m].
std::vector<std::size_t> lights_ahead(const LightPlan &plan, double along_m, double ahead_m);

// Whether the stop bar is within reach: there is one and its along_m is at
// most along + ahead_m.
bool stop_bar_ahead(const LightPlan &plan, double along_m, double ahead_m);

} // namespace zoal_atc::taxi

#endif // ZOAL_ATC_TAXI_LEAD_ON_LIGHTS_HPP
