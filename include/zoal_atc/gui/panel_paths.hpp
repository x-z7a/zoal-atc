#ifndef ZOAL_ATC_GUI_PANEL_PATHS_HPP
#define ZOAL_ATC_GUI_PANEL_PATHS_HPP

#include <string>
#include <string_view>

namespace zoal_atc::gui {

std::string directory_name(std::string_view path);
std::string base_name(std::string_view path);
std::string join_path(std::string_view base, std::string_view child);
std::string plugin_root_from_binary_path(std::string_view plugin_binary_path);

} // namespace zoal_atc::gui

#endif
