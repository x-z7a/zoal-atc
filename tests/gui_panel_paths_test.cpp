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
  using zoal_atc::gui::apps_dir;
  using zoal_atc::gui::assets_dir;
  using zoal_atc::gui::base_name;
  using zoal_atc::gui::directory_name;
  using zoal_atc::gui::file_url;
  using zoal_atc::gui::join_path;
  using zoal_atc::gui::plugin_root_from_binary_path;
  using zoal_atc::gui::query_escape;
  using zoal_atc::gui::shell_home_url;
  using zoal_atc::gui::shell_index_path;

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

  expect_eq(apps_dir("/xp/Resources/plugins/zoal-atc"),
            "/xp/Resources/plugins/zoal-atc/apps", "apps_dir");
  expect_eq(assets_dir("/xp/Resources/plugins/zoal-atc"),
            "/xp/Resources/plugins/zoal-atc/assets", "assets_dir");
  expect_eq(shell_index_path("/xp/Resources/plugins/zoal-atc"),
            "/xp/Resources/plugins/zoal-atc/apps/zoal-atc/index.html",
            "shell_index_path");
  expect_eq(file_url("/xp/Resources/plugins/zoal atc/apps/zoal-atc/index.html"),
            "file:///xp/Resources/plugins/zoal%20atc/apps/zoal-atc/index.html",
            "file_url");
  expect_eq(query_escape("0.1.0 dev"), "0.1.0%20dev", "query_escape");
  expect_eq(shell_home_url("/xp/Resources/plugins/zoal-atc", "0.1.0"),
            "file:///xp/Resources/plugins/zoal-atc/apps/zoal-atc/index.html"
            "?plugin_version=0.1.0&skyscript=ready",
            "shell_home_url");

  return 0;
}
