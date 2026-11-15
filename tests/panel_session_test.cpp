#include "zoal_atc/gui/panel_session.hpp"

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
using zoal_atc::transport::json_string_field;
using zoal_atc::transport::json_uint_field;

constexpr std::int64_t kWall = 1'794'052'800'000;

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

std::string line(const std::string &kind, const std::string &text,
                 const std::string &at) {
  return R"({"kind":")" + kind + R"(","text":")" + text + R"(","at":")" + at +
         R"("})";
}

// A bridge whose plugin answers the local actions the way xplane_plugin.cpp
// does, recording what it was asked.
struct Rig {
  struct Asked {
    std::string action;
    std::string payload;
  };
  std::vector<Asked> local;
  std::string theme = "graphite";
  std::string pilot_id = "123456";
  std::string token;
  bool refuse_theme = false;
  GuiBridge bridge;
  PanelSession panel{bridge};

  Rig() {
    bridge.set_local_action(
        [this](const std::string &action,
               const std::string &payload) -> std::optional<LocalAnswer> {
          auto settings = [this] {
            return R"({"simbriefPilotId":")" + pilot_id + R"(","theme":")" +
                   theme + R"("})";
          };
          if (action == "settings") {
            local.push_back({action, payload});
            return LocalAnswer{true, settings(), ""};
          }
          if (action == "save_settings") {
            local.push_back({action, payload});
            if (const auto t = json_string_field(payload, "theme")) {
              if (refuse_theme) {
                return LocalAnswer{false, "null", "could not write"};
              }
              theme = *t;
            }
            if (const auto id = json_string_field(payload, "simbriefPilotId")) {
              pilot_id = *id;
            }
            return LocalAnswer{true, settings(), ""};
          }
          if (action == "connection_settings" ||
              action == "save_connection_settings") {
            local.push_back({action, payload});
            if (const auto t = json_string_field(payload, "token")) {
              token = *t;
            }
            return LocalAnswer{true,
                               R"({"token":")" + token +
                                   R"(","url":"ws://atc.zoal.app/plugin"})",
                               ""};
          }
          if (action == "tune_radio") {
            local.push_back({action, payload});
            return LocalAnswer{true, "{}", ""};
          }
          return std::nullopt;
        });
  }

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

  // Connects, attaches the panel and has the console accept its
  // subscription. Returns what the panel asked the console on the way up.
  std::vector<Sent> bring_up() {
    bridge.set_connected(true, 0);
    panel.attach(0);
    for (const Sent &item : sent()) {
      if (item.what == "gui_subscribe") {
        bridge.on_socket_frame(gui_ok(item.id), 0);
      }
    }
    panel.pump(0, kWall);
    return sent();
  }

  static std::uint64_t id_of(const std::vector<Sent> &items,
                             const std::string &action) {
    std::uint64_t id = 0;
    for (const Sent &item : items) {
      if (item.what == action) {
        id = item.id;
      }
    }
    return id;
  }
};

void formats_the_readouts() {
  PanelTelemetry t;
  t.altitude_ft_msl = 12345.4;
  expect_str(format_altitude(t), "12,345 ft", "grouped altitude");
  t.on_ground = true;
  expect_str(format_altitude(t), "on ground", "on the ground");
  expect_str(format_thousands(999), "999", "no separator under a thousand");
  expect_str(format_thousands(1000000), "1,000,000", "two separators");
  expect_str(format_frequency(118.8), "118.800", "three decimals");
  expect_str(format_frequency(0.0), kUnknown, "no frequency yet");
  expect_str(format_standby(121.9), "sby 121.900", "standby");
  expect_str(format_groundspeed(141.6), "142 kt", "rounded groundspeed");
  t.surveillance = "current";
  t.traffic_count = 3;
  expect_str(format_traffic(t), "3 contacts", "current traffic");
  t.surveillance = "";
  expect_str(format_traffic(t), "unavailable", "no feed is not no traffic");
  expect_str(format_facility_count(1), "1 aircraft", "one");
  expect_str(format_facility_count(4), "4 aircraft", "several");
  expect_str(format_plan_route(std::nullopt), kUnknown, "no plan");
  PanelFlightPlan plan;
  plan.has_plan = true;
  plan.origin = "CYQT";
  plan.destination = "CYYZ";
  plan.cruise_alt_ft = 24000;
  plan.aircraft_icao = "DH8D";
  expect_str(format_plan_route(plan), "CYQT \xE2\x86\x92 CYYZ", "route");
  expect_str(format_plan_detail(plan), "DH8D \xC2\xB7 24,000 ft", "detail");
  PanelLink link;
  expect_str(describe_link(link), "disconnected", "down");
  link.connected = true;
  expect_str(describe_link(link), "subscribing", "not yet listening");
  link.subscribed = true;
  link.pending = 2;
  expect_str(describe_link(link), "connected (2 in flight)", "busy");
  expect_true(parse_frequency(" 118.8").value_or(0) == 118.8, "a frequency");
  expect_true(!parse_frequency("tower").has_value(), "not a frequency");
}

