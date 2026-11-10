#include "zoal_atc/gui/hud_session.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
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

using namespace zoal_atc::gui;
using zoal_atc::transport::json_raw_field;
using zoal_atc::transport::json_string_field;
using zoal_atc::transport::json_uint_field;

// 2026-11-07T12:00:00Z.
constexpr std::int64_t kWall = 1'794'052'800'000;
constexpr const char *kWallIso = "2026-11-07T12:00:00Z";

std::string gui_event(const std::string &name, const std::string &payload) {
  return R"({"type":"gui_event","event":")" + name + R"(","payload":)" +
         payload + "}";
}

std::string gui_ok(std::uint64_t id, const std::string &payload = "{}") {
  return R"({"type":"gui_response","request_id":)" + std::to_string(id) +
         R"(,"ok":true,"payload":)" + payload + "}";
}

std::string gui_refused(std::uint64_t id, const std::string &error) {
  return R"({"type":"gui_response","request_id":)" + std::to_string(id) +
         R"(,"ok":false,"error":")" + error + R"("})";
}

std::string atc_line(const std::string &text) {
  return R"({"kind":"atc","text":")" + text + R"(","at":")" + kWallIso +
         R"(","spokenBy":"tower"})";
}

// A bridge whose plugin answers the HUD's local actions the way
// xplane_plugin.cpp does, recording what it was asked.
struct Rig {
  std::vector<std::string> local_actions;
  std::string theme = "graphite";
  GuiBridge bridge;
  HudSession hud{bridge};

  Rig() {
    bridge.set_local_action(
        [this](const std::string &action,
               const std::string &) -> std::optional<LocalAnswer> {
          if (action == "settings") {
            local_actions.push_back(action);
            return LocalAnswer{true, R"({"theme":")" + theme + R"("})", ""};
          }
          if (action == "open_panel") {
            local_actions.push_back(action);
            return LocalAnswer{true, R"({"opened":true})", ""};
          }
          return std::nullopt;
        });
  }

  // The frames the bridge has for the console, as {action or type, id}.
  struct Sent {
    std::string what;
    std::uint64_t id = 0;
    std::string frame;
  };
  std::vector<Sent> sent() {
    std::vector<Sent> out;
    for (const std::string &frame : bridge.take_socket_frames()) {
      Sent item;
      item.frame = frame;
      item.what = json_string_field(frame, "action")
                      .value_or(json_string_field(frame, "type").value_or(""));
      item.id = json_uint_field(frame, "request_id").value_or(0);
      out.push_back(item);
    }
    return out;
  }

  // Connects, attaches the HUD and has the console accept its subscription.
  void bring_up() {
    bridge.set_connected(true, 0);
    hud.attach(0);
    for (const Sent &item : sent()) {
      if (item.what == "gui_subscribe") {
        bridge.on_socket_frame(gui_ok(item.id), 0);
      }
    }
    hud.pump(0, kWall);
  }

  std::uint64_t last_request(const std::string &action) {
    std::uint64_t id = 0;
    for (const Sent &item : sent()) {
      if (item.what == action) {
        id = item.id;
      }
    }
    return id;
  }
};

void says_the_console_is_reachable_once_subscribed() {
  Rig rig;
  rig.bridge.set_connected(true, 0);
  rig.hud.attach(0);
  rig.hud.pump(0, kWall);
  expect_true(!rig.hud.reachable(), "connected is not yet listening");

  for (const auto &item : rig.sent()) {
    if (item.what == "gui_subscribe") {
      rig.bridge.on_socket_frame(gui_ok(item.id), 0);
    }
  }
  rig.hud.pump(0, kWall);
  expect_true(rig.hud.reachable(), "subscribed is reachable");

  rig.bridge.set_connected(false, 10);
  rig.hud.pump(10, kWall);
  expect_true(!rig.hud.reachable(), "a dropped socket is unreachable");
}

void shows_what_atc_just_said() {
  Rig rig;
  rig.bring_up();
  rig.bridge.on_socket_frame(gui_event("comm_log", atc_line("Cleared to land")),
                             0);
  rig.hud.pump(0, kWall);
  expect_true(rig.hud.feed().lines().size() == 1, "one line");
  if (!rig.hud.feed().empty()) {
    const HudEntry &entry = rig.hud.feed().lines().front().entry;
    expect_str(entry.text, "Cleared to land", "its words");
    expect_str(entry.kind, "atc", "the entry's own kind, not the envelope's");
    expect_str(entry.spoken_by, "tower", "the seat");
  }
}

