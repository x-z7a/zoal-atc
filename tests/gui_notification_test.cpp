#include "zoal_atc/gui/notification.hpp"

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
  std::cerr << label << ": got [" << got << "], want [" << want << "]\n";
  ++g_failures;
}

void expect_true(bool condition, const char *label) {
  if (condition) {
    return;
  }
  std::cerr << label << ": expected true\n";
  ++g_failures;
}

void expect_false(bool condition, const char *label) {
  if (!condition) {
    return;
  }
  std::cerr << label << ": expected false\n";
  ++g_failures;
}

void expect_near(double got, double want, const char *label) {
  const double delta = got > want ? got - want : want - got;
  if (delta < 0.0001) {
    return;
  }
  std::cerr << label << ": got " << got << ", want " << want << "\n";
  ++g_failures;
}

using zoal_atc::gui::GuiEvent;
using zoal_atc::gui::NotificationController;
using zoal_atc::gui::NotificationPreferences;
using zoal_atc::gui::notification_for;
using zoal_atc::gui::parse_notification_control;

GuiEvent atc_reply(const std::string &text) {
  GuiEvent event;
  event.event = "atc_reply";
  event.payload_json = "{\"text\":\"" + text + "\",\"category\":\"instruction\"}";
  return event;
}

GuiEvent atc_reply_from(const std::string &position, const std::string &text) {
  GuiEvent event;
  event.event = "atc_reply";
  event.payload_json = "{\"text\":\"" + text + "\",\"spokenBy\":\"" + position + "\"}";
  return event;
}

void test_parse_control() {
  const auto control = parse_notification_control(
      "{\"type\":\"notification_control\",\"enabled\":false,\"corner\":\"bottom_"
      "right\",\"timeout_secs\":12,\"sound\":false}");
  expect_true(control.valid, "a control frame parses");
  expect_false(control.prefs.enabled, "enabled is honoured");
  expect_eq(control.prefs.corner, "bottom_right", "corner is honoured");
  expect_near(control.prefs.timeout_secs, 12.0, "timeout is honoured");
  expect_false(control.prefs.sound, "sound is honoured");
}

void test_other_frames_are_not_control_frames() {
  expect_false(parse_notification_control(
                   "{\"type\":\"telemetry_control\",\"interval_secs\":1}")
                   .valid,
               "another frame type is not a notification command");
  expect_false(parse_notification_control("not json at all").valid,
               "malformed input is not a notification command");
}

void test_absent_fields_keep_defaults() {
  const auto control =
      parse_notification_control("{\"type\":\"notification_control\"}");
  expect_true(control.valid, "a bare control frame is still a command");
  expect_true(control.prefs.enabled, "enabled defaults on");
  expect_eq(control.prefs.corner, "top_right", "corner keeps its default");
  expect_true(control.prefs.sound, "sound defaults on");
}

// Over a hidden browser nothing on the toast can be clicked, so a notification
// that waits to be dismissed would never leave.
void test_timeout_cannot_be_forever() {
  const auto zero = parse_notification_control(
      "{\"type\":\"notification_control\",\"timeout_secs\":0}");
  expect_true(zero.prefs.timeout_secs >= 2.0,
              "a zero timeout is clamped to something that expires");

  const auto huge = parse_notification_control(
      "{\"type\":\"notification_control\",\"timeout_secs\":9000}");
  expect_true(huge.prefs.timeout_secs <= 30.0,
              "an absurd timeout is clamped");
}

// A corner the plugin does not understand is a console it does not understand,
// not a reason to put the toast somewhere arbitrary.
void test_unknown_corner_is_ignored() {
  const auto control = parse_notification_control(
      "{\"type\":\"notification_control\",\"corner\":\"middle\"}");
  expect_eq(control.prefs.corner, "top_right", "an unknown corner is refused");
}

void test_controller_falls_back_to_local() {
  NotificationController controller;
  expect_true(controller.preferences().enabled,
              "with no command the local default governs");

  controller.apply(parse_notification_control(
      "{\"type\":\"notification_control\",\"enabled\":false}"));
  expect_false(controller.preferences().enabled, "a command governs once given");

  controller.apply(parse_notification_control("{\"type\":\"telemetry_control\"}"));
  expect_false(controller.preferences().enabled,
               "an invalid command does not overwrite a valid one");

  controller.reset();
  expect_true(controller.preferences().enabled,
              "losing the console reverts to the local default");
}

