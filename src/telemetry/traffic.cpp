#include "zoal_atc/telemetry/traffic.hpp"

#include <algorithm>
#include <cmath>

namespace zoal_atc::telemetry {
namespace {

constexpr double kFeetPerMetre = 3.280839895;
constexpr double kKnotsPerMps = 1.943844492;
constexpr double kNmPerDegLat = 60.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// at returns element `i` of a dataref array, or 0 when the array came back
// short. A missing field degrades to its zero value rather than failing the
// whole frame.
template <typename T> double at(const std::vector<T> &v, int i) {
  if (i < 0 || i >= static_cast<int>(v.size())) {
    return 0.0;
  }
  return static_cast<double>(v[static_cast<std::size_t>(i)]);
}

// A slot is populated when it has a real position. Unpopulated slots read as
// all-zero, which lands on the null island.
//
// Validity is deliberately NOT keyed on modeS_id: the id is identity, and
// X-Plane does not always publish one, so requiring it would silently drop real
// aircraft.
bool slot_populated(const TrafficArrays &arrays, int i) {
  const double lat = at(arrays.lat, i);
  const double lon = at(arrays.lon, i);
  return !(std::fabs(lat) < 1e-9 && std::fabs(lon) < 1e-9);
}

} // namespace

const char *traffic_status_name(TrafficStatus status) {
  switch (status) {
  case TrafficStatus::known_empty:
    return "known_empty";
  case TrafficStatus::current:
    return "current";
  case TrafficStatus::unavailable:
  default:
    return "unavailable";
  }
}

std::string tcas_string_at(const std::vector<char> &blob, int index,
                           int slot_width) {
  if (index < 0 || slot_width <= 0) {
    return {};
  }
  const int base = index * slot_width;
  if (base < 0 || base >= static_cast<int>(blob.size())) {
    return {};
  }
  const int end =
      std::min(base + slot_width, static_cast<int>(blob.size()));

  std::string out;
  out.reserve(static_cast<std::size_t>(slot_width));
  for (int i = base; i < end; ++i) {
    const char c = blob[static_cast<std::size_t>(i)];
    if (c == '\0') {
      break; // NUL terminates the record
    }
    // Printable ASCII only: a junk byte must not corrupt the JSON frame.
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 0x20 && uc < 0x7F) {
      out.push_back(c);
    }
  }
  // Trim trailing spaces; some feeds pad rather than terminate.
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

double traffic_distance_nm(double lat1, double lon1, double lat2, double lon2) {
  const double dlat = (lat2 - lat1) * kNmPerDegLat;
  const double mid = (lat1 + lat2) * 0.5;
  const double dlon = (lon2 - lon1) * kNmPerDegLat * std::cos(mid * kDegToRad);
  return std::sqrt(dlat * dlat + dlon * dlon);
}

TrafficExtract extract_targets(const TrafficArrays &arrays, int num_acf,
                               const OwnPosition &own,
                               const TrafficBounds &bounds,
                               bool datarefs_resolved) {
  TrafficExtract out;

  // The distinction the whole phase turns on: a dataref that does not exist
  // reports no picture, never zero aircraft.
  if (!datarefs_resolved) {
    out.status = TrafficStatus::unavailable;
    return out;
  }

  // Distance is carried alongside so the sort and the cap do not recompute it.
  struct Scored {
    TrafficTarget target;
    double distance_nm;
  };
  std::vector<Scored> found;

  // Slot 0 is ownship — verified in the sim 2026-08-11, not assumed. Starting
  // this loop at zero would have the console advise our own aircraft about
  // itself at zero range, which the conflict tier promotes to a safety alert.
  //
  // num_acf is a count, not a high-water mark — X-Plane documents that populated
  // entries need not be consecutive — so it only stops the scan early once that
  // many aircraft have been seen.
  out.census.reported = num_acf;
  int seen = 0;
  for (int i = 1; i < kTcasSlots; ++i) {
    if (num_acf > 0 && seen >= num_acf) {
      break;
    }
    if (!slot_populated(arrays, i)) {
      continue;
    }
    ++seen;
    out.census.seen = seen;

    TrafficTarget t;
    t.id = static_cast<std::int32_t>(at(arrays.modeS_id, i));
    t.callsign = tcas_string_at(arrays.flight_id, i);
    t.icao_type = tcas_string_at(arrays.icao_type, i);
    t.lat_deg = at(arrays.lat, i);
    t.lon_deg = at(arrays.lon, i);
    t.alt_ft_msl = at(arrays.ele_m, i) * kFeetPerMetre;
    t.groundspeed_kts = at(arrays.v_msc, i) * kKnotsPerMps;
    t.vertical_speed_fpm = at(arrays.vertical_speed_fpm, i);
    t.track_deg = at(arrays.hpath_deg, i);
    t.heading_deg = at(arrays.psi_deg, i);
    t.on_ground = at(arrays.weight_on_wheels, i) != 0.0;

    // Bounding only — well outside every range the console decides on, so
    // nothing actionable is filtered away.
    if (std::fabs(t.alt_ft_msl - own.alt_ft_msl) > bounds.vertical_ft) {
      ++out.census.dropped_vertical;
      continue;
    }
    const double distance =
        traffic_distance_nm(own.lat_deg, own.lon_deg, t.lat_deg, t.lon_deg);
    if (distance > bounds.radius_nm) {
      ++out.census.dropped_range;
      continue;
    }
    found.push_back(Scored{t, distance});
  }

  std::stable_sort(found.begin(), found.end(),
                   [](const Scored &a, const Scored &b) {
                     return a.distance_nm < b.distance_nm;
                   });

  // The cap is reported. A truncated picture that looks complete is how a
  // controller ends up separating from the wrong aircraft.
  if (bounds.max_targets > 0 &&
      static_cast<int>(found.size()) > bounds.max_targets) {
    out.census.dropped_cap =
        static_cast<int>(found.size()) - bounds.max_targets;
    found.resize(static_cast<std::size_t>(bounds.max_targets));
    out.truncated = true;
  }

  out.targets.reserve(found.size());
  for (const Scored &s : found) {
    out.targets.push_back(s.target);
  }
  out.status =
      out.targets.empty() ? TrafficStatus::known_empty : TrafficStatus::current;
  return out;
}

} // namespace zoal_atc::telemetry