void reads_the_stored_settings_on_the_way_up() {
  Rig rig;
  rig.theme = "night";
  rig.token = "secret";
  rig.bring_up();
  rig.panel.pump(0, kWall);
  expect_true(rig.panel.settings_loaded(), "settings read");
  expect_str(rig.panel.simbrief_pilot_id(), "123456", "the stored pilot ID");
  expect_true(rig.panel.theme() == HudTheme::night, "the stored theme");
  expect_true(rig.panel.connection_loaded(), "connection read");
  expect_str(rig.panel.token(), "secret", "the stored token");
  expect_str(rig.panel.console_url(), "ws://atc.zoal.app/plugin", "the url");
}

void follows_the_console() {
  Rig rig;
  rig.bring_up();
  expect_true(rig.panel.reachable(), "reachable once subscribed");
  rig.bridge.on_socket_frame(
      gui_event("telemetry",
                R"({"frequencyMhz":118.8,"altitudeFtMsl":3500,"groundspeedKts":120,"onGround":false,"airport":"CYQT","surveillance":"current","trafficCount":2})"),
      0);
  rig.bridge.on_socket_frame(
      gui_event("flight_snapshot",
                R"({"hasSession":true,"callsign":"ACA21","lifecycle":"taxi"})"),
      0);
  rig.bridge.on_socket_frame(
      gui_event("facility_snapshot", R"({"count":3,"flights":[{"count":9}]})"),
      0);
  rig.panel.pump(0, kWall);
  expect_true(rig.panel.telemetry().has_value(), "telemetry");
  if (rig.panel.telemetry()) {
    expect_str(rig.panel.telemetry()->airport, "CYQT", "airport");
    expect_str(format_traffic(*rig.panel.telemetry()), "2 contacts", "traffic");
  }
  expect_str(rig.panel.callsign(), "ACA21", "callsign");
  expect_str(rig.panel.lifecycle(), "taxi", "lifecycle");
  expect_true(rig.panel.facility_count().value_or(0) == 3,
              "the top-level count, not a nested one");

  expect_true(rig.panel.freshness(kWall + 1000) == PanelFreshness::fresh,
              "fresh");
  expect_true(rig.panel.freshness(kWall + 5000) == PanelFreshness::stale,
              "stale after four seconds of silence");
  expect_str(rig.panel.freshness_message(kWall + 9000), "No telemetry for 9s",
             "says how stale");
  rig.bridge.set_connected(false, 10);
  rig.panel.pump(10, kWall);
  expect_true(rig.panel.freshness(kWall) == PanelFreshness::unreachable,
              "an unreachable console outranks stale telemetry");
}

