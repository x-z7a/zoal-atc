#include "zoal_atc/taxi/scenery_match.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace zoal_atc::taxi {

LightsMode parse_lights_mode(const std::string &text) {
  // Trim leading whitespace
  size_t start = 0;
  while (start < text.length() && (text[start] == ' ' || text[start] == '\t' ||
                                    text[start] == '\r' || text[start] == '\n')) {
    start++;
  }

  // Trim trailing whitespace
  size_t end = text.length();
  while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r' || text[end - 1] == '\n')) {
    end--;
  }

  // Build trimmed lowercase string
  std::string trimmed;
  for (size_t i = start; i < end; i++) {
    trimmed += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
  }

  if (trimmed == "off") return LightsMode::Off;
  if (trimmed == "matching") return LightsMode::MatchingScenery;
  return LightsMode::Always;
}

const char *lights_mode_name(LightsMode mode) {
  if (mode == LightsMode::Off) return "off";
  if (mode == LightsMode::MatchingScenery) return "matching";
  return "always";
}

std::vector<std::string> scenery_pack_order(const std::string &ini_text) {
  std::vector<std::string> result;
  std::istringstream stream(ini_text);
  std::string line;

  while (std::getline(stream, line)) {
    // Strip trailing '\r'
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Strip trailing spaces
    while (!line.empty() && line.back() == ' ') {
      line.pop_back();
    }

    // Check if line starts with exactly "SCENERY_PACK " (not SCENERY_PACK_DISABLED)
    const std::string prefix = "SCENERY_PACK ";
    if (line.length() >= prefix.length() && line.substr(0, prefix.length()) == prefix) {
      std::string path = line.substr(prefix.length());

      // Replace backslashes with forward slashes
      for (auto &c : path) {
        if (c == '\\') c = '/';
      }

      // Remove trailing '/'
      while (!path.empty() && path.back() == '/') {
        path.pop_back();
      }

      // Handle *GLOBAL_AIRPORTS*
      if (path == "*GLOBAL_AIRPORTS*") {
        result.push_back(kGlobalAirports);
      } else if (!path.empty()) {
        result.push_back(path);
      }
    }
  }

  return result;
}

bool apt_dat_has_airport(const std::string &apt_text, const std::string &icao) {
  std::istringstream stream(apt_text);
  std::string line;

  while (std::getline(stream, line)) {
    // Strip trailing '\r'
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Split line on whitespace
    std::istringstream line_stream(line);
    std::vector<std::string> fields;
    std::string field;

    while (line_stream >> field) {
      fields.push_back(field);
    }

    // Check conditions: at least 5 fields, first is "1", "16" or "17", fifth matches icao
    if (fields.size() >= 5 &&
        (fields[0] == "1" || fields[0] == "16" || fields[0] == "17")) {
      // Compare ICAO case-insensitively
      std::string field_icao = fields[4];
      std::string icao_lower = icao;

      for (auto &c : field_icao) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      for (auto &c : icao_lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }

      if (field_icao == icao_lower) {
        return true;
      }
    }
  }

  return false;
}

std::string scenery_for_airport(const std::string &xplane_root, const std::string &icao) {
  try {
    // Read the scenery_packs.ini file
    std::filesystem::path ini_path =
        std::filesystem::path(xplane_root) / "Custom Scenery" / "scenery_packs.ini";

    std::ifstream ini_file(ini_path, std::ios::binary);
    if (!ini_file) {
      return kGlobalAirports;
    }

    std::string ini_text((std::istreambuf_iterator<char>(ini_file)),
                         std::istreambuf_iterator<char>());
    ini_file.close();

    // Get the scenery pack order
    auto packs = scenery_pack_order(ini_text);

    for (const auto &entry : packs) {
      if (entry == kGlobalAirports) {
        return kGlobalAirports;
      }

      // Construct the path
      std::filesystem::path pack_path(entry);
      if (pack_path.is_relative()) {
        pack_path = std::filesystem::path(xplane_root) / pack_path;
      }

      // Try to read apt.dat
      std::filesystem::path apt_path = pack_path / "Earth nav data" / "apt.dat";
      std::ifstream apt_file(apt_path, std::ios::binary);
      if (!apt_file) {
        continue;
      }

      std::string apt_text((std::istreambuf_iterator<char>(apt_file)),
                           std::istreambuf_iterator<char>());
      apt_file.close();

      if (apt_dat_has_airport(apt_text, icao)) {
        // Get the last non-empty component of entry
        std::string folder_name = entry;
        // Remove trailing slashes (should already be done, but just in case)
        while (!folder_name.empty() && folder_name.back() == '/') {
          folder_name.pop_back();
        }
        // Find the last component after the last '/'
        size_t last_slash = folder_name.find_last_of('/');
        if (last_slash != std::string::npos) {
          folder_name = folder_name.substr(last_slash + 1);
        }
        return folder_name;
      }
    }

    return kGlobalAirports;
  } catch (...) {
    return kGlobalAirports;
  }
}

bool should_light(LightsMode mode, const std::string &route_scenery,
                  const std::string &own_scenery) {
  if (mode == LightsMode::Off) return false;
  if (mode == LightsMode::Always) return true;
  // MatchingScenery
  return !route_scenery.empty() && route_scenery == own_scenery;
}

} // namespace zoal_atc::taxi
