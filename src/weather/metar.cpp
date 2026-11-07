#include "zoal_atc/weather/metar.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace zoal_atc::weather {

namespace fs = std::filesystem;
using transport::json_escape;

namespace {

bool looks_like_xplane_root(const fs::path &root) {
  std::error_code ec;
  return fs::is_directory(root / "Global Scenery", ec) ||
         fs::is_directory(root / "Custom Data", ec) ||
         fs::is_directory(root / "Output", ec);
}

} // namespace

std::string resolve_xplane_root(const std::string &path) {
  fs::path root(path);
  if (root.empty()) {
    return path;
  }
  std::error_code ec;
  if (fs::is_regular_file(root, ec)) {
    root = root.parent_path();
  }
  for (fs::path candidate = root; !candidate.empty();
       candidate = candidate.parent_path()) {
    if (looks_like_xplane_root(candidate)) {
      return candidate.string();
    }
    if (candidate == candidate.parent_path()) {
      break;
    }
  }
  return root.string();
}

std::string metar_dir(const std::string &xplane_root) {
  return (fs::path(resolve_xplane_root(xplane_root)) / "Output" /
          "real weather")
      .string();
}

std::optional<std::string> read_file_text(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::optional<std::string> newest_metar_file(const std::string &dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return std::nullopt;
  }
  std::optional<std::string> newest;
  fs::file_time_type newest_time{};
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("metar-", 0) != 0 ||
        name.find(".txt") == std::string::npos) {
      continue;
    }
    const auto mtime = entry.last_write_time(ec);
    if (!newest.has_value() || mtime > newest_time) {
      newest = entry.path().string();
      newest_time = mtime;
    }
  }
  return newest;
}

std::string build_metar_frame(std::string_view text) {
  std::ostringstream out;
  out << "{\"type\":\"weather_metar\",\"text\":\"" << json_escape(text)
      << "\"}";
  return out.str();
}
} // namespace zoal_atc::weather
