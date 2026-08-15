#include "zoal_atc/gui/panel_paths.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

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

std::string percent_escape(std::string_view value, bool keep_path_separators) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (const char raw : value) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    const bool alpha_num = std::isalnum(ch) != 0;
    const bool safe = alpha_num || ch == '-' || ch == '_' || ch == '.';
    const bool path_safe =
        keep_path_separators && (ch == '/' || ch == ':' || ch == '\\');
    if (safe || path_safe) {
      out << static_cast<char>(ch == '\\' ? '/' : ch);
      continue;
    }
    out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
  }
  return out.str();
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

std::string apps_dir(std::string_view plugin_root) {
  return join_path(plugin_root, "apps");
}

std::string assets_dir(std::string_view plugin_root) {
  return join_path(plugin_root, "assets");
}

std::string shell_index_path(std::string_view plugin_root) {
  return join_path(join_path(apps_dir(plugin_root), "zoal-atc"), "index.html");
}

std::string file_url(std::string_view path) {
  const std::string escaped = percent_escape(path, true);
  if (!escaped.empty() && escaped.front() == '/') {
    return "file://" + escaped;
  }
  return "file:///" + escaped;
}

std::string query_escape(std::string_view value) {
  return percent_escape(value, false);
}

std::string shell_home_url(std::string_view plugin_root,
                           std::string_view plugin_version) {
  // Only facts that are true for the life of the page belong in the URL. The
  // console's state arrives live over the bridge (phase 23 M2), so baking a
  // starting value in here would only give the panel something stale to show.
  return file_url(shell_index_path(plugin_root)) +
         "?plugin_version=" + query_escape(plugin_version) +
         "&skyscript=ready";
}

} // namespace zoal_atc::gui
