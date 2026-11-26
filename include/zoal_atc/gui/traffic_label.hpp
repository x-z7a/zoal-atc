#ifndef ZOAL_ATC_GUI_TRAFFIC_LABEL_HPP
#define ZOAL_ATC_GUI_TRAFFIC_LABEL_HPP

#include "zoal_atc/traffic/traffic_picture.hpp"

#include <string>

namespace zoal_atc::gui {

// The tag drawn over each aircraft this plugin renders, in place of XPMP2's
// one-line bitmap label. What it says depends on how far the camera is: the
// near ones say the most. No ImGui and no X-Plane; src/ui/traffic_labels_view
// draws it, src/xplane/traffic_labels places it.

enum class LabelDetail {
  full,     // callsign and type; altitude, vertical, speed, distance from us
  compact,  // callsign and altitude
  callsign, // callsign only
};

// Camera distances, in metres, at which the tag gets shorter and then goes.
struct LabelRange {
  double full_m = 3'000.0;    // nearer than this: full
  double compact_m = 12'000.0; // nearer than this: compact
  double max_m = 30'000.0;    // farther than this: no tag (except tracked)
};

struct TrafficLabel {
  std::string id;
  bool pilot = false;    // a participant, not console AI
  bool tracked = false;
  bool followed = false;
  LabelDetail detail = LabelDetail::callsign;
  // 1 up to 80% of max_m, then linearly down to 0.35 at max_m. Tracked: 1.
  float opacity = 1.0f;

  std::string callsign; // callsign, else the track id
  std::string type;     // "A320 · ACA", "A320", "ACA", or "" (not kUnknown)
  std::string altitude; // format_traffic_altitude
  std::string vertical; // format_vertical_speed; "" on the ground
  int trend = 0;        // vertical_trend; 0 on the ground
  std::string speed;    // "410 kt"
  std::string distance; // format_distance_nm from own-ship; "" without own-ship
};

// Which tag a camera this far away gets. Tracked aircraft are always full.
LabelDetail label_detail(double camera_distance_m, const LabelRange &range,
                         bool tracked);

// How opaque the tag is (see TrafficLabel::opacity).
float label_opacity(double camera_distance_m, const LabelRange &range,
                    bool tracked);

// Whether to draw a tag at all: within max_m, or tracked.
bool label_shown(double camera_distance_m, const LabelRange &range,
                 bool tracked);

// The tag for one aircraft. `tracked` / `followed` are the panel's ids.
TrafficLabel traffic_label(const traffic::Nearby &aircraft,
                           double camera_distance_m, const LabelRange &range,
                           const std::string &tracked,
                           const std::string &followed);

} // namespace zoal_atc::gui

#endif // ZOAL_ATC_GUI_TRAFFIC_LABEL_HPP
