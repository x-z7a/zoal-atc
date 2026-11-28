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

// A drag moves the corner the HUD hangs from: right and down shrink the right
// margin and grow the top one, because X-Plane's y grows upward.
void a_drag_moves_the_corner() {
  const zoal_atc::gui::HudPlacement start;
  const auto moved = zoal_atc::gui::hud_dragged(start, -300, -200, kScreen, 180);
  expect_int(moved.margin_right, 16 + 300, "dragged left");
  expect_int(moved.margin_top, 40 + 200, "dragged down");
  const auto rect = zoal_atc::gui::hud_rect(kScreen, 36, [&] {
    auto placement = moved;
    placement.width = 180;
    return placement;
  }());
  expect_int(rect.right, 1920 - 316, "the window follows");
  expect_int(rect.top, 1080 - 240, "and its top too");
}

// However far the mouse goes, the bar stays on the screen to be dragged back.
void a_drag_stops_at_the_edge() {
  const zoal_atc::gui::HudPlacement start;
  const auto far_left = zoal_atc::gui::hud_dragged(start, -5000, 0, kScreen, 180);
  expect_int(far_left.margin_right, 1920 - 180, "stops at the left edge");
  const auto far_right = zoal_atc::gui::hud_dragged(start, 5000, 5000, kScreen, 180);
  expect_int(far_right.margin_right, 0, "stops at the right edge");
  expect_int(far_right.margin_top, 0, "stops at the top");
  const auto far_down = zoal_atc::gui::hud_dragged(start, 0, -5000, kScreen, 180);
  expect_int(far_down.margin_top, 1080 - 36, "the bar stays above the bottom");
}

} // namespace

int main() {
  a_drag_moves_the_corner();
  a_drag_stops_at_the_edge();
  pins_to_the_top_right();
  an_empty_hud_is_only_its_handle();
  never_runs_off_the_screen();
  follows_an_offset_desktop();
  narrows_to_its_content();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