void test_raises_for_a_transmission_nobody_can_see() {
  const auto request = notification_for(atc_reply("taxi to runway 07 via alpha"),
                                        NotificationPreferences{}, false);
  expect_true(request.raise, "a hidden panel raises a toast");
  expect_eq(request.title, "ATC", "the title says who spoke");
  expect_eq(request.body, "taxi to runway 07 via alpha",
            "the body is what was said");
}

// The whole point: notify about what the pilot cannot already see. A visible
// panel is showing the radio log.
// Ground and Tower are different people to a pilot, and which one called is the
// first thing they need from a toast.
void test_the_title_names_the_seat() {
  expect_eq(notification_for(atc_reply_from("ground", "taxi via alpha"),
                             NotificationPreferences{}, false)
                .title,
            "Ground", "ground is named");
  expect_eq(notification_for(atc_reply_from("clearance_delivery", "cleared to CYGK"),
                             NotificationPreferences{}, false)
                .title,
            "Clearance Delivery", "delivery is named in full");
  expect_eq(notification_for(atc_reply_from("center", "climb and maintain"),
                             NotificationPreferences{}, false)
                .title,
            "Center", "center is named");
}

// A console that does not send a position, or sends one this plugin predates,
// still produces a toast that says something true.
void test_an_unknown_seat_is_still_atc() {
  expect_eq(notification_for(atc_reply("taxi via alpha"), NotificationPreferences{}, false)
                .title,
            "ATC", "no position falls back to ATC");
  expect_eq(notification_for(atc_reply_from("flight_service_station", "advisory"),
                             NotificationPreferences{}, false)
                .title,
            "ATC", "an unrecognised position falls back to ATC");
}

void test_does_not_repeat_what_is_on_screen() {
  const auto request = notification_for(atc_reply("taxi via alpha"),
                                        NotificationPreferences{}, true);
  expect_false(request.raise, "a visible panel raises nothing");
}

void test_respects_the_pilots_choice() {
  NotificationPreferences off;
  off.enabled = false;
  expect_false(notification_for(atc_reply("taxi via alpha"), off, false).raise,
               "turned off means silent");
}

void test_only_transmissions() {
  GuiEvent telemetry;
  telemetry.event = "telemetry";
  telemetry.payload_json = "{\"altitudeFtMsl\":3000}";
  expect_false(
      notification_for(telemetry, NotificationPreferences{}, false).raise,
      "telemetry is not something ATC said");
}

void test_an_empty_transmission_raises_nothing() {
  GuiEvent empty;
  empty.event = "atc_reply";
  empty.payload_json = "{\"text\":\"\"}";
  expect_false(notification_for(empty, NotificationPreferences{}, false).raise,
               "an empty toast is worse than none");

  GuiEvent textless;
  textless.event = "atc_reply";
  textless.payload_json = "{\"category\":\"instruction\"}";
  expect_false(
      notification_for(textless, NotificationPreferences{}, false).raise,
      "a payload with no text raises nothing");
}

void test_a_long_clearance_is_truncated_not_dropped() {
  const std::string clearance(400, 'x');
  const auto request =
      notification_for(atc_reply(clearance), NotificationPreferences{}, false);
  expect_true(request.raise, "a long clearance still notifies");
  expect_true(request.body.size() < clearance.size(),
              "a long clearance is shortened to fit");
}

} // namespace

int main() {
  test_parse_control();
  test_other_frames_are_not_control_frames();
  test_absent_fields_keep_defaults();
  test_timeout_cannot_be_forever();
  test_unknown_corner_is_ignored();
  test_controller_falls_back_to_local();
  test_raises_for_a_transmission_nobody_can_see();
  test_the_title_names_the_seat();
  test_an_unknown_seat_is_still_atc();
  test_does_not_repeat_what_is_on_screen();
  test_respects_the_pilots_choice();
  test_only_transmissions();
  test_an_empty_transmission_raises_nothing();
  test_a_long_clearance_is_truncated_not_dropped();

  if (g_failures > 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
