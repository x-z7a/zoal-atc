#ifndef ZOAL_ATC_GUI_PANEL_THEME_HPP
#define ZOAL_ATC_GUI_PANEL_THEME_HPP

#include <optional>
#include <string>
#include <string_view>

namespace zoal_atc::gui {

// The colour themes the panel and HUD ship, by the name the page knows them
// by (gui/zoal-atc/src/domain/theme.ts). Stored as `panel_theme` in
// zoal_atc.cfg, so both windows -- two separate browser pages -- agree on one.
inline constexpr std::string_view kDefaultPanelTheme = "graphite";

// known_panel_theme answers the name if it is one the page can draw, nullopt
// otherwise. A save of anything else is refused rather than stored, so the
// config file never holds a theme the page would have to guess about.
std::optional<std::string> known_panel_theme(std::string_view name);

// panel_theme_or_default reads a stored value: a stale or hand-edited name
// falls back to the default instead of leaving the page unthemed.
std::string panel_theme_or_default(std::string_view stored);

} // namespace zoal_atc::gui

#endif
