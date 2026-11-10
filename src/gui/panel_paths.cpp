#include "zoal_atc/gui/panel_paths.hpp"


namespace zoal_atc::gui {

namespace {

bool is_separator(char c) { return c == '/' || c == '\\'; }

char preferred_separator(std::string_view path) {
  const auto slash = path.find('/');
  const auto backslash = path.find('\\');
  if (backslash != std::string_view::npos &&
      slash == std::string_view::npos) {
    return '\\';
  }
  return '/';
}

bool is_platform_directory(std::string_view name) {
  return name == "mac_x64" || name == "win_x64" || name == "lin_x64";
}

} // namespace

std::string directory_name(std::string_view path) {
  while (!path.empty() && is_separator(path.back())) {
    path.remove_suffix(1);
  }
  const auto pos = path.find_last_of("/\\");
  if (pos == std::string_view::npos) {
    return "";
  }
  if (pos == 0) {
    return std::string(path.substr(0, 1));
  }
  return std::string(path.substr(0, pos));
}

std::string base_name(std::string_view path) {
  while (!path.empty() && is_separator(path.back())) {
    path.remove_suffix(1);
  }
  const auto pos = path.find_last_of("/\\");
  if (pos == std::string_view::npos) {
    return std::string(path);
  }
  return std::string(path.substr(pos + 1));
}

std::string join_path(std::string_view base, std::string_view child) {
  if (base.empty()) {
    return std::string(child);
  }
  if (child.empty()) {
    return std::string(base);
  }
  std::string out(base);
  while (!out.empty() && is_separator(out.back())) {
    out.pop_back();
  }
  std::string suffix(child);
  while (!suffix.empty() && is_separator(suffix.front())) {
    suffix.erase(suffix.begin());
  }
  out.push_back(preferred_separator(base));
  out += suffix;
  return out;
}

std::string plugin_root_from_binary_path(std::string_view plugin_binary_path) {
  const std::string binary_dir = directory_name(plugin_binary_path);
  if (is_platform_directory(base_name(binary_dir))) {
    return directory_name(binary_dir);
  }
  return binary_dir;
}

} // namespace zoal_atc::gui
