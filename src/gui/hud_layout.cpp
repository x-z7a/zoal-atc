#include "zoal_atc/gui/hud_layout.hpp"

#include <algorithm>

namespace zoal_atc::gui {

ScreenRect hud_rect(const ScreenRect &screen, int content_height,
                    const HudPlacement &placement) {
  const int screen_width = std::max(1, screen.right - screen.left);
  const int screen_height = std::max(1, screen.top - screen.bottom);

  const int width = std::clamp(placement.width, 1, screen_width);
  const int margin_right =
      std::clamp(placement.margin_right, 0, screen_width - width);
  const int margin_top = std::clamp(placement.margin_top, 0, screen_height - 1);
  const int room = std::max(1, screen_height - margin_top);
  const int height =
      std::clamp(std::max(content_height, placement.min_height), 1, room);

  ScreenRect rect;
  rect.right = screen.right - margin_right;
  rect.left = rect.right - width;
  rect.top = screen.top - margin_top;
  rect.bottom = rect.top - height;
  return rect;
}

HudPlacement hud_dragged(const HudPlacement &from, int dx, int dy,
                         const ScreenRect &screen, int width) {
  const int screen_width = std::max(1, screen.right - screen.left);
  const int screen_height = std::max(1, screen.top - screen.bottom);
  const int bar_width = std::clamp(width, 1, screen_width);
  HudPlacement to = from;
  to.margin_right = std::clamp(from.margin_right - dx, 0, screen_width - bar_width);
  to.margin_top = std::clamp(from.margin_top - dy, 0,
                             std::max(0, screen_height - from.min_height));
  return to;
}

} // namespace zoal_atc::gui
