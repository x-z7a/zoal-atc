#include "zoal_atc/gui/traffic_panel.hpp"
#include "zoal_atc/gui/panel_session.hpp"
#include "zoal_atc/gui/i18n.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace zoal_atc::gui {

namespace {

constexpr const char *kMinus = "\xE2\x88\x92"; // U+2212

} // namespace

std::string identity_line(const traffic::Identity &who) {
  std::string out = who.icao_type;
  if (!who.destination.empty()) {
    return out + (out.empty() ? "" : " ") + "\xE2\x86\x92 " + who.destination; // an arrow
  }
  bool said = !who.airline.empty() && who.callsign.rfind(who.airline, 0) == 0;
  if (!who.airline.empty() && !said) {
    out += (out.empty() ? "" : " \xC2\xB7 ") + who.airline;
  }
  return out;
}

std::string format_distance_nm(double nm) {
  if (nm < 99.95) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f nm", nm);
    return std::string(buf);
  } else {
    return std::to_string(std::llround(nm)) + " nm";
  }
}

std::string format_bearing(double deg) {
  auto r = std::llround(deg) % 360;
  if (r < 0) r += 360;
  if (r == 0) r = 360;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%03lld\xc2\xb0", r);
  return std::string(buf);
}

std::string format_traffic_altitude(double alt_ft_msl, bool on_ground) {
  if (on_ground) return "GND";
  auto h = std::llround(alt_ft_msl / 100.0) * 100;
  if (h >= 18000) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "FL%03lld", h / 100);
    return std::string(buf);
  } else {
    return format_thousands(double(h)) + " ft";
  }
}

std::string format_relative_altitude(double relative_ft) {
  auto h = std::llround(relative_ft / 100.0) * 100;
  if (h == 0) return "0";
  if (h > 0) {
    return "+" + format_thousands(double(h));
  } else {
    return kMinus + format_thousands(double(-h));
  }
}

std::string format_vertical_speed(double fpm) {
  auto trend = vertical_trend(fpm);
  if (trend == 0) {
    return tr("level");
  } else {
    auto h = std::llround(fpm / 100.0) * 100;
    if (h > 0) {
      return "+" + format_thousands(double(h));
    } else {
      return kMinus + format_thousands(double(-h));
    }
  }
}

int vertical_trend(double fpm) {
  if (fpm >= kLevelFpm) return 1;
  if (fpm <= -kLevelFpm) return -1;
  return 0;
}

TrafficPanel traffic_panel(const std::vector<traffic::Nearby> &picture,
                           TrafficFilter filter,
                           const std::string &tracked,
                           const std::string &followed) {
  TrafficPanel result;
  result.available = true;
  result.tracked = tracked;
  result.followed = followed;
  result.total = picture.size();
  result.pilots = 0;
  result.ai = 0;

  for (const auto &n : picture) {
    if (n.identity.kind == traffic::TrafficKind::Participant) {
      ++result.pilots;
    } else {
      ++result.ai;
    }
  }

  for (const auto &n : picture) {
    bool pilot = n.identity.kind == traffic::TrafficKind::Participant;
    if ((filter == TrafficFilter::pilots && !pilot) ||
        (filter == TrafficFilter::ai && pilot)) {
      continue;
    }

    TrafficPanelRow row;
    row.id = n.identity.id;
    row.pilot = pilot;
    row.callsign = n.identity.callsign.empty() ? n.identity.id : n.identity.callsign;

    row.type = identity_line(n.identity);
    if (row.type.empty()) {
      row.type = kUnknown;
    }

    if (n.has_own) {
      row.distance = format_distance_nm(n.distance_nm);
      row.bearing = format_bearing(n.bearing_deg);
    } else {
      row.distance = kUnknown;
      row.bearing = kUnknown;
    }

    row.altitude = format_traffic_altitude(n.sample.alt_ft_msl, n.sample.on_ground);

    if (n.has_own && !n.sample.on_ground) {
      row.relative = format_relative_altitude(n.relative_alt_ft);
    } else {
      row.relative = "";
    }

    row.speed = std::to_string(std::llround(n.sample.groundspeed_kts)) + " kt";

    if (n.sample.on_ground) {
      row.vertical = "";
      row.trend = 0;
    } else {
      row.vertical = format_vertical_speed(n.sample.vertical_speed_fpm);
      row.trend = vertical_trend(n.sample.vertical_speed_fpm);
    }

    row.on_ground = n.sample.on_ground;
    row.tracked = !tracked.empty() && row.id == tracked;
    row.followed = !followed.empty() && row.id == followed;

    result.rows.push_back(row);
  }

  return result;
}

} // namespace zoal_atc::gui
