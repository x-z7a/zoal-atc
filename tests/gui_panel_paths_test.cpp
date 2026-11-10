#include "zoal_atc/gui/panel_paths.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_eq(const std::string &got, const std::string &want,
               const char *label) {
  if (got == want) {
    return;
  }
  std::cerr << label << ": got [" << got << "], want [" << want << "]\n";
  std::exit(1);
}

} // namespace

int main() {
  using zoal_atc::gui::base_name;
  using zoal_atc::gui::directory_name;
  using zoal_atc::gui::join_path;
  using zoal_atc::gui::plugin_root_from_binary_path;

  expect_eq(directory_name("/xp/Resources/plugins/zoal-atc/mac_x64/a.xpl"),
            "/xp/Resources/plugins/zoal-atc/mac_x64", "directory_name");
  expect_eq(base_name("/xp/Resources/plugins/zoal-atc/mac_x64/"),
            "mac_x64", "base_name");
  expect_eq(join_path("/xp/Resources/plugins/zoal-atc", "apps"),
            "/xp/Resources/plugins/zoal-atc/apps", "join_path posix");
  expect_eq(join_path("C:\\XP\\Resources\\plugins\\zoal-atc", "apps"),
            "C:\\XP\\Resources\\plugins\\zoal-atc\\apps",
            "join_path windows");

  expect_eq(plugin_root_from_binary_path(
                "/xp/Resources/plugins/zoal-atc/mac_x64/zoal-atc.xpl"),
            "/xp/Resources/plugins/zoal-atc", "mac plugin root");
  expect_eq(plugin_root_from_binary_path(
                "C:\\XP\\Resources\\plugins\\zoal-atc\\win_x64\\zoal-atc.xpl"),
            "C:\\XP\\Resources\\plugins\\zoal-atc", "win plugin root");
  expect_eq(plugin_root_from_binary_path("/tmp/zoal-atc.xpl"), "/tmp",
            "flat plugin root");

  return 0;
}
