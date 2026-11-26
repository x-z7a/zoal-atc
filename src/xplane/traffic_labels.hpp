#ifndef ZOAL_ATC_XPLANE_TRAFFIC_LABELS_HPP
#define ZOAL_ATC_XPLANE_TRAFFIC_LABELS_HPP

#include "zoal_atc/gui/traffic_label.hpp"

#include <functional>
#include <string>
#include <vector>

// The tags over the traffic: one full-screen Dear ImGui overlay that takes no
// input (imgui_window.hpp, overlay), drawn in the HUD's style
// (src/ui/traffic_labels_view.cpp) in place of XPMP2's bitmap labels.
//
// Main thread only. traffic_layer owns it: it starts and stops it with the
// traffic, and says what to tag.
namespace zoal_atc::xplane::traffic_labels {

// Shows and hides the tags; bindable, and in the plugin menu.
inline constexpr const char *kCommandName = "zoal_atc/traffic_labels_toggle";

// One aircraft to tag: what it says and where it is in X-Plane's local frame.
struct Target {
  gui::TrafficLabel label;
  double x = 0.0, y = 0.0, z = 0.0;
};

// Creates the overlay and its command. `targets` is asked once a frame while
// the tags are drawn. False when X-Plane cannot make the window, in which case
// the caller keeps XPMP2's own labels.
bool start(std::function<std::vector<Target>()> targets);
void stop();
void set_enabled(bool enabled); // with the traffic layer
void tick();                    // flight loop: size and density

} // namespace zoal_atc::xplane::traffic_labels

#endif
