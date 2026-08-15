#ifndef ZOAL_ATC_GUI_SKYSCRIPT_PANEL_HPP
#define ZOAL_ATC_GUI_SKYSCRIPT_PANEL_HPP

#include <cstdint>
#include <functional>
#include <string>

namespace zoal_atc::gui {

class GuiBridge;

struct SkyscriptPanelConfig {
  std::string plugin_version;
  // The GUI proxy the panel talks through. Null leaves it a static shell that
  // opens and reports local plugin facts but asks the console nothing - a legal
  // state, because flying must never depend on the panel.
  GuiBridge *bridge = nullptr;
  // The plugin's monotonic clock, shared with the bridge so the request
  // timeouts a Skyscript handler starts are measured on the same time base the
  // flight loop expires them against.
  std::function<std::uint64_t()> now_ms;
};

void start_skyscript_panel(const SkyscriptPanelConfig &config);
void stop_skyscript_panel();

// tick_skyscript_panel is the only place messages reach the browser. It runs on
// the X-Plane main thread, which is where CEF is pumped: the websocket receiver
// thread queues into the bridge and never touches Skyscript itself.
void tick_skyscript_panel();

// apply_notification_control hands the panel a console->plugin
// `notification_control` frame. Returns true when the frame was one, so the
// caller stops trying other parsers. Safe before the panel exists.
bool apply_notification_control(const std::string &frame);

// clear_notification_control reverts to the local default, for when the console
// goes away. A stale preference should not outlive the console that sent it.
void clear_notification_control();

} // namespace zoal_atc::gui

#endif
