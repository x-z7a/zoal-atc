#ifndef ZOAL_ATC_UI_HUD_PALETTE_HPP
#define ZOAL_ATC_UI_HUD_PALETTE_HPP

#include "kit.hpp"
#include "zoal_atc/gui/hud_session.hpp"

#include "imgui.h"

// The HUD's colours, per theme: what the HUD and the traffic labels over the
// sky are both drawn in, so the two read as one instrument.
namespace zoal_atc::ui::hud_style {

using gui::HudTheme;

// The roles the HUD uses from tokens.css, per theme.
struct Palette {
  unsigned sunken, text, text_dim, text_feed, accent, active, warn, on_warn,
      danger, delivery, seat, tower, raised, border;
  float border_alpha;
  unsigned shadow;
  float shadow_alpha;
};

inline constexpr Palette kGraphite{0x121213, 0xefebe5, 0xa6a099, 0xd8d2ca, 0xefb45c,
                            0xa9d39f, 0xff9b6a, 0x2b1206, 0xff8a7a, 0xc4b3e6,
                            0xe8a989, 0x9fc4e8, 0x2d2d30, 0xfff8f0, 0.15f,
                            0x000000, 0.70f};
inline constexpr Palette kDaylight{0xece6dd, 0x27231e, 0x6b6358, 0x3d3731, 0xa35d08,
                            0x3b7738, 0xc0561d, 0xffffff, 0xb93a2a, 0x6d53a6,
                            0xad552a, 0x2d6a9b, 0xe8e2d8, 0x463219, 0.21f,
                            0xffffff, 0.55f};
inline constexpr Palette kNight{0x090807, 0xc8b8a7, 0x85776a, 0xb4a493, 0xcc8748,
                         0x8cab81, 0xd8844f, 0x1f0c03, 0xd4705f, 0xa394c0,
                         0xc48b70, 0x8aa5c0, 0x1f1b18, 0xffebd7, 0.12f,
                         0x000000, 0.70f};

inline const Palette &palette(HudTheme theme) {
  switch (theme) {
  case HudTheme::daylight:
    return kDaylight;
  case HudTheme::night:
    return kNight;
  case HudTheme::graphite:
    break;
  }
  return kGraphite;
}

// One tint for all of it, so the HUD reads as one instrument.
inline ImU32 glass(const Palette &p) { return rgba(p.sunken, 0.72f); }
inline ImU32 edge(const Palette &p) { return rgba(p.accent, 0.22f); }

} // namespace zoal_atc::ui::hud_style

#endif
