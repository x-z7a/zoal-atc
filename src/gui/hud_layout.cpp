#include "zoal_atc/gui/hud_layout.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <algorithm>
#include <cmath>

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

std::optional<int> parse_hud_layout(const std::string &payload_json) {
  const auto height = transport::json_number_field(payload_json, "height");
  if (!height.has_value() || !std::isfinite(*height) || *height <= 0.0 ||
      *height > 10000.0) {
    return std::nullopt;
  }
  return static_cast<int>(std::ceil(*height));
}

} // namespace zoal_atc::gui
