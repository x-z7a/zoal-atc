#include "zoal_atc/gui/hud_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect_true(bool condition, const char *label) {
  if (condition) {
    return;
  }
  std::cerr << label << ": expected true\n";
  ++g_failures;
}

void expect_str(const std::string &got, const std::string &want,
                const char *label) {
  if (got == want) {
    return;
  }
  std::cerr << label << ": got \"" << got << "\", want \"" << want << "\"\n";
  ++g_failures;
}

void expect_i64(std::int64_t got, std::int64_t want, const char *label) {
  if (got == want) {
    return;
  }
  std::cerr << label << ": got " << got << ", want " << want << "\n";
  ++g_failures;
}

using namespace zoal_atc::gui;

// 2026-11-07T12:00:00Z.
constexpr std::int64_t kNow = 1'794'052'800'000;
constexpr const char *kNowIso = "2026-11-07T12:00:00Z";

HudEntry atc(const std::string &text, const std::string &at = kNowIso) {
  HudEntry entry;
  entry.kind = "atc";
  entry.text = text;
  entry.at = at;
  entry.spoken_by = "tower";
  return entry;
}

std::vector<std::string> texts(const HudFeed &feed) {
  std::vector<std::string> out;
  for (const HudLine &line : feed.lines()) {
    out.push_back(line.entry.text);
  }
  return out;
}

std::string joined(const std::vector<std::string> &parts) {
  std::string out;
  for (const std::string &part : parts) {
    out += (out.empty() ? "" : ",") + part;
  }
  return out;
}

// --- the feed (domain/hud.test.ts, hud/HudFeedApp.test.tsx) -----------------

void shows_a_transmission_the_moment_it_arrives() {
  HudFeed feed;
  feed.add(atc("Cleared for takeoff"), kNow);
  expect_str(joined(texts(feed)), "Cleared for takeoff", "shown at once");
}

void keeps_only_the_newest_three() {
  HudFeed feed;
  // Distinct stamps: the same words at the same instant would be a replay.
  feed.add(atc("one", "2026-11-07T11:59:57Z"), kNow);
  feed.add(atc("two", "2026-11-07T11:59:58Z"), kNow);
  feed.add(atc("three", "2026-11-07T11:59:59Z"), kNow);
  feed.add(atc("four", kNowIso), kNow);
  expect_str(joined(texts(feed)), "two,three,four", "newest three");
}

void lets_each_line_go_a_minute_after_it_came_in() {
  HudFeed feed;
  feed.add(atc("Contact departure"), kNow);
  feed.expire(kNow + kHudLifespanMs - 1);
  expect_true(feed.lines().size() == 1, "still up just before a minute");
  feed.expire(kNow + kHudLifespanMs);
  expect_true(feed.empty(), "gone at a minute");
}

// The lifespan runs from when the HUD showed the line, not from the console's
// stamp: a skewed console clock must not cut a line short.
void times_a_line_from_when_it_was_shown() {
  HudFeed feed;
  feed.add(atc("Squawk 4521", "2026-11-07T11:59:30Z"), kNow);
  feed.expire(kNow + kHudLifespanMs - 1);
  expect_true(feed.lines().size() == 1, "a full minute from arrival");
}

