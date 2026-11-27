#include "zoal_atc/taxi/lead_on_lights.hpp"

#include <cmath>
#include <vector>
#include <optional>
#include <algorithm>
#include <cstddef>
#include <limits>

namespace zoal_atc::taxi {

namespace {
constexpr double kRadius = 6371008.8;
constexpr double kPi = 3.14159265358979323846;

struct Point {
  double x = 0.0;
  double y = 0.0;
  
  Point() = default;
  Point(double x_, double y_) : x(x_), y(y_) {}
};

Point project(double lat, double lon, double lat0, double lon0) {
  Point p;
  p.x = (lon - lon0) * kPi / 180.0 * kRadius * std::cos(lat0 * kPi / 180.0);
  p.y = (lat - lat0) * kPi / 180.0 * kRadius;
  return p;
}

void unproject(const Point &p, double lat0, double lon0, double &lat, double &lon) {
  lat = lat0 + p.y * 180.0 / (kPi * kRadius);
  lon = lon0 + p.x * 180.0 / (kPi * kRadius * std::cos(lat0 * kPi / 180.0));
}

Point operator-(const Point &a, const Point &b) {
  return {a.x - b.x, a.y - b.y};
}

Point operator+(const Point &a, const Point &b) {
  return {a.x + b.x, a.y + b.y};
}

Point operator*(const Point &a, double s) {
  return {a.x * s, a.y * s};
}

double dot(const Point &a, const Point &b) {
  return a.x * b.x + a.y * b.y;
}

double length(const Point &p) {
  return std::sqrt(dot(p, p));
}

Point unit(const Point &p) {
  double len = length(p);
  if (len < 1e-9) return {0.0, 0.0};
  return p * (1.0 / len);
}

double heading(const Point &dir) {
  double h = std::atan2(dir.x, dir.y) * 180.0 / kPi;
  if (h < 0.0) h += 360.0;
  return h;
}

struct Vertex {
  Point pos;
  bool is_turn = false;
};

std::vector<Vertex> build_rounded_path(const std::vector<Point> &points, Spacing spacing) {
  if (points.size() < 2) return {};
  
  std::vector<Vertex> path;
  path.reserve(points.size() * 4); // Estimate
  
  // Remove duplicates
  std::vector<Point> clean_points;
  clean_points.push_back(points[0]);
  for (std::size_t i = 1; i < points.size(); ++i) {
    if (length(points[i] - points[i-1]) >= 0.01) {
      clean_points.push_back(points[i]);
    }
  }
  
  if (clean_points.size() < 2) return {};
  
  // Start with first point as straight
  path.push_back({clean_points[0], false});
  
  for (std::size_t i = 1; i < clean_points.size() - 1; ++i) {
    const Point &a = clean_points[i-1];
    const Point &b = clean_points[i];
    const Point &c = clean_points[i+1];
    
    Point u1 = unit(b - a);
    Point u2 = unit(c - b);
    
    double dot_product = dot(u1, u2);
    // Clamp to [-1, 1] to avoid numerical errors
    dot_product = std::max(-1.0, std::min(1.0, dot_product));
    double theta = std::acos(dot_product);
    
    if (theta < 5.0 * kPi / 180.0 || theta > 175.0 * kPi / 180.0) {
      // Straight segment
      path.push_back({b, false});
    } else {
      // Turn segment
      double r = spacing.max_corner_radius_m;
      double t = r * std::tan(theta / 2.0);
      
      // Cap tangent length to half of the smaller leg
      double len_ab = length(b - a);
      double len_bc = length(c - b);
      double max_t = 0.5 * std::min(len_ab, len_bc);
      if (t > max_t) {
        r = max_t / std::tan(theta / 2.0);
        t = max_t;
      }
      
      Point t1 = b - u1 * t;
      Point t2 = b + u2 * t;
      
      // Add tangent point as straight
      path.push_back({t1, false});
      
      // Compute arc center
      double cross = u1.x * u2.y - u1.y * u2.x;
      Point n = (cross > 0) ? Point(-u1.y, u1.x) : Point(u1.y, -u1.x);
      n = unit(n);
      Point o = t1 + n * r;
      
      // Arc length
      double arc_length = r * theta;
      int k = std::max(2, static_cast<int>(std::ceil(arc_length / 2.0)));
      
      // Sign of rotation (positive for counter-clockwise in x-east/y-north frame)
      int sign = (cross > 0) ? 1 : -1;
      
      // Add arc points
      for (int j = 1; j <= k; ++j) {
        double angle = sign * theta * static_cast<double>(j) / static_cast<double>(k);
        Point diff = t1 - o;
        double cos_a = std::cos(angle);
        double sin_a = std::sin(angle);
        Point rotated(diff.x * cos_a - diff.y * sin_a,
                      diff.x * sin_a + diff.y * cos_a);
        Point arc_point = o + rotated;
        path.push_back({arc_point, true});
      }
      
      // Add t2 as straight (should be the same as last arc point within floating error)
      path.push_back({t2, false});
    }
  }
  
  // Add last point as straight
  path.push_back({clean_points.back(), false});
  
  return path;
}

} // anonymous namespace

LightPlan plan_lights(const std::vector<RoutePoint> &points, Spacing spacing) {
  if (points.size() < 2) return {};
  
  // Project all points to local coordinates
  double lat0 = points[0].lat_deg;
  double lon0 = points[0].lon_deg;
  std::vector<Point> projected_points;
  projected_points.reserve(points.size());
  for (const auto &p : points) {
    projected_points.push_back(project(p.lat_deg, p.lon_deg, lat0, lon0));
  }
  
  // Build rounded path
  auto path = build_rounded_path(projected_points, spacing);
  if (path.empty()) return {};
  
  // Compute cumulative distances and fill path
  LightPlan plan;
  plan.path.reserve(path.size());
  
  double total_length = 0.0;
  for (std::size_t i = 0; i < path.size(); ++i) {
    PathPoint pp;
    if (i > 0) {
      Point diff = path[i].pos - path[i-1].pos;
      total_length += length(diff);
    }
    pp.along_m = total_length;
    
    // Unproject back to lat/lon
    unproject(path[i].pos, lat0, lon0, pp.lat_deg, pp.lon_deg);
    
    plan.path.push_back(pp);
  }
  
  plan.length_m = total_length;
  
  if (plan.path.empty()) return {};
  
  // Place lights
  double next_light_along = 0.0;
  std::size_t light_index = 0;
  for (std::size_t i = 0; i < path.size() - 1; ++i) {
    Point diff = path[i+1].pos - path[i].pos;
    double segment_length = length(diff);
    
    if (segment_length < 1e-6) continue;
    
    // A vertex is tagged by the segment ending at it, so this segment is a
    // turn when its end vertex is.
    double step = path[i + 1].is_turn ? spacing.turn_m : spacing.straight_m;
    
    // If we've already placed a light and are now entering a new segment,
    // adjust next_light_along to ensure proper spacing
    if (i > 0 && light_index > 0) {
      if (next_light_along - plan.path[i].along_m > step) {
        next_light_along = plan.path[i].along_m + step;
      }
    }
    
    // Place lights along this segment
    while (next_light_along <= plan.path[i].along_m + segment_length + 1e-9) {
      double fraction = (next_light_along - plan.path[i].along_m) / segment_length;
      
      Point pos = path[i].pos + diff * fraction;
      
      Light light;
      unproject(pos, lat0, lon0, light.lat_deg, light.lon_deg);
      light.along_m = next_light_along;
      
      // Compute heading
      if (i < path.size() - 1) {
        Point next_diff = path[i+1].pos - path[i].pos;
        light.heading_deg = heading(next_diff);
      } else {
        // Use last segment's direction
        if (i > 0) {
          Point prev_diff = path[i].pos - path[i-1].pos;
          light.heading_deg = heading(prev_diff);
        } else {
          light.heading_deg = 0.0;
        }
      }
      
      plan.lights.push_back(light);
      light_index++;
      
      next_light_along += step;
    }
  }
  
  // Handle stop bar
  if (!points.back().hold_short.empty()) {
    // Place stop bar at the last vertex
    Point last_pos = path.back().pos;
    plan.stop_bar.emplace();
    unproject(last_pos, lat0, lon0, plan.stop_bar->lat_deg, plan.stop_bar->lon_deg);
    plan.stop_bar->along_m = plan.length_m;
    
    // Compute heading of last segment
    if (path.size() >= 2) {
      Point diff = path.back().pos - path[path.size()-2].pos;
      plan.stop_bar->heading_deg = heading(diff);
    } else {
      plan.stop_bar->heading_deg = 0.0;
    }
    
    // Remove lights that would overlap with stop bar
    auto it = std::remove_if(plan.lights.begin(), plan.lights.end(),
                             [&plan](const Light &l) { return l.along_m > plan.length_m - 3.0; });
    plan.lights.erase(it, plan.lights.end());
  } else {
    // Ensure a light at the very end exists
    if (plan.lights.empty() || plan.lights.back().along_m < plan.length_m - 0.5) {
      Light last_light;
      Point last_pos = path.back().pos;
      unproject(last_pos, lat0, lon0, last_light.lat_deg, last_light.lon_deg);
      last_light.along_m = plan.length_m;
      
      // Compute heading of last segment
      if (path.size() >= 2) {
        Point diff = path.back().pos - path[path.size()-2].pos;
        last_light.heading_deg = heading(diff);
      } else {
        last_light.heading_deg = 0.0;
      }
      
      plan.lights.push_back(last_light);
    }
  }
  
  return plan;
}

std::optional<double> along_route(const LightPlan &plan, double lat_deg, double lon_deg,
                                  double previous_along_m, double max_off_route_m) {
  if (plan.path.size() < 2) return std::nullopt;
  
  // Project aircraft to local coordinates
  double lat0 = plan.path[0].lat_deg;
  double lon0 = plan.path[0].lon_deg;
  Point aircraft = project(lat_deg, lon_deg, lat0, lon0);
  
  double min_distance = std::numeric_limits<double>::max();
  double along = 0.0;
  
  for (std::size_t i = 0; i < plan.path.size() - 1; ++i) {
    // Skip segments that end before previous_along_m - 30
    if (plan.path[i+1].along_m < previous_along_m - 30.0) continue;
    
    Point a = project(plan.path[i].lat_deg, plan.path[i].lon_deg, lat0, lon0);
    Point b = project(plan.path[i + 1].lat_deg, plan.path[i + 1].lon_deg, lat0, lon0);
    Point ab = b - a;
    double segment_length = length(ab);
    
    if (segment_length < 1e-9) continue;
    
    // Project aircraft onto segment
    Point ap = aircraft - a;
    double t = dot(ap, ab) / (segment_length * segment_length);
    t = std::max(0.0, std::min(1.0, t)); // Clamp to [0, 1]
    
    Point closest = a + ab * t;
    Point diff = aircraft - closest;
    double distance = length(diff);
    
    if (distance < min_distance) {
      min_distance = distance;
      along = plan.path[i].along_m + t * segment_length;
    }
  }
  
  if (min_distance > max_off_route_m || std::isinf(min_distance)) return std::nullopt;
  
  return along;
}

std::vector<std::size_t> lights_ahead(const LightPlan &plan, double along_m, double ahead_m) {
  std::vector<std::size_t> result;
  
  for (std::size_t i = 0; i < plan.lights.size(); ++i) {
    if (plan.lights[i].along_m > along_m && plan.lights[i].along_m <= along_m + ahead_m) {
      result.push_back(i);
    }
  }
  
  return result;
}

bool stop_bar_ahead(const LightPlan &plan, double along_m, double ahead_m) {
  if (!plan.stop_bar.has_value()) return false;
  return plan.stop_bar->along_m <= along_m + ahead_m;
}

} // namespace zoal_atc::taxi
