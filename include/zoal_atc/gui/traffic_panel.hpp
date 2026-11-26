#ifndef ZOAL_ATC_GUI_TRAFFIC_PANEL_HPP
#define ZOAL_ATC_GUI_TRAFFIC_PANEL_HPP

#include "zoal_atc/traffic/traffic_picture.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace zoal_atc::gui {

// What the panel's Traffic tab shows: the aircraft this plugin draws for the
// console, in the words the panel writes them in. No ImGui and no X-Plane;
// src/ui/panel_view.cpp draws it and src/xplane fills it once a frame.

enum class TrafficFilter { all, pilots, ai };

// A vertical speed under this, either way, is level.
inline constexpr double kLevelFpm = 200.0;

struct TrafficPanelRow {
  std::string id;          // console track id; what follow and track name
  bool pilot = false;      // a participant, not console AI
  std::string callsign;    // callsign, else the track id
  std::string type;        // "B738 · ACA", "B738", "ACA", or kUnknown
  std::string distance;    // "12.4 nm"; kUnknown without own-ship
  std::string bearing;     // "284°", "001°", "360°" never; kUnknown without own-ship
  std::string altitude;    // format_traffic_altitude
  std::string relative;    // format_relative_altitude; "" when unknown or on the ground
  std::string speed;       // "210 kt"
  std::string vertical;    // format_vertical_speed
  int trend = 0;           // +1 climbing, -1 descending, 0 level
  bool on_ground = false;
  bool tracked = false;
  bool followed = false;
};

struct TrafficPanel {
  // The traffic layer is running; when false, `unavailable` says why.
  bool available = false;
  std::string unavailable;
  std::vector<TrafficPanelRow> rows; // after the filter, nearest first
  std::size_t total = 0;             // before the filter
  std::size_t pilots = 0;            // before the filter
  std::size_t ai = 0;                // before the filter
  std::string tracked;               // track id, or empty
  std::string followed;              // track id, or empty
  double chase_distance_m = 0.0;     // the follow camera's, while following
};

// "12.4 nm" under 100 nm (one decimal), "143 nm" from 100 (rounded).
std::string format_distance_nm(double nm);
// Rounded to a whole degree, three digits, 0 shown as "360°": "001°", "284°".
std::string format_bearing(double deg);
// "GND" on the ground; "FL350" at or above 18,000 ft (rounded to the nearest
// hundred, three digits); else "3,200 ft" rounded to the nearest 100 ft.
std::string format_traffic_altitude(double alt_ft_msl, bool on_ground);
// Rounded to the nearest 100 ft with a sign: "+1,200", "\xE2\x88\x92" "400"
// (U+2212 minus), "0" for under 50 ft either way.
std::string format_relative_altitude(double relative_ft);
// Under kLevelFpm either way: "level"; else signed, rounded to 100 fpm:
// "+1,500", "\xE2\x88\x92" "800".
std::string format_vertical_speed(double fpm);
int vertical_trend(double fpm);

// The tab's rows from the picture: `filter` applied, order kept. tracked /
// followed mark the matching rows and are copied into the result. `total`,
// `pilots` and `ai` count `picture` before filtering (an Unknown kind counts
// as AI). available is set true; the caller sets it false with a reason.
TrafficPanel traffic_panel(const std::vector<traffic::Nearby> &picture,
                           TrafficFilter filter, const std::string &tracked,
                           const std::string &followed);

} // namespace zoal_atc::gui

#endif // ZOAL_ATC_GUI_TRAFFIC_PANEL_HPP