void keeps_the_radio_log() {
  Rig rig;
  const auto up = rig.bring_up();
  const std::uint64_t sync = Rig::id_of(up, "comm_log");
  expect_true(sync != 0, "asks for the whole log when the link comes up");
  expect_true(Rig::id_of(up, "flight_plan") != 0, "and for the flight plan");

  // A line pushed while the resync is in flight...
  rig.bridge.on_socket_frame(
      gui_event("comm_log", line("atc", "taxi via alpha", "2026-11-07T12:00:02Z")),
      0);
  rig.panel.pump(0, kWall);
  expect_true(rig.panel.comm().size() == 1, "shown at once");

  // ...is kept when the record that comes back does not have it.
  rig.bridge.on_socket_frame(
      gui_ok(sync, "[" + line("pilot", "request taxi", "2026-11-07T12:00:00Z") +
                       "," + line("atc", "hold short", "2026-11-07T12:00:01Z") +
                       "]"),
      0);
  rig.panel.pump(0, kWall);
  expect_true(rig.panel.comm().size() == 3, "record plus the line since");
  if (rig.panel.comm().size() == 3) {
    expect_str(rig.panel.comm()[0].text, "request taxi", "oldest first");
    expect_str(rig.panel.comm()[2].text, "taxi via alpha", "newest last");
  }

  // A replayed line is not a second transmission.
  const std::uint64_t revision = rig.panel.comm_revision();
  rig.bridge.on_socket_frame(
      gui_event("comm_log", line("atc", "taxi via alpha", "2026-11-07T12:00:02Z")),
      0);
  rig.panel.pump(0, kWall);
  expect_true(rig.panel.comm().size() == 3, "replay ignored");
  expect_true(rig.panel.comm_revision() == revision, "nothing changed");

  for (int i = 0; i < 120; ++i) {
    rig.bridge.on_socket_frame(
        gui_event("comm_log", line("atc", "line " + std::to_string(i), "")), 0);
    rig.panel.pump(0, kWall);
  }
  expect_true(rig.panel.comm().size() == kPanelCommLimit, "capped");
  expect_str(rig.panel.comm().back().text, "line 119", "keeps the newest");
}

void tunes_through_the_plugin() {
  Rig rig;
  rig.bring_up();
  rig.panel.tune("tower", 0);
  expect_true(!rig.panel.home().error.empty(), "not a frequency is said");
  rig.panel.tune("118.8", 0);
  expect_true(rig.panel.home().error.empty(), "a new try clears it");
  expect_true(!rig.local.empty() && rig.local.back().action == "tune_radio",
              "the plugin is asked");
  if (!rig.local.empty()) {
    expect_true(rig.local.back().payload.find("\"mhz\":118.8") !=
                    std::string::npos,
                "with the frequency");
  }
}

void transmits_and_shows_a_refusal() {
  Rig rig;
  rig.bring_up();
  rig.panel.transmit("ACA21 \"ready\"", 0);
  expect_true(rig.panel.home().busy(), "in flight");
  const auto items = rig.sent();
  const std::uint64_t id = Rig::id_of(items, "submit_text");
  expect_true(id != 0, "sent to the console");
  for (const auto &item : items) {
    if (item.what == "submit_text") {
      expect_true(item.frame.find(R"(ACA21 \"ready\")") != std::string::npos,
                  "escaped");
    }
  }
  rig.bridge.on_socket_frame(gui_refused(id, "no session"), 0);
  rig.panel.pump(0, kWall);
  expect_true(!rig.panel.home().busy(), "settled");
  expect_str(rig.panel.home().error, "no session", "the console's own words");

  rig.bridge.set_connected(false, 0);
  rig.panel.pump(0, kWall);
  rig.panel.transmit("anyone", 0);
  expect_str(rig.panel.home().error, "console disconnected",
             "refused at once while the socket is down");
}

void ends_the_flight_on_the_second_press() {
  Rig rig;
  rig.bring_up();
  rig.bridge.on_socket_frame(
      gui_event("comm_log", line("atc", "cleared to CYYZ", "2026-11-07T12:00:00Z")),
      0);
  rig.panel.pump(0, kWall);
  rig.sent();

  rig.panel.press_new_flight(0, kWall);
  expect_true(rig.panel.armed(), "the first press arms");
  expect_true(Rig::id_of(rig.sent(), "end_flight_session") == 0,
              "and sends nothing");
  rig.panel.pump(0, kWall + kPanelArmedMs);
  expect_true(!rig.panel.armed(), "arming lapses");

  rig.panel.press_new_flight(0, kWall);
  rig.panel.press_new_flight(0, kWall + 100);
  expect_true(!rig.panel.armed(), "the confirm disarms");
  const std::uint64_t id = Rig::id_of(rig.sent(), "end_flight_session");
  expect_true(id != 0, "the confirm ends the flight");
  rig.bridge.on_socket_frame(gui_ok(id), 0);
  rig.panel.pump(0, kWall + 200);
  expect_true(rig.panel.comm().empty(), "the log starts again");
}

