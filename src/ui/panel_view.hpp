#ifndef ZOAL_ATC_UI_PANEL_VIEW_HPP
#define ZOAL_ATC_UI_PANEL_VIEW_HPP

#include "kit.hpp"
#include "zoal_atc/gui/panel_session.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The radio panel, drawn with Dear ImGui: what the React panel drew in a
// browser window, drawn natively so a click on it never makes X-Plane's
// "active browser" -- the state that blanks other plugins' UIs.
//
// It is a view of gui::PanelSession and nothing else: no X-Plane here, so the
// same frame can be rendered off-screen (tools/panel_preview.cpp).
namespace zoal_atc::ui {

// What the view keeps between frames that the session has no business
// holding: which tab is up, what is half-typed, where the log is scrolled.
struct PanelViewState {
  int tab = 0; // 0 Home, 1 Settings
  char tune[32] = {};
  char say[512] = {};
  char token[512] = {};
  char pilot_id[64] = {};
  bool token_revealed = false;
  // The stored values the fields were last filled from. A field reloads only
  // when the plugin's value changes, so saving a theme does not wipe a pilot
  // ID being typed.
  bool token_loaded = false;
  std::string token_stored;
  bool pilot_loaded = false;
  std::string pilot_stored;
  // Whether the log follows the radio: true until the pilot scrolls up to
  // read something, true again the moment they come back down.
  bool stick = true;
  std::uint64_t comm_revision = 0;
  std::size_t comm_count = 0;
  int pending_below = 0;
  bool jump = false;
  // The pilot pressed on the panel away from any control and is moving it.
  bool moving = false;
};

struct PanelFrame {
  std::string version;
  std::uint64_t steady_ms = 0;
  std::int64_t wall_ms = 0;
  // Presses run after the frame rather than inside it: an action can write a
  // dataref, rewrite the config file or re-dial the socket, none of which
  // belongs in a draw callback.
  std::vector<std::function<void()>> *later = nullptr;
};

struct PanelViewResult {
  bool close = false;
  // The panel is being dragged from anywhere that is not a control: the
  // window follows the mouse.
  bool drag_started = false;
  bool dragging = false;
};

// Sets the context's style for the panel. Once, after fonts are loaded.
void setup_panel_style();

// One frame of the panel, filling the display.
PanelViewResult draw_panel(gui::PanelSession &session, PanelViewState &state,
                           const Typeface &face, const PanelFrame &frame);

} // namespace zoal_atc::ui

#endif
