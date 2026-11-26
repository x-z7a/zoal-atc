#include "zoal_atc/gui/traffic_label.hpp"
#include "zoal_atc/gui/traffic_panel.hpp"

#include <cmath>
#include <string>

namespace zoal_atc::gui {

LabelDetail label_detail(double camera_distance_m, const LabelRange &range, bool tracked) {
  if (tracked) {
    return LabelDetail::full;
  }
  
  if (camera_distance_m < range.full_m) {
    return LabelDetail::full;
  }
  
  if (camera_distance_m < range.compact_m) {
    return LabelDetail::compact;
  }
  
  return LabelDetail::callsign;
}

float label_opacity(double camera_distance_m, const LabelRange &range, bool tracked) {
  if (tracked) {
    return 1.0f;
  }
  
  double fade_start = 0.8 * range.max_m;
  
  if (camera_distance_m <= fade_start) {
    return 1.0f;
  }
  
  if (camera_distance_m >= range.max_m) {
    return 0.35f;
  }
  
  double t = (camera_distance_m - fade_start) / (range.max_m - fade_start);
  return static_cast<float>(1.0 - 0.65 * t);
}

bool label_shown(double camera_distance_m, const LabelRange &range, bool tracked) {
  return tracked || camera_distance_m <= range.max_m;
}

TrafficLabel traffic_label(const traffic::Nearby &aircraft,
                           double camera_distance_m,
                           const LabelRange &range,
                           const std::string &tracked,
                           const std::string &followed) {
  TrafficLabel label;
  
  const auto &identity = aircraft.identity;
  const auto &sample = aircraft.sample;
  
  label.id = identity.id;
  label.pilot = identity.kind == traffic::TrafficKind::Participant;
  label.tracked = !tracked.empty() && label.id == tracked;
  label.followed = !followed.empty() && label.id == followed;
  
  label.detail = label_detail(camera_distance_m, range, label.tracked);
  label.opacity = label_opacity(camera_distance_m, range, label.tracked);
  
  label.callsign = identity.callsign.empty() ? identity.id : identity.callsign;
  
  if (!identity.icao_type.empty() && !identity.airline.empty()) {
    label.type = identity.icao_type + " \xC2\xB7 " + identity.airline;
  } else if (!identity.icao_type.empty()) {
    label.type = identity.icao_type;
  } else if (!identity.airline.empty()) {
    label.type = identity.airline;
  } else {
    label.type = "";
  }
  
  label.altitude = format_traffic_altitude(sample.alt_ft_msl, sample.on_ground);
  
  if (sample.on_ground) {
    label.vertical = "";
    label.trend = 0;
  } else {
    label.vertical = format_vertical_speed(sample.vertical_speed_fpm);
    label.trend = vertical_trend(sample.vertical_speed_fpm);
  }
  
  label.speed = std::to_string(std::llround(sample.groundspeed_kts)) + " kt";
  
  if (aircraft.has_own) {
    label.distance = format_distance_nm(aircraft.distance_nm);
  } else {
    label.distance = "";
  }
  
  return label;
}

} // namespace zoal_atc::gui
