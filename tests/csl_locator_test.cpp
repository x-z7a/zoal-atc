#include "test_support.hpp"
#include "zoal_atc/traffic/csl_locator.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <cstdint>

namespace fs = std::filesystem;
using namespace zoal_atc::traffic;
using test::require;

namespace {
fs::path root = fs::temp_directory_path() / ("zoal_csl_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(&root)));

void touch(const fs::path &file) {
  fs::create_directories(file.parent_path());
  std::ofstream(file) << "x";
}

void eq(const std::string &got, const std::string &want, const std::string &what) {
  require(got == want, what + ": got '" + got + "', want '" + want + "'");
}

void setup() {
  fs::remove_all(root);
  
  touch(root/"plugins"/"LiveTraffic"/"Resources"/"CSL"/"Bluebell"/"B738"/"xsb_aircraft.txt");
  touch(root/"plugins"/"Swift"/"data"/"csl"/"X-CSL"/"xsb_aircraft.txt");
  touch(root/"plugins"/"Empty"/"Resources"/"CSL"/"readme.txt");
  touch(root/"plugins"/"Deep"/"a"/"b"/"c"/"CSL"/"p"/"xsb_aircraft.txt");
  touch(root/"mine"/"MyCSL"/"pkg"/"xsb_aircraft.txt");
  
  fs::create_directories(root/"plugin_root");
}

void test_holds_csl_finds_models_below() {
  require(holds_csl(root/"mine"/"MyCSL"), "holds_csl should find models in mine/MyCSL");
  require(holds_csl(root/"mine"), "holds_csl should find models in mine");
  require(!holds_csl(root/"plugins"/"Empty"/"Resources"/"CSL"), "holds_csl should not find models in Empty/Resources/CSL");
  require(!holds_csl(root/"nope"), "holds_csl should not find models in nope");
  require(!holds_csl(root/"mine"/"MyCSL"/"pkg"/"xsb_aircraft.txt"), "holds_csl should not find models in xsb_aircraft.txt file");
  require(!holds_csl(root/"mine", 1), "holds_csl should not find models at depth 1");
  require(holds_csl(root/"mine", 2), "holds_csl should find models at depth 2");
}

void test_defaults_in_order() {
  auto v = default_csl_folders(root/"plugin_root", root/"plugins");
  require(v.size() == 5, "default_csl_folders should return 5 folders");
  require(v[0] == root/"plugin_root"/"Resources"/"CSL", "first folder should be plugin_root/Resources/CSL");
  require(v[2] == root/"plugins"/"LiveTraffic"/"Resources"/"CSL", "third folder should be plugins/LiveTraffic/Resources/CSL");
}

void test_discover_finds_csl_folders() {
  auto f = discover_csl_folders(root/"plugins");
  require(f.size() == 2, "discover_csl_folders should find 2 folders");
  eq(f[0].label, "LiveTraffic/Resources/CSL", "first label should be LiveTraffic/Resources/CSL");
  eq(f[1].label, "Swift/data/csl", "second label should be Swift/data/csl");
  require(f[0].path == root/"plugins"/"LiveTraffic"/"Resources"/"CSL", "first path should be plugins/LiveTraffic/Resources/CSL");
  require(discover_csl_folders(root/"missing").empty(), "discover_csl_folders should return empty for missing directory");
}

void test_normalise_paths() {
  eq(normalise_user_path("  \"/a b/CSL\" \n", ""), "/a b/CSL", "normalise_user_path should handle quoted paths");
  eq(normalise_user_path("~/X-Plane/CSL", "/Users/me"), "/Users/me/X-Plane/CSL", "normalise_user_path should expand ~");
  eq(normalise_user_path("/Volumes/My\\ Disk/CSL", ""), "/Volumes/My Disk/CSL", "normalise_user_path should handle escaped spaces");
  eq(normalise_user_path("~", ""), "~", "normalise_user_path should handle just ~");
  eq(normalise_user_path("'/x'", ""), "/x", "normalise_user_path should handle single quotes");
}

void test_choose_prefers_configured() {
  auto d = default_csl_folders(root/"plugin_root", root/"plugins");
  auto c = choose_csl((root/"mine"/"MyCSL").string(), d);
  require(c.configured, "choose_csl should report configured=true when path is valid");
  require(c.path == root/"mine"/"MyCSL", "choose_csl should return configured path");
  require(c.skipped.empty(), "choose_csl should not skip anything when path is valid");
}

void test_choose_falls_back() {
  auto d = default_csl_folders(root/"plugin_root", root/"plugins");
  auto c = choose_csl((root/"plugins"/"Empty").string(), d);
  require(!c.configured, "choose_csl should report configured=false when path is invalid");
  require(c.path == root/"plugins"/"LiveTraffic"/"Resources"/"CSL", "choose_csl should fall back to default");
  require(!c.skipped.empty(), "choose_csl should skip the invalid path");
  
  auto c2 = choose_csl("", d);
  require(c2.path == root/"plugins"/"LiveTraffic"/"Resources"/"CSL", "choose_csl should return first default when no config given");
  require(c2.skipped.empty(), "choose_csl should not skip anything when using defaults");
}

void test_choose_none() {
  auto c = choose_csl("", {root/"plugin_root"/"Resources"/"CSL"});
  require(c.path.empty(), "choose_csl should return empty path when no valid folders found");
}
} // anonymous namespace

int main() {
  setup();
  
  test_holds_csl_finds_models_below();
  test_defaults_in_order();
  test_discover_finds_csl_folders();
  test_normalise_paths();
  test_choose_prefers_configured();
  test_choose_falls_back();
  test_choose_none();
  
  std::error_code ec;
  fs::remove_all(root, ec);
  std::cout << "csl locator tests passed\n";
  return 0;
}