void imports_the_plan() {
  Rig rig;
  rig.bring_up();
  rig.panel.pump(0, kWall);
  rig.panel.import_now(0);
  const auto items = rig.sent();
  const std::uint64_t id = Rig::id_of(items, "refresh_flight_plan");
  expect_true(id != 0, "asked");
  for (const auto &item : items) {
    if (item.what == "refresh_flight_plan") {
      expect_true(item.frame.find("123456") != std::string::npos,
                  "naming the stored pilot ID");
    }
  }
  rig.bridge.on_socket_frame(
      gui_ok(id, R"({"hasPlan":true,"origin":"CYQT","destination":"CYYZ"})"),
      0);
  rig.panel.pump(0, kWall);
  expect_str(format_plan_route(rig.panel.plan()), "CYQT \xE2\x86\x92 CYYZ",
             "the answer is the plan");
}

void saves_settings_locally() {
  Rig rig;
  rig.bring_up();
  rig.panel.pump(0, kWall);
  const std::uint64_t revision = rig.panel.settings_revision();
  rig.panel.save_pilot_id("777", 0);
  rig.panel.pump(0, kWall);
  expect_str(rig.panel.simbrief_pilot_id(), "777", "saved");
  expect_true(rig.panel.settings_revision() != revision, "the field reloads");

  rig.panel.pick_theme(HudTheme::daylight, 0);
  expect_true(rig.panel.theme() == HudTheme::daylight, "shown at once");
  rig.panel.pump(0, kWall);
  expect_str(rig.theme, "daylight", "stored");

  rig.refuse_theme = true;
  rig.panel.pick_theme(HudTheme::night, 0);
  rig.panel.pump(0, kWall);
  expect_true(rig.panel.theme() == HudTheme::daylight,
              "a refused save puts the old one back");
  expect_str(rig.panel.local().error, "could not write", "and says why");

  // Local settings work while the console is down: they repair it.
  rig.bridge.set_connected(false, 0);
  rig.panel.pump(0, kWall);
  rig.panel.save_token("fresh", 0);
  rig.panel.pump(0, kWall);
  expect_str(rig.panel.token(), "fresh", "token saved with no console");

  rig.panel.on_settings_changed(R"({"theme":"night"})");
  expect_true(rig.panel.theme() == HudTheme::night, "follows a save elsewhere");
}

void splits_a_json_array() {
  using zoal_atc::transport::json_array_elements;
  const auto three = json_array_elements(R"( [1, {"a":[2,3]}, "x,y"] )");
  expect_true(three.has_value() && three->size() == 3, "three elements");
  if (three && three->size() == 3) {
    expect_str((*three)[1], R"({"a":[2,3]})", "nested kept whole");
    expect_str((*three)[2], R"("x,y")", "a comma in a string");
  }
  expect_true(json_array_elements("[]").value_or(std::vector<std::string>{1})
                  .empty(),
              "empty");
  expect_true(!json_array_elements("{}").has_value(), "not an array");
  expect_true(!json_array_elements("[1,").has_value(), "truncated");
}

} // namespace

int main() {
  formats_the_readouts();
  reads_the_stored_settings_on_the_way_up();
  follows_the_console();
  keeps_the_radio_log();
  tunes_through_the_plugin();
  transmits_and_shows_a_refusal();
  ends_the_flight_on_the_second_press();
  imports_the_plan();
  saves_settings_locally();
  splits_a_json_array();

  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "panel session: all checks passed\n";
  return EXIT_SUCCESS;
}
