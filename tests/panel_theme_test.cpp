#include "zoal_atc/gui/panel_theme.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect_eq(const std::string &got, const std::string &want,
               const char *label) {
  if (got == want) {
    return;
  }
  std::cerr << label << ": got '" << got << "', want '" << want << "'\n";
  ++g_failures;
}

void expect_true(bool condition, const char *label) {
  if (condition) {
    return;
  }
  std::cerr << label << ": expected true\n";
  ++g_failures;
}

using zoal_atc::gui::known_panel_theme;
using zoal_atc::gui::panel_theme_or_default;

void knows_every_shipped_theme() {
  expect_true(known_panel_theme("graphite").has_value(), "graphite");
  expect_true(known_panel_theme("daylight").has_value(), "daylight");
  expect_true(known_panel_theme("night").has_value(), "night");
}

// A save is refused rather than stored, so the file never holds a theme the
// page has no colours for.
void refuses_anything_else() {
  expect_true(!known_panel_theme("").has_value(), "empty");
  expect_true(!known_panel_theme("Daylight").has_value(), "case matters");
  expect_true(!known_panel_theme("solarized").has_value(), "unknown");
}

// A hand-edited or stale value must still draw something.
void falls_back_to_graphite() {
  expect_eq(panel_theme_or_default(""), "graphite", "unset");
  expect_eq(panel_theme_or_default("neon"), "graphite", "unknown");
  expect_eq(panel_theme_or_default("night"), "night", "kept");
}

} // namespace

int main() {
  knows_every_shipped_theme();
  refuses_anything_else();
  falls_back_to_graphite();
  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "panel_theme tests passed\n";
  return EXIT_SUCCESS;
}
