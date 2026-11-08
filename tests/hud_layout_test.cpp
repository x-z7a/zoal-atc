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

void reads_the_page_report() {
  const auto height = zoal_atc::gui::parse_hud_layout(R"({"height":212.4})");
  expect_true(height.has_value() && *height == 213, "rounds up to whole boxels");
  expect_true(!zoal_atc::gui::parse_hud_layout(R"({"height":0})").has_value(),
              "zero is not a height");
  expect_true(!zoal_atc::gui::parse_hud_layout(R"({"height":-5})").has_value(),
              "negative is not a height");
  expect_true(!zoal_atc::gui::parse_hud_layout("not json").has_value(),
              "garbage is ignored");
}

// The feed hangs under the bar, so the bar's window never has to change size
// for a line to come or go.
void the_feed_hangs_under_the_bar() {
  const auto bar = zoal_atc::gui::hud_rect(kScreen, 38);
  const auto feed = zoal_atc::gui::hud_feed_rect(kScreen, bar, 312);
  expect_int(feed.left, bar.left, "same left edge");
  expect_int(feed.right, bar.right, "same right edge");
  expect_int(feed.top, bar.bottom - 6, "a gap below the bar");
  expect_int(feed.top - feed.bottom, 312, "as tall as asked");
}

// Empty, it is a strip that takes no clicks; it never runs off the screen.
void the_feed_collapses_and_stays_on_screen() {
  const auto bar = zoal_atc::gui::hud_rect(kScreen, 38);
  const auto empty = zoal_atc::gui::hud_feed_rect(kScreen, bar, 1);
  expect_int(empty.top - empty.bottom, 1, "a one-boxel strip");
  const auto huge = zoal_atc::gui::hud_feed_rect(kScreen, bar, 5000);
  expect_true(huge.bottom >= kScreen.bottom, "stays on screen");
}

} // namespace

int main() {
  pins_to_the_top_right();
  an_empty_hud_is_only_its_handle();
  never_runs_off_the_screen();
  follows_an_offset_desktop();
  reads_the_page_report();
  the_feed_hangs_under_the_bar();
  the_feed_collapses_and_stays_on_screen();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
