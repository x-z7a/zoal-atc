#include "zoal_atc/traffic/traffic_picture.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

double normalise_deg(double d) {
  d = std::fmod(d, 360.0);
  if (d < 0.0) {
    d += 360.0;
  }
  if (d == 360.0) {
    d = 0.0;
  }
  return d;
}
} // namespace

namespace zoal_atc::traffic {

double distance_nm(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
  double lat1_rad = lat1_deg * kDegToRad;
  double lat2_rad = lat2_deg * kDegToRad;
  double dlat = (lat2_deg - lat1_deg) * kDegToRad;
  double dlon = (lon2_deg - lon1_deg) * kDegToRad;

  double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
             std::cos(lat1_rad) * std::cos(lat2_rad) * std::sin(dlon / 2) * std::sin(dlon / 2);
  a = std::clamp(a, 0.0, 1.0);
  double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
  return kEarthRadiusNm * c;
}

double bearing_deg(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
  double lat1_rad = lat1_deg * kDegToRad;
  double lat2_rad = lat2_deg * kDegToRad;
  double dlon = (lon2_deg - lon1_deg) * kDegToRad;

  double y = std::sin(dlon) * std::cos(lat2_rad);
  double x = std::cos(lat1_rad) * std::sin(lat2_rad) -
             std::sin(lat1_rad) * std::cos(lat2_rad) * std::cos(dlon);
  double bearing_rad = std::atan2(y, x);
  double bearing_deg_val = bearing_rad / kDegToRad;
  return normalise_deg(bearing_deg_val);
}

std::vector<Nearby> nearby(const TrafficRegistry &registry, double local_time_s, const OwnPosition &own) {
  std::vector<Identity> ids;
  registry.for_each([&](const Identity &who){
    ids.push_back(who);
  });

  std::vector<Nearby> result;
  for (const auto &id : ids) {
    auto s = registry.state(id.id, local_time_s);
    if (!s) continue;

    Nearby nearby_item{id, *s};
    if (own.valid) {
      nearby_item.has_own = true;
      nearby_item.distance_nm = distance_nm(own.lat_deg, own.lon_deg, s->lat_deg, s->lon_deg);
      nearby_item.bearing_deg = bearing_deg(own.lat_deg, own.lon_deg, s->lat_deg, s->lon_deg);
      nearby_item.relative_alt_ft = s->alt_ft_msl - own.alt_ft_msl;
    }
    result.push_back(nearby_item);
  }

  if (own.valid) {
    std::stable_sort(result.begin(), result.end(), [](const Nearby &a, const Nearby &b) {
      if (a.distance_nm != b.distance_nm) {
        return a.distance_nm < b.distance_nm;
      }
      return a.identity.id < b.identity.id;
    });
  } else {
    std::stable_sort(result.begin(), result.end(), [](const Nearby &a, const Nearby &b) {
      return a.identity.id < b.identity.id;
    });
  }

  return result;
}

CameraPose chase_pose(double target_x, double target_y, double target_z,
                      double target_heading_deg, const ChaseOffset &offset) {
  double d = std::clamp(offset.distance_m, kChaseMinDistanceM, kChaseMaxDistanceM);
  double a = target_heading_deg + offset.orbit_deg;
  double ar = a * kDegToRad;
  double x = target_x - d * std::sin(ar);
  double y = target_y + offset.height_m;
  double z = target_z + d * std::cos(ar);
  double heading_deg = normalise_deg(a);
  double pitch_deg = -std::atan2(offset.height_m, d) / kDegToRad;

  return CameraPose{x, y, z, heading_deg, pitch_deg};
}

} // namespace zoal_atc::traffic
