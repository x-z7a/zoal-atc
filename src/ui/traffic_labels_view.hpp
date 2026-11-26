#ifndef ZOAL_ATC_UI_TRAFFIC_LABELS_VIEW_HPP
#define ZOAL_ATC_UI_TRAFFIC_LABELS_VIEW_HPP

#include "kit.hpp"
#include "zoal_atc/gui/hud_session.hpp"
#include "zoal_atc/gui/traffic_label.hpp"

#include <vector>

#include "imgui.h"

// The tags over the traffic this plugin draws: the HUD's glass and colours,
// in place of XPMP2's bitmap labels. Dear ImGui only, so tools/panel_preview
// can draw them off-screen; src/xplane/traffic_labels.cpp places them.
namespace zoal_atc::ui {

struct PlacedLabel {
  gui::TrafficLabel label;
  // The point just above the aircraft, in display coordinates (y down). The
  // tag hangs over it on a short leader.
  ImVec2 anchor;
  // Larger is farther: far tags are drawn first, so near ones sit on top.
  double depth = 0.0;
};

// Draws every tag into the current ImGui context, which must cover the whole
// screen and take no input.
void draw_traffic_labels(std::vector<PlacedLabel> labels, gui::HudTheme theme,
                         const Typeface &face);

} // namespace zoal_atc::ui

#endif
