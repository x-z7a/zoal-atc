#include "zoal_atc/gui/hud_layout.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int g_failures = 0;

void expect_int(int got, int want, const char *label) {
  if (got == want) {
    return;
  }
  std::cerr << label << ": got " << got << ", want " << want << "\n";
  ++g_failures;
}

void expect_true(bool condition, const char *label) {
  if (condition) {
    return;
  }
  std::cerr << label << ": expected true\n";
  ++g_failures;
}

using zoal_atc::gui::ScreenRect;

// A 1920x1080 desktop, X-Plane style: y grows upward.
constexpr ScreenRect kScreen{0, 1080, 1920, 0};

void pins_to_the_top_right() {
  const auto rect = zoal_atc::gui::hud_rect(kScreen, 200);
  expect_int(rect.right, 1920 - 16, "right margin");
  expect_int(rect.left, 1920 - 16 - 440, "fixed width");
  expect_int(rect.top, 1080 - 40, "clears the menu bar");
  expect_int(rect.top - rect.bottom, 200, "as tall as the content");
}

// Empty is a strip, not a pane: it must not take the clicks of a cockpit it
// is not drawing anything over.
void an_empty_hud_is_only_its_handle() {
  const auto rect = zoal_atc::gui::hud_rect(kScreen, 0);
  expect_int(rect.top - rect.bottom, 36, "minimum height");
}

void never_runs_off_the_screen() {
  const auto tall = zoal_atc::gui::hud_rect(kScreen, 5000);
  expect_true(tall.bottom >= kScreen.bottom, "bottom stays on screen");

  const ScreenRect narrow{100, 400, 400, 0};
  const auto rect = zoal_atc::gui::hud_rect(narrow, 100);
  expect_true(rect.left >= narrow.left, "left stays on screen");
  expect_int(rect.right - rect.left, 300, "width shrinks to fit");
}

// Global desktop coordinates need not start at zero: a second monitor to the
// left of the main one makes them negative.
void follows_an_offset_desktop() {
  const ScreenRect offset{-1920, 1080, 0, 0};
  const auto rect = zoal_atc::gui::hud_rect(offset, 120);
  expect_int(rect.right, -16, "right edge of the offset desktop");
}

// The HUD narrows its window to what it is drawing; the right edge stays put.
void narrows_to_its_content() {
  zoal_atc::gui::HudPlacement placement;
  placement.width = 120;
  const auto rect = zoal_atc::gui::hud_rect(kScreen, 36, placement);
  expect_int(rect.right, 1920 - 16, "right edge unchanged");
  expect_int(rect.right - rect.left, 120, "as wide as the content");
}

} // namespace

int main() {
  pins_to_the_top_right();
  an_empty_hud_is_only_its_handle();
  never_runs_off_the_screen();
  follows_an_offset_desktop();
  narrows_to_its_content();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
