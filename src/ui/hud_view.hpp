#ifndef ZOAL_ATC_UI_HUD_VIEW_HPP
#define ZOAL_ATC_UI_HUD_VIEW_HPP

#include "kit.hpp"
#include "zoal_atc/gui/hud_session.hpp"

#include <cstdint>
#include <string>

namespace zoal_atc::ui {

// What the HUD remembers between frames, and what the last frame asked for.
// A press is recorded here while drawing and run by the caller afterwards: an
// action may open a window, and X-Plane's windows are not rearranged mid-draw.
struct HudViewState {
  gui::HudAction pressed = gui::HudAction::none;
  std::string pressed_call;
  bool pressed_next = false;
  bool pressed_dismiss_call = false;
  bool pressed_dismiss_notice = false;
  // What the pilot typed in the HUD's line, and what they sent with Enter.
  char typed[256] = {};
  std::string pressed_text;
  // The call strip's width last frame, which the typing line matches.
  float strip_width = 0.0f;
  // How far the call strip is scrolled, in boxels.
  float strip_scroll = 0.0f;
  // What the last frame drew, so the window can be made exactly that big.
  float content_width = 0.0f;
  float content_height = 36.0f;
};

struct HudFrame {
  // Unix milliseconds, which transmissions are stamped and faded with.
  std::int64_t wall_ms = 0;
};

// Draws one frame of the HUD into the current ImGui context, which must be as
// big as the screen: the HUD hangs from its top right corner.
void draw_hud(gui::HudSession &session, HudViewState &state, const Typeface &face,
              const HudFrame &frame);

} // namespace zoal_atc::ui

#endif
