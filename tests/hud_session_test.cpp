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
  std::vector<std::string> tunes;
  std::string theme = "graphite";
  GuiBridge bridge;
  HudSession hud{bridge};

  Rig() {
    bridge.set_local_action(
        [this](const std::string &action,
               const std::string &payload) -> std::optional<LocalAnswer> {
          if (action == "settings") {
            local_actions.push_back(action);
            return LocalAnswer{true, R"({"theme":")" + theme + R"("})", ""};
          }
          if (action == "open_panel") {
            local_actions.push_back(action);
            return LocalAnswer{true, R"({"opened":true})", ""};
          }
          if (action == "tune_radio") {
            local_actions.push_back(action);
            tunes.push_back(payload);
            return LocalAnswer{true, "{}", ""};
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

// --- calls by button ---------------------------------------------------------

constexpr std::uint64_t kGround = 121'900'000;
constexpr std::uint64_t kTower = 118'800'000;

std::string ground_menu(std::uint64_t revision, bool readback) {
  std::string items =
      R"({"key":"taxi_departure","label":"Taxi for departure","sentence":"Ground, AC21, ready to taxi"})";
  if (readback) {
    items = R"({"key":"readback:abc","label":"Read back","sentence":"taxi via A, AC21","readback":true},)" +
            items;
  }
  return R"({"frequencyHz":121900000,"role":"ground","station":"CYOW_GND","revision":)" +
         std::to_string(revision) +
         R"(,"nextFrequencyHz":118800000,"items":[)" + items + "]}";
}

void asks_the_position_on_a_new_channel_for_its_menu() {
  Rig rig;
  rig.bring_up();
  rig.sent();
  rig.hud.set_radio(1, kGround, 0);
  rig.hud.pump(0, kWall);
  expect_true(rig.last_request("pilot_menu") != 0, "asked on tuning");
  rig.hud.pump(1, kWall);
  expect_true(rig.last_request("pilot_menu") == 0, "asked once per channel");
  rig.hud.set_radio(2, kTower, 2);
  rig.hud.pump(2, kWall);
  const auto requests = rig.sent();
  expect_true(requests.size() == 1 && requests.front().what == "pilot_menu",
              "asked again on retuning");
  if (!requests.empty()) {
    expect_true(requests.front().frame.find(R"("transmit_com":2,"frequency_hz":118800000)") !=
                    std::string::npos,
                "uses the radio the pilot just tuned");
  }
  rig.hud.pump(2'002, kWall + 2'000);
  expect_true(rig.last_request("pilot_menu") != 0, "retries a missing menu");
}

void parses_a_menu() {
  const auto menu = parse_call_menu(ground_menu(4, true));
  expect_true(menu.has_value(), "parsed");
  if (!menu) {
    return;
  }
  expect_true(menu->frequency_hz == kGround, "its channel");
  expect_true(menu->next_frequency_hz == kTower, "the next channel");
  expect_true(menu->items.size() == 2, "two items");
  expect_true(menu->readback() != nullptr, "a readback");
  expect_str(menu->items.at(1).sentence, "Ground, AC21, ready to taxi", "the sentence");
  expect_true(!parse_call_menu(R"({"items":[]})").has_value(), "no channel");
  expect_str(format_mhz(126'575'000), "126.575", "an 8.33 channel");
}

void a_press_is_a_call_spoken_once_the_console_takes_it() {
  Rig rig;
  std::vector<std::string> spoken;
  rig.hud.set_own_call_speaker(
      [&spoken](const std::string &sentence, std::uint64_t) { spoken.push_back(sentence); });
  rig.bring_up();
  rig.hud.set_radio(1, kGround, 0);
  rig.bridge.on_socket_frame(gui_event("pilot_menu", ground_menu(1, false)), 0);
  rig.hud.pump(0, kWall);
  expect_true(rig.hud.calls() != nullptr, "the tuned channel's menu");

  rig.sent();
  rig.hud.press_call("taxi_departure", 1, kWall);
  std::uint64_t id = 0;
  std::string frame;
  for (const auto &item : rig.sent()) {
    if (item.what == "pilot_call") {
      id = item.id;
      frame = item.frame;
    }
  }
  expect_true(id != 0, "a pilot_call went to the console");
  expect_true(frame.find("taxi_departure") != std::string::npos, "with the key");
  expect_true(frame.find(R"("frequency_hz":121900000)") != std::string::npos,
              "on the frequency the button was offered for");
  expect_true(rig.hud.call_in_flight("taxi_departure"), "in flight");
  expect_true(rig.hud.awaiting_own_call(), "ATC waits even before the acceptance arrives");
  rig.hud.press_call("taxi_departure", 1, kWall);
  expect_true(rig.last_request("pilot_call") == 0, "not sent twice");
  expect_true(spoken.empty(), "not spoken before the console took it");

  rig.bridge.on_socket_frame(gui_ok(id), 2);
  rig.hud.pump(2, kWall);
  expect_true(spoken.size() == 1 && spoken.front() == "Ground, AC21, ready to taxi",
              "spoken in the pilot's voice");
  expect_true(!rig.hud.call_in_flight("taxi_departure"), "settled");
  expect_true(!rig.hud.awaiting_own_call(), "the accepted call is now handed to audio");
}

void a_refused_call_is_shown_and_not_spoken() {
  Rig rig;
  std::vector<std::string> spoken;
  rig.hud.set_own_call_speaker(
      [&spoken](const std::string &sentence, std::uint64_t) { spoken.push_back(sentence); });
  rig.bring_up();
  rig.hud.set_radio(1, kGround, 0);
  rig.bridge.on_socket_frame(gui_event("pilot_menu", ground_menu(1, false)), 0);
  rig.hud.pump(0, kWall);
  rig.sent();
  rig.hud.press_call("taxi_departure", 1, kWall);
  const std::uint64_t id = rig.last_request("pilot_call");
  rig.bridge.on_socket_frame(gui_refused(id, "the console could not take your call"), 2);
  rig.hud.pump(2, kWall);
  expect_true(spoken.empty(), "a refused call was never made");
  expect_str(rig.hud.call_error(), "the console could not take your call", "why");
}

void auto_readback_reads_back_what_was_heard() {
  Rig rig;
  rig.bring_up();
  rig.hud.on_settings_changed(R"({"autoReadback":true})");
  rig.hud.set_radio(1, kGround, 0);
  rig.hud.pump(0, kWall);
  rig.sent();

  // Heard first, menu second: the readback is pressed when the menu arrives.
  rig.hud.reply_heard(kGround, 1, kWall);
  expect_true(rig.last_request("pilot_call") == 0, "nothing to read back yet");
  rig.bridge.on_socket_frame(gui_event("pilot_menu", ground_menu(2, true)), 2);
  rig.hud.pump(2, kWall + 1'000);
  expect_true(rig.last_request("pilot_call") != 0, "read back on its own");

  // The same readback is not pressed twice.
  rig.bridge.on_socket_frame(gui_event("pilot_menu", ground_menu(3, true)), 3);
  rig.hud.pump(3, kWall + 2'000);
  rig.hud.reply_heard(kGround, 4, kWall + 2'000);
  expect_true(rig.last_request("pilot_call") == 0, "once");
}

// A controller still queued on the channel is not heard yet, so auto readback
// waits for it rather than reading back on the strength of the transmission
// before it. When the last one finishes, the readback goes.
void auto_readback_waits_for_the_controller_to_finish() {
  Rig rig;
  rig.bring_up();
  bool queued = true;
  rig.hud.set_controller_speech_pending([&queued](std::uint64_t) { return queued; });
  rig.hud.on_settings_changed(R"({"autoReadback":true})");
  rig.hud.set_radio(1, kGround, 0);
  rig.hud.pump(0, kWall);
  rig.sent();

  // The transmission before it played, and the readback arrived, but the
  // instruction it reads back is still waiting its turn.
  rig.hud.reply_heard(kGround, 1, kWall);
  rig.bridge.on_socket_frame(gui_event("pilot_menu", ground_menu(2, true)), 2);
  rig.hud.pump(2, kWall + 1'000);
  expect_true(rig.last_request("pilot_call") == 0, "not while the controller is still to be heard");

  queued = false;
  rig.hud.reply_heard(kGround, 3, kWall + 6'000);
  expect_true(rig.last_request("pilot_call") != 0, "read back once it has played");
}

void auto_readback_off_presses_nothing() {
  Rig rig;
  rig.bring_up();
  rig.hud.set_radio(1, kGround, 0);
  rig.hud.pump(0, kWall);
  rig.bridge.on_socket_frame(gui_event("pilot_menu", ground_menu(2, true)), 1);
  rig.hud.pump(1, kWall);
  rig.sent();
  rig.hud.reply_heard(kGround, 2, kWall);
  expect_true(rig.last_request("pilot_call") == 0, "off by default");
}

// A tower working ground control: Tower's and Ground's menus on one channel
// are shown together, readbacks first, and a press names its role.
void a_shared_channel_shows_both_roles() {
  Rig rig;
  rig.bring_up();
  rig.hud.set_radio(1, kTower, 0);
  rig.bridge.on_socket_frame(
      gui_event("pilot_menu",
                R"({"frequencyHz":118800000,"role":"tower","station":"VVNB_TWR","revision":3,)"
                R"("items":[{"key":"runway_vacated","label":"Clear of runway","sentence":"Tower, AC21, clear of the runway"},)"
                R"({"key":"radio_check","label":"Radio check","sentence":"Tower, AC21, radio check"}]})"),
      0);
  rig.bridge.on_socket_frame(
      gui_event("pilot_menu",
                R"({"frequencyHz":118800000,"role":"ground","station":"VVNB_TWR_GND","revision":1,)"
                R"("items":[{"key":"taxi_gate","label":"Taxi to the gate","sentence":"Ground, AC21, request taxi to the gate"},)"
                R"({"key":"radio_check","label":"Radio check","sentence":"Ground, AC21, radio check"}]})"),
      0);
  rig.hud.pump(0, kWall);
  const HudCallMenu *menu = rig.hud.calls();
  expect_true(menu != nullptr, "a merged menu");
  if (menu == nullptr) {
    return;
  }
  expect_true(menu->items.size() == 3, "both roles' calls, the radio check once");
  expect_str(menu->items.front().key, "taxi_gate", "the role heard from last first");
  expect_true(menu->find("runway_vacated") != nullptr, "and the other role's");

  rig.sent();
  rig.hud.press_call("runway_vacated", 1, kWall);
  std::string frame;
  for (const auto &item : rig.sent()) {
    if (item.what == "pilot_call") {
      frame = item.frame;
    }
  }
  expect_true(frame.find(R"("role":"tower")") != std::string::npos, "the press names its role");
}

void the_next_frequency_tunes_the_selected_radio() {
  Rig rig;
  rig.bring_up();
  rig.hud.set_radio(2, kGround, 0);
  rig.bridge.on_socket_frame(gui_event("pilot_menu", ground_menu(1, false)), 0);
  rig.hud.pump(0, kWall);
  rig.hud.press_next_frequency(1);
  expect_true(rig.tunes.size() == 1, "tuned once");
  if (!rig.tunes.empty()) {
    expect_true(rig.tunes.front().find("118.800") != std::string::npos, "to Tower");
    expect_true(rig.tunes.front().find(R"("com":2)") != std::string::npos, "on COM2");
  }
}

// A controller's line waits for its voice: the reply is queued behind the
// pilot's own call, and words before the voice read as the answer arriving
// before the question.
void a_reply_is_shown_when_its_voice_starts() {
  Rig rig;
  rig.hud.set_holds_until_spoken(true);
  rig.bring_up();
  rig.bridge.on_socket_frame(gui_event("comm_log", atc_line("Cleared to land")), 0);
  rig.hud.pump(0, kWall);
  expect_true(rig.hud.feed().empty(), "held while the pilot's own call plays");
  rig.hud.reply_spoken("Cleared to land", kWall + 3'000);
  rig.hud.pump(1, kWall + 3'000);
  expect_true(rig.hud.feed().lines().size() == 1, "shown as the controller starts");
}

void a_reply_whose_voice_never_starts_is_still_shown() {
  Rig rig;
  rig.hud.set_holds_until_spoken(true);
  rig.bring_up();
  rig.bridge.on_socket_frame(gui_event("comm_log", atc_line("Cleared to land")), 0);
  rig.hud.pump(0, kWall);
  rig.hud.pump(1, kWall + kHudHoldMs);
  expect_true(rig.hud.feed().lines().size() == 1, "not lost");
}

void a_voice_that_starts_before_its_line_shows_the_line_at_once() {
  Rig rig;
  rig.hud.set_holds_until_spoken(true);
  rig.bring_up();
  rig.hud.reply_spoken("Cleared to land", kWall);
  rig.bridge.on_socket_frame(gui_event("comm_log", atc_line("Cleared to land")), 0);
  rig.hud.pump(0, kWall + 500);
  expect_true(rig.hud.feed().lines().size() == 1, "already said");
}

void without_voices_a_reply_is_shown_at_once() {
  Rig rig;
  rig.bring_up();
  rig.bridge.on_socket_frame(gui_event("comm_log", atc_line("Cleared to land")), 0);
  rig.hud.pump(0, kWall);
  expect_true(rig.hud.feed().lines().size() == 1, "nothing to wait for");
}

// A choice opens its options; pressing one sends that option's key, and the
// choice closes.
void a_choice_opens_and_its_option_is_the_call() {
  Rig rig;
  rig.bring_up();
  rig.hud.set_radio(1, kGround, 0);
  rig.bridge.on_socket_frame(
      gui_event("pilot_menu",
                R"({"frequencyHz":121900000,"role":"center","station":"CZUL","revision":1,)"
                R"("items":[{"key":"level","label":"Altitude","options":[)"
                R"({"key":"level:27000","label":"FL270","sentence":"Montreal, AC21, request FL270"},)"
                R"({"key":"level:25000","label":"FL250","sentence":"Montreal, AC21, request FL250"}]}]})"),
      0);
  rig.hud.pump(0, kWall);
  const HudCallMenu *menu = rig.hud.calls();
  expect_true(menu != nullptr && menu->items.size() == 1, "one item");
  if (menu == nullptr || menu->items.empty()) {
    return;
  }
  expect_true(menu->items.front().options.size() == 2, "its two options");

  rig.sent();
  rig.hud.press_call("level", 1, kWall);
  expect_true(rig.last_request("pilot_call") == 0, "a choice is not a call");
  expect_true(rig.hud.open_choice() != nullptr, "it opens");

  rig.hud.press_call("level:25000", 2, kWall);
  std::string frame;
  for (const auto &item : rig.sent()) {
    if (item.what == "pilot_call") {
      frame = item.frame;
    }
  }
  expect_true(frame.find("level:25000") != std::string::npos, "the option's key is the call");
  expect_true(rig.hud.open_choice() == nullptr, "and the choice closes");
}

// A call typed in the HUD goes as a typed turn, spoken in the pilot's voice
// once the console takes it.
void a_typed_call_is_sent_and_spoken() {
  Rig rig;
  std::vector<std::string> spoken;
  rig.hud.set_own_call_speaker(
      [&spoken](const std::string &sentence, std::uint64_t) { spoken.push_back(sentence); });
  rig.bring_up();
  rig.sent();
  rig.hud.submit_text("  request higher  ", 1);
  std::uint64_t id = 0;
  std::string frame;
  for (const auto &item : rig.sent()) {
    if (item.what == "submit_text") {
      id = item.id;
      frame = item.frame;
    }
  }
  expect_true(id != 0, "a submit_text went to the console");
  expect_true(frame.find(R"("text":"request higher")") != std::string::npos, "trimmed");
  rig.bridge.on_socket_frame(gui_ok(id), 2);
  rig.hud.pump(2, kWall);
  expect_true(spoken.size() == 1 && spoken.front() == "request higher", "spoken in the pilot's voice");

  rig.hud.submit_text("   ", 3);
  expect_true(rig.last_request("submit_text") == 0, "nothing to send is not sent");
}

void the_transcript_is_a_setting() {
  Rig rig;
  expect_true(rig.hud.shows_transcript(), "shown by default");
  rig.hud.on_settings_changed(R"({"hudTranscript":false})");
  expect_true(!rig.hud.shows_transcript(), "hidden when turned off");
}

void the_typing_line_is_a_setting() {
  Rig rig;
  expect_true(rig.hud.shows_typing(), "shown by default");
  rig.hud.on_settings_changed(R"({"hudTyping":false})");
  expect_true(!rig.hud.shows_typing(), "hidden when turned off");
  rig.hud.on_settings_changed(R"({"theme":"night"})");
  expect_true(!rig.hud.shows_typing(), "another setting leaves it alone");
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
  asks_the_position_on_a_new_channel_for_its_menu();
  parses_a_menu();
  a_press_is_a_call_spoken_once_the_console_takes_it();
  a_refused_call_is_shown_and_not_spoken();
  auto_readback_reads_back_what_was_heard();
  auto_readback_waits_for_the_controller_to_finish();
  auto_readback_off_presses_nothing();
  the_next_frequency_tunes_the_selected_radio();
  a_shared_channel_shows_both_roles();
  a_reply_is_shown_when_its_voice_starts();
  a_reply_whose_voice_never_starts_is_still_shown();
  a_voice_that_starts_before_its_line_shows_the_line_at_once();
  without_voices_a_reply_is_shown_at_once();
  a_choice_opens_and_its_option_is_the_call();
  a_typed_call_is_sent_and_spoken();
  the_typing_line_is_a_setting();
  the_transcript_is_a_setting();

  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "hud session: all checks passed\n";
  return EXIT_SUCCESS;
}
