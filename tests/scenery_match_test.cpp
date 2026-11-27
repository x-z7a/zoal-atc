#include "test_support.hpp"
#include "zoal_atc/taxi/scenery_match.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace zoal_atc::taxi;
using test::require;

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

void modes_parse() {
  require(parse_lights_mode("off") == LightsMode::Off, "off");
  require(parse_lights_mode(" Matching ") == LightsMode::MatchingScenery, "matching");
  require(parse_lights_mode("always") == LightsMode::Always, "always");
  require(parse_lights_mode("") == LightsMode::Always, "default always");
  require(parse_lights_mode("bogus") == LightsMode::Always, "unknown always");
  require(std::string(lights_mode_name(LightsMode::MatchingScenery)) == "matching", "name");
}

void pack_order_reads_enabled_packs() {
  const std::string ini =
      "I\r\n1000 Version\r\nSCENERY\r\n\r\n"
      "SCENERY_PACK Custom Scenery/CYYZ Toronto/\r\n"
      "SCENERY_PACK_DISABLED Custom Scenery/Old KJFK/\r\n"
      "SCENERY_PACK *GLOBAL_AIRPORTS*\r\n"
      "SCENERY_PACK Custom Scenery\\Mesh\\\r\n";
  auto order = scenery_pack_order(ini);
  require(order.size() == 3, "three enabled packs, got " + std::to_string(order.size()));
  require(order[0] == "Custom Scenery/CYYZ Toronto", "first: " + order[0]);
  require(order[1] == kGlobalAirports, "global marker: " + order[1]);
  require(order[2] == "Custom Scenery/Mesh", "backslashes: " + order[2]);
}

void apt_dat_finds_headers() {
  const std::string apt = "I\n1200 Generated\n\n1    569 1 0 CYYZ Toronto Pearson\n100 ...\n"
                          "16 0 0 0 H123 Heliport\n";
  require(apt_dat_has_airport(apt, "cyyz"), "land airport, case-insensitive");
  require(apt_dat_has_airport(apt, "H123"), "heliport header");
  require(!apt_dat_has_airport(apt, "CYUL"), "absent");
  require(!apt_dat_has_airport("100 0 0 0 CYYZ\n", "CYYZ"), "not a header row");
}

void the_first_pack_with_the_airport_wins() {
  const auto root = std::filesystem::temp_directory_path() / "zoal_scenery_match_test";
  std::filesystem::remove_all(root);
  write(root / "Custom Scenery/scenery_packs.ini",
        "I\n1000 Version\nSCENERY\n\n"
        "SCENERY_PACK Custom Scenery/Toronto HD/\n"
        "SCENERY_PACK Custom Scenery/Montreal/\n"
        "SCENERY_PACK *GLOBAL_AIRPORTS*\n"
        "SCENERY_PACK Custom Scenery/Ottawa Late/\n");
  write(root / "Custom Scenery/Toronto HD/Earth nav data/apt.dat", "I\n1 569 1 0 CYYZ Pearson\n");
  write(root / "Custom Scenery/Montreal/Earth nav data/apt.dat", "I\n1 118 1 0 CYUL Trudeau\n");
  write(root / "Custom Scenery/Ottawa Late/Earth nav data/apt.dat", "I\n1 374 1 0 CYOW Ottawa\n");
  require(scenery_for_airport(root.string(), "CYYZ") == "Toronto HD", "custom pack");
  require(scenery_for_airport(root.string(), "CYUL") == "Montreal", "second pack");
  require(scenery_for_airport(root.string(), "CYOW") == kGlobalAirports,
          "a pack below the global airports loses to them");
  require(scenery_for_airport(root.string(), "EGLL") == kGlobalAirports, "nobody's: global");
  require(scenery_for_airport((root / "nowhere").string(), "CYYZ") == kGlobalAirports,
          "no ini: global");
  std::filesystem::remove_all(root);
}

void lighting_follows_the_pilots_choice() {
  require(should_light(LightsMode::Always, "A", "B"), "always lights");
  require(!should_light(LightsMode::Off, "A", "A"), "off never");
  require(should_light(LightsMode::MatchingScenery, "A", "A"), "matching lights the same");
  require(!should_light(LightsMode::MatchingScenery, "A", "B"), "not other scenery");
  require(!should_light(LightsMode::MatchingScenery, "", ""), "unknown is not a match");
}

} // namespace

int main() {
  modes_parse();
  pack_order_reads_enabled_packs();
  apt_dat_finds_headers();
  the_first_pack_with_the_airport_wins();
  lighting_follows_the_pilots_choice();
  std::cout << "scenery_match tests passed\n";
  return 0;
}
