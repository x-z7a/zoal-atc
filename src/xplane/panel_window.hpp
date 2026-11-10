#ifndef ZOAL_ATC_XPLANE_PANEL_WINDOW_HPP
#define ZOAL_ATC_XPLANE_PANEL_WINDOW_HPP

#include "zoal_atc/gui/gui_bridge.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace zoal_atc::xplane {

// The radio panel: own-ship readouts, the radio log with its composer, and
// the settings -- one Dear ImGui window drawn natively by X-Plane
// (imgui_window.hpp), like the HUD. Not a browser window, so clicking it
// leaves other plugins' UIs alone, and there is no browser to ship.
//
// It is a view of the GUI bridge (gui::PanelSession) and owns the plugin's
// menu: Plugins > zoal-atc > Toggle Panel / Toggle HUD.

// The panel's toggle command, which the menu and a key binding run.
inline constexpr const char *kPanelCommandName = "zoal_atc/gui_toggle";

struct PanelConfig {
  std::string plugin_version;
  // The GUI proxy the panel talks through.
  gui::GuiBridge *bridge = nullptr;
  // The plugin's monotonic clock, the one the bridge runs on.
  std::function<std::uint64_t()> steady_ms;
};

// Creates the window (hidden), its command and the plugin menu, and attaches
// to the bridge. Start the HUD first: the menu finds its command by name.
void start_panel(const PanelConfig &config);
void stop_panel();

// Once a flight loop: runs what was pressed, drains the bridge and moves the
// window while its mast is dragged. Runs whether or not the panel is showing,
// so it never has to ask the console what it missed.
void tick_panel();

// Brings the panel up, for the HUD's "Open panel".
void show_panel();

// The plugin's settings.changed notice (a theme saved).
void panel_settings_changed(const std::string &payload_json);

} // namespace zoal_atc::xplane

#endif
