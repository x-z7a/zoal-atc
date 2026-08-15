#ifndef ZOAL_ATC_GUI_PANEL_PATHS_HPP
#define ZOAL_ATC_GUI_PANEL_PATHS_HPP

#include <string>
#include <string_view>

namespace zoal_atc::gui {

std::string directory_name(std::string_view path);
std::string base_name(std::string_view path);
std::string join_path(std::string_view base, std::string_view child);
std::string plugin_root_from_binary_path(std::string_view plugin_binary_path);
std::string apps_dir(std::string_view plugin_root);
std::string assets_dir(std::string_view plugin_root);
std::string shell_index_path(std::string_view plugin_root);
std::string file_url(std::string_view path);
std::string query_escape(std::string_view value);
std::string shell_home_url(std::string_view plugin_root,
                           std::string_view plugin_version);

} // namespace zoal_atc::gui

#endif
