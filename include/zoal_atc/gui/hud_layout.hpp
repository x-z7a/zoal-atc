#ifndef ZOAL_ATC_GUI_HUD_LAYOUT_HPP
#define ZOAL_ATC_GUI_HUD_LAYOUT_HPP

namespace zoal_atc::gui {

// Where the HUD sits, in X-Plane's global desktop boxels (y up).
//
// The HUD's window takes every click inside its rectangle -- transparent or
// not -- so it is pinned to the top right and kept exactly as big as what it is
// drawing: an idle HUD is its bar, not a pane of invisible glass over the
// cockpit. hud_window.cpp narrows `width` to the content, too.
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
  // Never smaller than the bar, never taller than the screen allows.
  int min_height = 36;
};

// hud_rect pins a window `content_height` tall to the top right of `screen`,
// clamped so it stays on screen however small the screen or tall the content.
ScreenRect hud_rect(const ScreenRect &screen, int content_height,
                    const HudPlacement &placement = {});

} // namespace zoal_atc::gui

#endif