// The bridge replays its cache to a view that attaches: the last line the
// console sent is shown if it is still news.
void picks_up_the_cached_line_on_attach() {
  Rig rig;
  rig.bridge.set_connected(true, 0);
  rig.bridge.on_socket_frame(gui_event("comm_log", atc_line("Hold short")), 0);
  rig.hud.attach(0);
  rig.hud.pump(0, kWall);
  expect_true(rig.hud.feed().lines().size() == 1, "replayed line shown");

  // Detaching and attaching again replays it a second time; it is one line.
  rig.hud.detach();
  rig.hud.attach(1);
  rig.hud.pump(1, kWall + 1'000);
  expect_true(rig.hud.feed().lines().size() == 1, "a replay is not news");
}

void reads_the_callsign_from_the_flight_snapshot() {
  Rig rig;
  rig.bring_up();
  rig.bridge.on_socket_frame(
      gui_event("flight_snapshot",
                R"({"hasSession":true,"callsign":"N123AB","lifecycle":"taxi"})"),
      0);
  rig.hud.pump(0, kWall);
  expect_str(rig.hud.callsign(), "N123AB", "callsign");
}

void wears_the_theme_the_plugin_names() {
  Rig rig;
  rig.theme = "night";
  rig.bring_up();
  expect_true(rig.local_actions.size() == 1 &&
                  rig.local_actions.front() == "settings",
              "asked for the settings on attach");
  expect_true(rig.hud.theme() == HudTheme::night, "night from the answer");

  rig.hud.on_settings_changed(R"({"theme":"daylight"})");
  expect_true(rig.hud.theme() == HudTheme::daylight, "changed in the panel");
  rig.hud.on_settings_changed(R"({"simbriefPilotId":"123"})");
  expect_true(rig.hud.theme() == HudTheme::daylight,
              "a save that names no theme leaves it");
  rig.hud.on_settings_changed(R"({"theme":"neon"})");
  expect_true(rig.hud.theme() == HudTheme::graphite, "unknown is the default");
}

// The plugin attaches the HUD while it is still starting and installs the
// handler that answers "settings" afterwards (xplane_plugin.cpp).
void asks_for_the_theme_once_the_plugin_is_listening() {
  GuiBridge bridge;
  HudSession hud(bridge);
  hud.attach(0);
  bridge.set_local_action(
      [](const std::string &action,
         const std::string &) -> std::optional<LocalAnswer> {
        if (action == "settings") {
          return LocalAnswer{true, R"({"theme":"night"})", ""};
        }
        return std::nullopt;
      });
  hud.pump(0, kWall);
  hud.pump(1, kWall);
  expect_true(hud.theme() == HudTheme::night, "answered after start-up");
}

void reloads_the_flight_plan_through_the_console() {
  Rig rig;
  rig.bring_up();
  rig.hud.toggle_menu();
  rig.hud.press_reload(5, kWall);
  const auto sent = rig.sent();
  expect_true(sent.size() == 1 && sent.front().what == "refresh_flight_plan",
              "one refresh_flight_plan request");
  if (sent.empty()) {
    return;
  }
  expect_str(json_raw_field(sent.front().frame, "payload").value_or(""),
             R"({"pilotId":""})", "the plugin's own pilot ID");
  expect_true(rig.hud.menu().busy(), "busy while it runs");

  rig.bridge.on_socket_frame(gui_ok(sent.front().id, R"({"hasPlan":true})"), 6);
  rig.hud.pump(6, kWall + 300);
  expect_true(!rig.hud.menu().busy(), "settled");
  expect_true(rig.hud.menu().done() == HudAction::reload_plan, "Plan reloaded");
}

void shows_the_consoles_refusal() {
  Rig rig;
  rig.bring_up();
  rig.hud.toggle_menu();
  rig.hud.press_reload(5, kWall);
  const std::uint64_t id = rig.last_request("refresh_flight_plan");
  rig.bridge.on_socket_frame(gui_refused(id, "no SimBrief pilot ID"), 6);
  rig.hud.pump(6, kWall);
  expect_str(rig.hud.menu().error(), "no SimBrief pilot ID", "its words");
}

void says_so_when_the_console_is_down() {
  Rig rig;
  rig.hud.attach(0);
  rig.hud.toggle_menu();
  rig.hud.press_reload(5, kWall);
  expect_str(rig.hud.menu().error(), "console disconnected",
             "refused at once, not after a timeout");
  expect_true(!rig.hud.menu().busy(), "not left busy");
}

void a_request_the_console_never_answers_times_out() {
  Rig rig;
  rig.bring_up();
  rig.hud.toggle_menu();
  rig.hud.press_reload(5, kWall);
  rig.bridge.tick(5 + 60'000);
  rig.hud.pump(5 + 60'000, kWall + 60'000);
  expect_str(rig.hud.menu().error(), "request timed out", "timed out");
  expect_true(!rig.hud.menu().busy(), "no longer busy");
}

void starts_a_new_flight_on_the_second_press() {
  Rig rig;
  rig.bring_up();
  rig.bridge.on_socket_frame(gui_event("comm_log", atc_line("Taxi via A")), 0);
  rig.hud.pump(0, kWall);
  rig.hud.toggle_menu();

  rig.hud.press_new_flight(5, kWall);
  expect_true(rig.sent().empty(), "the first press only arms");
  rig.hud.press_new_flight(6, kWall + 1'000);
  const std::uint64_t id = rig.last_request("end_flight_session");
  expect_true(id != 0, "the second ends the session");

  rig.bridge.on_socket_frame(gui_ok(id), 7);
  rig.hud.pump(7, kWall + 1'200);
  expect_true(rig.hud.menu().done() == HudAction::end_flight, "Started");
  expect_true(rig.hud.feed().empty(), "the last flight's lines are gone");
}

void opens_the_panel_without_the_console() {
  Rig rig;
  rig.hud.attach(0);
  rig.hud.pump(0, kWall);
  rig.hud.toggle_menu();
  rig.hud.press_open_panel(5);
  expect_true(rig.local_actions.size() == 2 &&
                  rig.local_actions.back() == "open_panel",
              "the plugin opened the panel");
  rig.hud.pump(5, kWall);
  expect_true(!rig.hud.menu().busy(), "answered locally");
  expect_true(rig.hud.menu().error().empty(), "no error");
}

void an_answer_for_someone_else_is_ignored() {
  Rig rig;
  rig.bring_up();
  rig.hud.toggle_menu();
  rig.hud.press_reload(5, kWall);
  // The panel's request, answered: nothing of the HUD's.
  rig.bridge.on_socket_frame(gui_ok(999), 6);
  rig.hud.pump(6, kWall);
  expect_true(rig.hud.menu().busy(), "still waiting for its own");
}

void reads_a_comm_log_entry() {
  const auto entry = parse_hud_entry(
      R"({"kind":"pilot","sessionId":"s1","overheard":true,"addressedTo":"N1",)"
      R"("text":"Roger \"wilco\"","at":"2026-11-07T12:00:00Z","spokenBy":"DAL42"})");
  expect_true(entry.has_value(), "parsed");
  if (entry) {
    expect_str(entry->kind, "pilot", "kind");
    expect_str(entry->text, "Roger \"wilco\"", "escaped text");
    expect_str(entry->addressed_to, "N1", "addressed to");
    expect_str(entry->spoken_by, "DAL42", "spoken by");
    expect_true(entry->overheard, "overheard");
  }
  expect_true(!parse_hud_entry(R"({"kind":"atc","text":""})").has_value(),
              "nothing to show");
  expect_true(!parse_hud_entry("null").has_value(), "null payload");
}

} // namespace

int main() {
  says_the_console_is_reachable_once_subscribed();
  shows_what_atc_just_said();
  picks_up_the_cached_line_on_attach();
  reads_the_callsign_from_the_flight_snapshot();
  wears_the_theme_the_plugin_names();
  asks_for_the_theme_once_the_plugin_is_listening();
  reloads_the_flight_plan_through_the_console();
  shows_the_consoles_refusal();
  says_so_when_the_console_is_down();
  a_request_the_console_never_answers_times_out();
  starts_a_new_flight_on_the_second_press();
  opens_the_panel_without_the_console();
  an_answer_for_someone_else_is_ignored();
  reads_a_comm_log_entry();

  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "hud session: all checks passed\n";
  return EXIT_SUCCESS;
}
