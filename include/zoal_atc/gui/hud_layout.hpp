#ifndef ZOAL_ATC_GUI_HUD_LAYOUT_HPP
#define ZOAL_ATC_GUI_HUD_LAYOUT_HPP

#include <optional>
#include <string>

namespace zoal_atc::gui {

// Where the HUD sits, in X-Plane's global desktop boxels (y up).
//
// The HUD is a chromeless browser window, and a browser window takes every
// click inside its rectangle -- transparent or not. So it is pinned to the top
// right and kept exactly as tall as what the page is showing: an empty HUD is a
// thin strip, not a pane of invisible glass over the cockpit.
struct ScreenRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct HudPlacement {
  int width = 440;
  // Off the right edge, and down from the top far enough to clear X-Plane's
  // menu bar when it slides in.
  int margin_right = 16;
  int margin_top = 40;
  // Never smaller than the drawer handle, never taller than the screen allows.
  int min_height = 36;
};

// hud_rect pins a window `content_height` tall to the top right of `screen`,
// clamped so it stays on screen however small the screen or tall the content.
ScreenRect hud_rect(const ScreenRect &screen, int content_height,
                    const HudPlacement &placement = {});

// parse_hud_layout reads the page's {"height": N} report. nullopt for anything
// that is not a sensible positive height, so a malformed post cannot shrink the
// HUD to nothing or blow it up past the screen.
std::optional<int> parse_hud_layout(const std::string &payload_json);

} // namespace zoal_atc::gui

#endif
