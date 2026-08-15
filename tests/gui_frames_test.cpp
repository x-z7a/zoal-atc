#include "zoal_atc/gui/gui_frames.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

} // namespace

int main() {
  using namespace zoal_atc::gui;

  // --- plugin -> console ----------------------------------------------------

  // No flight id travels in the frame: the console attributes a GUI request to
  // the socket it arrived on, which is what keeps one cockpit's panel from
  // naming another flight (phase22 identity, phase23 invariant).
  GuiRequest request;
  request.request_id = 7;
  request.action = "tune_radio";
  request.payload_json = R"({"mhz":118.8,"com":1})";
  expect_eq(build_gui_request(request),
            R"({"type":"gui_request","request_id":7,"action":"tune_radio",)"
            R"("payload":{"mhz":118.8,"com":1}})",
            "build_gui_request");

  GuiRequest bare;
  bare.request_id = 1;
  bare.action = "flight_snapshot";
  expect_eq(build_gui_request(bare),
            R"({"type":"gui_request","request_id":1,)"
            R"("action":"flight_snapshot","payload":null})",
            "build_gui_request without payload");

  GuiRequest quoted;
  quoted.request_id = 2;
  quoted.action = R"(say "again")";
  expect_eq(build_gui_request(quoted),
            R"({"type":"gui_request","request_id":2,)"
            R"("action":"say \"again\"","payload":null})",
            "build_gui_request escapes the action");

  expect_eq(build_gui_subscribe(3, {"flight", "facility"}),
            R"({"type":"gui_subscribe","request_id":3,)"
            R"("topics":["flight","facility"]})",
            "build_gui_subscribe");
  expect_eq(build_gui_subscribe(4, {}),
            R"({"type":"gui_subscribe","request_id":4,"topics":[]})",
            "build_gui_subscribe with no topics");

  // --- console -> plugin ----------------------------------------------------

  auto ok_response = parse_gui_response(
      R"({"type":"gui_response","request_id":7,"ok":true,)"
      R"("payload":{"tuned":true,"mhz":118.8}})");
  expect_true(ok_response.has_value(), "parse ok gui_response");
  if (ok_response.has_value()) {
    expect_eq(std::to_string(ok_response->request_id), "7", "response id");
    expect_true(ok_response->ok, "response ok flag");
    expect_eq(ok_response->payload_json, R"({"tuned":true,"mhz":118.8})",
              "response payload survives verbatim");
    expect_eq(ok_response->error, "", "response error empty");
  }

  auto failed = parse_gui_response(
      R"({"type":"gui_response","request_id":9,"ok":false,)"
      R"("error":"unknown action"})");
  expect_true(failed.has_value(), "parse failed gui_response");
  if (failed.has_value()) {
    expect_true(!failed->ok, "failed response ok flag");
    expect_eq(failed->error, "unknown action", "failed response error");
    expect_eq(failed->payload_json, "", "failed response has no payload");
  }

  expect_true(!parse_gui_response(R"({"type":"atc_reply","text":"roger"})")
                   .has_value(),
              "atc_reply is not a gui_response");
  expect_true(!parse_gui_response(R"({"type":"gui_response","ok":true})")
                   .has_value(),
              "gui_response without a request id is unmatched");

  auto event = parse_gui_event(
      R"({"type":"gui_event","event":"flight_snapshot",)"
      R"("payload":{"callsign":"GABC","phase":"taxi_out"}})");
  expect_true(event.has_value(), "parse gui_event");
  if (event.has_value()) {
    expect_eq(event->event, "flight_snapshot", "event name");
    expect_eq(event->payload_json, R"({"callsign":"GABC","phase":"taxi_out"})",
              "event payload survives verbatim");
  }
  expect_true(!parse_gui_event(R"({"type":"gui_event","payload":{}})")
                   .has_value(),
              "gui_event without a name is not routable");

  expect_true(is_gui_frame(R"({"type":"gui_event","event":"x"})"),
              "gui_event is a gui frame");
  expect_true(is_gui_frame(R"({"type":"gui_response"})"),
              "a malformed gui_response is still the GUI's to drop");
  expect_true(!is_gui_frame(R"({"type":"telemetry_control"})"),
              "telemetry_control is not a gui frame");
  expect_true(!is_gui_frame("garbage"), "non-JSON is not a gui frame");

  // --- browser -> plugin ----------------------------------------------------

  auto panel = parse_panel_request(
      R"({"action":"submit_text","payload":{"text":"ready to taxi"}})");
  expect_true(panel.has_value(), "parse panel request");
  if (panel.has_value()) {
    expect_eq(panel->action, "submit_text", "panel action");
    expect_eq(panel->payload_json, R"({"text":"ready to taxi"})",
              "panel payload");
  }

  // A payload naming its own "action" must not be read as the request's.
  auto shadowed = parse_panel_request(
      R"({"payload":{"action":"other_flight"},"action":"flight_snapshot"})");
  expect_true(shadowed.has_value(), "parse panel request with nested action");
  if (shadowed.has_value()) {
    expect_eq(shadowed->action, "flight_snapshot", "top-level action wins");
  }

  expect_true(!parse_panel_request(R"({"payload":{}})").has_value(),
              "panel request without an action is rejected");
  expect_true(!parse_panel_request("garbage").has_value(),
              "non-JSON panel request is rejected");

  // --- plugin -> browser ----------------------------------------------------

  GuiResponse js_ok;
  js_ok.request_id = 7;
  js_ok.ok = true;
  js_ok.payload_json = R"({"tuned":true})";
  expect_eq(build_js_response(js_ok),
            R"({"kind":"response","request_id":7,"ok":true,)"
            R"("payload":{"tuned":true},"error":""})",
            "build_js_response ok");

  GuiResponse js_err;
  js_err.request_id = 8;
  js_err.error = "console disconnected";
  expect_eq(build_js_response(js_err),
            R"({"kind":"response","request_id":8,"ok":false,)"
            R"("payload":null,"error":"console disconnected"})",
            "build_js_response error");

  GuiEvent js_event;
  js_event.event = "comm_log";
  js_event.payload_json = R"([{"text":"cleared"}])";
  expect_eq(build_js_event(js_event),
            R"({"kind":"event","event":"comm_log",)"
            R"("payload":[{"text":"cleared"}]})",
            "build_js_event");

  GuiStatus status;
  status.connected = true;
  status.subscribed = false;
  status.pending = 2;
  status.dropped_events = 5;
  expect_eq(build_js_status(status),
            R"({"kind":"status","connected":true,"subscribed":false,)"
            R"("pending":2,"dropped_events":5})",
            "build_js_status");

  expect_eq(build_request_ack(true, 12, ""),
            R"({"accepted":true,"request_id":12,"error":""})",
            "build_request_ack accepted");
  expect_eq(build_request_ack(false, 0, "console disconnected"),
            R"({"accepted":false,"request_id":0,)"
            R"("error":"console disconnected"})",
            "build_request_ack rejected");

  if (g_failures != 0) {
    return EXIT_FAILURE;
  }
  return 0;
}
