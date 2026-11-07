#include "zoal_atc/weather/metar.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace zoal_atc::weather;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void require_contains(const std::string &haystack, const std::string &needle,
                      const std::string &message) {
  require(haystack.find(needle) != std::string::npos,
          message + " (missing `" + needle + "` in `" +
              haystack.substr(0, 200) + "`)");
}

struct TempRoot {
  fs::path root;

  TempRoot() {
    root = fs::temp_directory_path() / "zoal_atc_metar_test_root";
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~TempRoot() { fs::remove_all(root); }

  void write(const fs::path &rel, const std::string &content) const {
    const fs::path full = root / rel;
    fs::create_directories(full.parent_path());
    std::ofstream out(full, std::ios::binary);
    out << content;
  }
};

void root_resolves_from_plugin_subdirectory() {
  TempRoot tmp;
  tmp.write(fs::path("Output") / "real weather" / "metar-2026-07-03-11.txt",
            "KPAO 031100Z\n");
  const fs::path plugin_dir =
      tmp.root / "Resources" / "plugins" / "zoal-atc" / "mac_x64";
  fs::create_directories(plugin_dir);

  require(resolve_xplane_root(plugin_dir.string()) == tmp.root.string(),
          "plugin subdirectory resolves to X-Plane root");
  require(newest_metar_file(metar_dir(plugin_dir.string())).has_value(),
          "metar found from plugin subdirectory");
}

void metar_newest_file_and_frame() {
  TempRoot tmp;
  tmp.write(fs::path("Output") / "real weather" / "metar-2026-07-03-11.txt",
            "KPAO 031100Z 29009KT 10SM FEW015 18/12 A2998\n");
  tmp.write(fs::path("Output") / "real weather" / "notes.txt", "ignore me");

  const auto newest = newest_metar_file(metar_dir(tmp.root.string()));
  require(newest.has_value(), "newest metar found");
  require(newest->find("metar-2026-07-03-11.txt") != std::string::npos,
          "picked the metar file, not other files");

  const auto text = read_file_text(*newest);
  require(text.has_value(), "metar readable");
  const std::string frame = build_metar_frame(*text);
  require_contains(frame, "\"type\":\"weather_metar\"", "metar frame type");
  require_contains(frame, "KPAO 031100Z", "metar text");

  require(!newest_metar_file((tmp.root / "nope").string()).has_value(),
          "missing dir yields no metar");
}

} // namespace

int main() {
  root_resolves_from_plugin_subdirectory();
  metar_newest_file_and_frame();
  std::cout << "metar tests passed\n";
  return 0;
}
