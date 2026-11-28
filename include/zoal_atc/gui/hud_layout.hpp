#ifndef ZOAL_ATC_GUI_HUD_LAYOUT_HPP
#define ZOAL_ATC_GUI_HUD_LAYOUT_HPP

namespace zoal_atc::gui {

// Where the HUD sits, in X-Plane's global desktop boxels (y up).
//
// The HUD's window takes every click inside its rectangle -- transparent or
// not -- so it is kept exactly as big as what it is drawing: an idle HUD is its
// bar, not a pane of invisible glass over the cockpit. hud_window.cpp narrows
// `width` to the content, too. It hangs from its top right corner, which is
// where the pilot dragged its bar to (the top right of the screen until they
// do), and grows leftward and down from there.
struct ScreenRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

inline constexpr int kDefaultMarginRight = 16;
inline constexpr int kDefaultMarginTop = 40;

struct HudPlacement {
  int width = 440;
  // Off the right edge, and down from the top far enough to clear X-Plane's
  // menu bar when it slides in. Measured from the screen's top right corner
  // so a HUD dragged into the corner of one screen stays in that corner when
  // the window is resized. The pilot's drag moves these.
  int margin_right = kDefaultMarginRight;
  int margin_top = kDefaultMarginTop;
  // Never smaller than the bar, never taller than the screen allows.
  int min_height = 36;
};

// hud_rect pins a window `content_height` tall to the top right of `screen`,
// clamped so it stays on screen however small the screen or tall the content.
ScreenRect hud_rect(const ScreenRect &screen, int content_height,
                    const HudPlacement &placement = {});

// hud_dragged moves a HUD `width` wide by a drag of (`dx`, `dy`) boxels, x
// right and y up, as X-Plane measures the mouse. The margins are clamped so
// the bar stays wholly on `screen`: a HUD dragged off the edge stops at it
// rather than leaving the pilot nothing to drag it back by.
HudPlacement hud_dragged(const HudPlacement &from, int dx, int dy,
                         const ScreenRect &screen, int width);

} // namespace zoal_atc::gui

#endif
