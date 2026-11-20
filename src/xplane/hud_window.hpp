#ifndef ZOAL_ATC_XPLANE_HUD_WINDOW_HPP
#define ZOAL_ATC_XPLANE_HUD_WINDOW_HPP

#include "zoal_atc/gui/gui_bridge.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace zoal_atc::xplane {

// The radio HUD: the callsign, the console light, a menu of the three things a
// pilot reaches for without opening the panel, and the last few transmissions.
//
// One Dear ImGui window in the top right corner, drawn natively by X-Plane
// (imgui_window.hpp) -- not a browser window, so clicking it leaves other
// plugins' UIs alone. It is a view of the GUI bridge (hud_session.hpp) and is
// exactly as big as what it is showing, so it covers no more of the cockpit
// than it draws on.

// The HUD's toggle command. The plugin's menu finds it by this name.
inline constexpr const char *kHudCommandName = "zoal_atc/hud_toggle";

// What the HUD needs from the cockpit for calls made by button
// (atc docs/architecture/pilot-menu.md). Each is optional; the HUD draws no
// call buttons without `radio`.
struct HudCockpit {
  // The COM the pilot transmits on (1 or 2) and its active frequency in Hz.
  std::function<std::pair<int, std::uint64_t>()> radio;
  // Whether the cockpit speaks replies, so the HUD waits for a reply's voice
  // before showing its words. Asked every frame: the voices load after the
  // HUD starts.
  std::function<bool()> speaks_replies;
  // Speaks a call the console took, in the pilot's own voice, on a channel.
  std::function<void(const std::string &, std::uint64_t)> speak_own_call;
};

// Creates the window and its command, attaches to `bridge` and shows the HUD.
// `steady_ms` is the plugin's monotonic clock, the one the bridge runs on.
void start_hud(gui::GuiBridge &bridge, std::function<std::uint64_t()> steady_ms,
               HudCockpit cockpit = {});
void stop_hud();

// Once a flight loop: drains the bridge, lapses timers and keeps the window
// sized and placed. Runs whether or not the HUD is showing, so an answer to a
// press is never left waiting for the next time it is opened.
void tick_hud();

// The plugin's settings.changed notice (a theme picked in the panel).
void hud_settings_changed(const std::string &payload_json);

// A controller's transmission on this channel finished playing in the
// cockpit, which is what auto readback waits for.
void hud_reply_heard(std::uint64_t frequency_hz);

// A controller's transmission started playing, or will not play: the HUD shows
// its words now rather than before its voice.
void hud_reply_spoken(const std::string &text);

} // namespace zoal_atc::xplane

#endif