void fades_a_line_out_over_its_last_seconds() {
  HudFeed feed;
  feed.add(atc("Squawk 4521"), kNow);
  const HudLine &line = feed.lines().front();
  expect_true(!hud_line_fading(line, kNow + 10'000), "not fading early");
  expect_true(hud_line_fading(line, kNow + kHudLifespanMs - 1'000),
              "fading at the end");
  expect_true(hud_line_opacity(line, kNow + 10'000) == 1.0f, "fully opaque");
  const float mid = hud_line_opacity(line, kNow + kHudLifespanMs - 2'500);
  expect_true(mid > 0.49f && mid < 0.51f, "half way through the fade");
  expect_true(hud_line_opacity(line, kNow + kHudLifespanMs) == 0.0f,
              "gone at the end");
}

// The plugin replays its newest cached line to every view that attaches. That
// line may be from an hour ago, and it is not what ATC just said.
void ignores_a_replayed_line_that_was_said_long_before() {
  HudFeed feed;
  feed.add(atc("Taxi to runway 07", "2026-11-07T11:50:00Z"), kNow);
  expect_true(feed.empty(), "stale line not shown");
}

void allows_for_the_console_clock_running_behind() {
  HudFeed feed;
  // Said 80 s ago by the console's clock: within the minute plus its slack.
  feed.add(atc("Hold short", "2026-11-07T11:58:40Z"), kNow);
  expect_true(feed.lines().size() == 1, "within the skew allowance");
}

// The replay of a line already shown -- or already shown and gone -- is not a
// second transmission.
void does_not_show_a_replay_twice() {
  HudFeed feed;
  feed.add(atc("Cleared to land"), kNow);
  feed.add(atc("Cleared to land"), kNow + 1'000);
  expect_true(feed.lines().size() == 1, "replay while shown");

  feed.expire(kNow + kHudLifespanMs);
  feed.add(atc("Cleared to land"), kNow + kHudLifespanMs + 1'000);
  expect_true(feed.empty(), "replay after it left");
}

void clears_with_a_new_flight() {
  HudFeed feed;
  feed.add(atc("one"), kNow);
  feed.clear();
  expect_true(feed.empty(), "cleared");
}

void shows_an_unstamped_line_since_nothing_says_it_is_old() {
  HudFeed feed;
  HudEntry entry;
  entry.kind = "pilot";
  entry.text = "Ready for departure";
  feed.add(entry, kNow);
  // Two unstamped lines with the same words may be two transmissions.
  feed.add(entry, kNow + 1'000);
  expect_true(feed.lines().size() == 2, "unstamped lines all shown");
}

// --- labels (components/CommLog.tsx, domain/format.ts) -----------------------

void labels_each_speaker() {
  HudEntry pilot;
  pilot.kind = "pilot";
  expect_str(hud_speaker(pilot, "N123AB"), "N123AB", "own callsign");
  expect_str(hud_speaker(pilot, ""), "You", "no callsign yet");

  HudEntry overheard = pilot;
  overheard.overheard = true;
  overheard.spoken_by = "DAL42";
  expect_str(hud_speaker(overheard, "N123AB"), "DAL42", "another aircraft");
  overheard.spoken_by.clear();
  expect_str(hud_speaker(overheard, "N123AB"), "Aircraft", "unnamed aircraft");

  HudEntry controller = atc("x");
  expect_str(hud_speaker(controller, "N123AB"), "Tower", "a position");
  controller.spoken_by = "clearance_delivery";
  expect_str(hud_speaker(controller, ""), "Clearance Delivery", "long name");
  controller.spoken_by = "none";
  expect_str(hud_speaker(controller, ""), "ATC", "no seat");
  controller.spoken_by = "something_new";
  expect_str(hud_speaker(controller, ""), "ATC", "unknown seat");
}

void colours_each_seat() {
  HudEntry entry = atc("x");
  expect_true(hud_tone(entry) == HudTone::tower, "tower");
  entry.spoken_by = "ground";
  expect_true(hud_tone(entry) == HudTone::surface, "ground is surface");
  entry.spoken_by = "approach";
  expect_true(hud_tone(entry) == HudTone::radar, "approach is radar");
  entry.spoken_by = "unicom_ctaf";
  expect_true(hud_tone(entry) == HudTone::advisory, "unicom is advisory");
  entry.spoken_by = "";
  expect_true(hud_tone(entry) == HudTone::atc, "unknown is plain ATC");
  entry.kind = "pilot";
  expect_true(hud_tone(entry) == HudTone::pilot, "pilot");
}

// --- timestamps ---------------------------------------------------------------

void parses_iso8601() {
  expect_i64(parse_iso8601_ms(kNowIso).value_or(-1), kNow, "UTC");
  expect_i64(parse_iso8601_ms("2026-11-07T12:00:00.250Z").value_or(-1),
             kNow + 250, "milliseconds");
  expect_i64(parse_iso8601_ms("2026-11-07T12:00:00.123456789Z").value_or(-1),
             kNow + 123, "nanoseconds truncate");
  expect_i64(parse_iso8601_ms("2026-11-07T13:30:00+01:30").value_or(-1),
             kNow, "positive offset");
  expect_i64(parse_iso8601_ms("2026-11-07T07:00:00-05:00").value_or(-1),
             kNow, "negative offset");
  expect_i64(parse_iso8601_ms("1970-01-01T00:00:00Z").value_or(-1), 0,
             "epoch");
  expect_i64(parse_iso8601_ms("2024-02-29T00:00:00Z").value_or(-1),
             1'709'164'800'000, "leap day");
  for (const char *bad : {"", "2026-11-07", "2026-13-01T00:00:00Z",
                          "2026-11-07T12:00:00Q", "yesterday",
                          "2026-11-07T12:00:00Zjunk"}) {
    expect_true(!parse_iso8601_ms(bad).has_value(), bad);
  }
}

// --- the menu (hud/HudApp.test.tsx) ------------------------------------------

HudMenu ready_open_menu() {
  HudMenu menu;
  menu.set_ready(true);
  menu.toggle();
  return menu;
}

void reloads_the_flight_plan() {
  HudMenu menu = ready_open_menu();
  expect_true(menu.press_reload(kNow) == HudAction::reload_plan, "runs reload");
  expect_true(menu.busy(), "busy while it runs");
  expect_true(!menu.reload_enabled(), "no second press while busy");
  menu.action_succeeded(HudAction::reload_plan, kNow + 200);
  expect_true(menu.done() == HudAction::reload_plan, "shows Plan reloaded");
  menu.tick(kNow + 200 + kHudDoneMs);
  expect_true(menu.done() == HudAction::none, "label lapses");
}

// It throws the clearance away, so one press only arms it.
void asks_before_starting_a_new_flight() {
  HudMenu menu = ready_open_menu();
  expect_true(menu.press_new_flight(kNow) == HudAction::none, "first arms");
  expect_true(menu.armed(), "armed");
  expect_true(menu.press_new_flight(kNow + 1'000) == HudAction::end_flight,
              "second confirms");
  expect_true(!menu.armed(), "disarmed once confirmed");
  menu.action_succeeded(HudAction::end_flight, kNow + 1'200);
  expect_true(menu.done() == HudAction::end_flight, "shows Started");
}

void an_armed_new_flight_lapses() {
  HudMenu menu = ready_open_menu();
  menu.press_new_flight(kNow);
  menu.tick(kNow + kHudArmedMs);
  expect_true(!menu.armed(), "lapsed");
  expect_true(menu.press_new_flight(kNow + kHudArmedMs + 1) == HudAction::none,
              "a late second press only arms again");
}

void opens_the_full_panel() {
  HudMenu menu = ready_open_menu();
  expect_true(menu.press_open_panel() == HudAction::open_panel, "open panel");
}

void shows_a_refusal_rather_than_pretending() {
  HudMenu menu = ready_open_menu();
  menu.press_reload(kNow);
  menu.action_failed(HudAction::reload_plan, "no SimBrief pilot ID");
  expect_str(menu.error(), "no SimBrief pilot ID", "the console's words");
  expect_true(!menu.busy(), "no longer busy");
  expect_true(menu.done() == HudAction::none, "no done label");
  menu.dismiss_error();
  expect_true(menu.error().empty(), "dismissed");

  menu.press_reload(kNow);
  menu.action_failed(HudAction::reload_plan, "");
  expect_true(!menu.error().empty(), "an empty refusal still says something");
}

void a_new_press_clears_the_last_refusal() {
  HudMenu menu = ready_open_menu();
  menu.press_reload(kNow);
  menu.action_failed(HudAction::reload_plan, "timeout");
  menu.press_reload(kNow + 1'000);
  expect_true(menu.error().empty(), "cleared on the next try");
}

void nothing_reaches_the_console_until_it_is_ready() {
  HudMenu menu;
  menu.toggle();
  expect_true(menu.press_reload(kNow) == HudAction::none, "reload disabled");
  expect_true(menu.press_new_flight(kNow) == HudAction::none,
              "new flight disabled");
  expect_true(!menu.armed(), "not even armed");
  expect_true(menu.press_open_panel() == HudAction::none, "panel disabled");
}

void closing_the_menu_forgets_what_it_was_doing() {
  HudMenu menu = ready_open_menu();
  menu.press_new_flight(kNow);
  menu.toggle();
  expect_true(!menu.open(), "closed");
  expect_true(!menu.armed(), "disarmed");

  menu.toggle();
  menu.press_reload(kNow);
  menu.action_failed(HudAction::reload_plan, "timeout");
  menu.toggle();
  expect_true(menu.error().empty(), "error forgotten");
}

} // namespace

int main() {
  shows_a_transmission_the_moment_it_arrives();
  keeps_only_the_newest_three();
  lets_each_line_go_a_minute_after_it_came_in();
  times_a_line_from_when_it_was_shown();
  fades_a_line_out_over_its_last_seconds();
  ignores_a_replayed_line_that_was_said_long_before();
  allows_for_the_console_clock_running_behind();
  does_not_show_a_replay_twice();
  clears_with_a_new_flight();
  shows_an_unstamped_line_since_nothing_says_it_is_old();
  labels_each_speaker();
  colours_each_seat();
  parses_iso8601();
  reloads_the_flight_plan();
  asks_before_starting_a_new_flight();
  an_armed_new_flight_lapses();
  opens_the_full_panel();
  shows_a_refusal_rather_than_pretending();
  a_new_press_clears_the_last_refusal();
  nothing_reaches_the_console_until_it_is_ready();
  closing_the_menu_forgets_what_it_was_doing();

  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "hud model: all checks passed\n";
  return EXIT_SUCCESS;
}
