#include "zoal_atc/gui/gui_bridge.hpp"

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

void expect_size(std::size_t got, std::size_t want, const char *label) {
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

std::string gui_response(std::uint64_t id, const std::string &payload) {
  return R"({"type":"gui_response","request_id":)" + std::to_string(id) +
         R"(,"ok":true,"payload":)" + payload + "}";
}

std::string gui_event(const std::string &name, const std::string &payload) {
  return R"({"type":"gui_event","event":")" + name + R"(","payload":)" +
         payload + "}";
}

std::vector<std::string> js_payloads(zoal_atc::gui::GuiBridge &bridge,
                                     std::size_t max = 64) {
  std::vector<std::string> payloads;
  for (const auto &message : bridge.take_js_messages(max)) {
    expect_eq(message.channel, zoal_atc::gui::kConsoleEventChannel,
              "js message channel");
    payloads.push_back(message.payload);
  }
  return payloads;
}

zoal_atc::gui::GuiBridgeConfig test_config() {
  zoal_atc::gui::GuiBridgeConfig config;
  config.request_timeout_ms = 1000;
  config.max_pending = 3;
  config.max_js_queue = 4;
  return config;
}

// bring_up leaves the ordinary state: socket up, page loaded and showing, the
// subscription answered. Request ids therefore continue from 2, and the
// subscribe frame and status messages it produced have already been taken.
void bring_up(zoal_atc::gui::GuiBridge &bridge) {
  bridge.set_connected(true, 0);
  bridge.attach_panel(0);
  bridge.on_socket_frame(gui_response(1, "null"), 0);
  bridge.take_socket_frames();
  bridge.take_js_messages(64);
}

void test_submit_needs_a_console() {
  zoal_atc::gui::GuiBridge bridge(test_config());

  // No socket, so there is nothing to proxy to. The panel is told now rather
  // than left waiting for a timeout that proves what we already know.
  const auto rejected =
      bridge.submit(R"({"action":"flight_snapshot"})", 0);
  expect_true(!rejected.accepted, "submit rejected while disconnected");
  expect_eq(rejected.ack_json,
            R"({"accepted":false,"request_id":0,)"
            R"("error":"console disconnected"})",
            "disconnected ack");
  expect_size(bridge.take_socket_frames().size(), 0,
              "no frame built while disconnected");

  bridge.set_connected(true, 0);
  const auto malformed = bridge.submit("not json", 0);
  expect_true(!malformed.accepted, "malformed request rejected");
  expect_eq(malformed.ack_json,
            R"({"accepted":false,"request_id":0,)"
            R"("error":"malformed request"})",
            "malformed ack");
}

void test_attach_subscribes_and_acks() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bridge.set_connected(true, 0);

  // Nothing is queued for a page that has not said it is listening.
  expect_size(bridge.take_js_messages(64).size(), 0,
              "no js messages before attach");

  expect_eq(bridge.attach_panel(0), R"({"ok":true,"replayed":0})",
            "attach ack with an empty cache");

  const auto frames = bridge.take_socket_frames();
  expect_size(frames.size(), 1, "attach subscribes");
  if (!frames.empty()) {
    expect_eq(frames[0],
              R"({"type":"gui_subscribe","request_id":1,)"
              R"("topics":["flight","facility"]})",
              "subscribe frame");
  }

  const auto messages = js_payloads(bridge);
  expect_size(messages.size(), 1, "attach posts one status");
  if (!messages.empty()) {
    expect_eq(messages[0],
              R"({"kind":"status","connected":true,"subscribed":false,)"
              R"("pending":1,"dropped_events":0})",
              "attach status");
  }

  // The console's answer to our own subscribe settles the flag and is not
  // forwarded: no browser promise is waiting on a request it never made.
  expect_true(bridge.on_socket_frame(gui_response(1, "null"), 0),
              "subscribe response consumed");
  expect_true(bridge.subscribed(), "subscribed after the console answers");
  expect_size(bridge.pending_count(), 0, "subscribe no longer pending");
  const auto after = js_payloads(bridge);
  expect_size(after.size(), 1, "only the status change is posted");
  if (!after.empty()) {
    expect_eq(after[0],
              R"({"kind":"status","connected":true,"subscribed":true,)"
              R"("pending":0,"dropped_events":0})",
              "subscribed status");
  }
}

void test_responses_match_out_of_order() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);

  const auto first = bridge.submit(
      R"({"action":"tune_radio","payload":{"mhz":118.8}})", 0);
  const auto second = bridge.submit(R"({"action":"flight_snapshot"})", 0);
  expect_true(first.accepted && second.accepted, "both submits accepted");
  expect_size(first.request_id, 2, "first panel request id");
  expect_size(second.request_id, 3, "second panel request id");
  expect_size(bridge.pending_count(), 2, "two requests pending");

  const auto frames = bridge.take_socket_frames();
  expect_size(frames.size(), 2, "two request frames");
  if (frames.size() == 2) {
    expect_eq(frames[0],
              R"({"type":"gui_request","request_id":2,"action":"tune_radio",)"
              R"("payload":{"mhz":118.8}})",
              "tune_radio frame");
    expect_eq(frames[1],
              R"({"type":"gui_request","request_id":3,)"
              R"("action":"flight_snapshot","payload":null})",
              "flight_snapshot frame");
  }

  // The console answers the second request first. Correlation is by id, so the
  // panel still learns which of its calls each answer belongs to.
  expect_true(bridge.on_socket_frame(gui_response(3, R"({"callsign":"GABC"})"),
                                     10),
              "late-first response consumed");
  expect_true(bridge.on_socket_frame(gui_response(2, R"({"tuned":true})"), 20),
              "second response consumed");
  expect_size(bridge.pending_count(), 0, "nothing left pending");

  const auto messages = js_payloads(bridge);
  expect_size(messages.size(), 2, "both responses posted");
  if (messages.size() == 2) {
    expect_eq(messages[0],
              R"({"kind":"response","request_id":3,"ok":true,)"
              R"("payload":{"callsign":"GABC"},"error":""})",
              "response for request 3");
    expect_eq(messages[1],
              R"({"kind":"response","request_id":2,"ok":true,)"
              R"("payload":{"tuned":true},"error":""})",
              "response for request 2");
  }

  // A response to a request that already timed out, or to another socket's id,
  // matches nothing and settles nothing.
  expect_true(bridge.on_socket_frame(gui_response(99, "null"), 30),
              "stray response consumed");
  expect_size(js_payloads(bridge).size(), 0, "stray response posts nothing");
}

void test_requests_time_out() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);

  const auto request = bridge.submit(R"({"action":"refresh_flight_plan"})", 0);
  expect_true(request.accepted, "submit accepted");
  bridge.take_socket_frames();

  bridge.tick(999);
  expect_size(bridge.pending_count(), 1, "still pending inside the window");
  expect_size(js_payloads(bridge).size(), 0, "nothing posted yet");

  bridge.tick(1000);
  expect_size(bridge.pending_count(), 0, "expired at the deadline");
  const auto messages = js_payloads(bridge);
  expect_size(messages.size(), 1, "timeout posts one response");
  if (!messages.empty()) {
    expect_eq(messages[0],
              R"({"kind":"response","request_id":2,"ok":false,)"
              R"("payload":null,"error":"request timed out"})",
              "timeout response");
  }

  // The console's late answer arrives after the panel gave up. It correlates
  // with nothing, so it cannot resolve a promise that already rejected.
  expect_true(bridge.on_socket_frame(gui_response(2, R"({"ok":1})"), 1500),
              "late response consumed");
  expect_size(js_payloads(bridge).size(), 0, "late response posts nothing");
}

void test_pending_is_bounded() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);

  // max_pending is 3 and the subscribe already settled, so three panel requests
  // fit and the fourth is refused rather than queued forever.
  expect_true(bridge.submit(R"({"action":"a"})", 0).accepted, "submit 1");
  expect_true(bridge.submit(R"({"action":"b"})", 0).accepted, "submit 2");
  expect_true(bridge.submit(R"({"action":"c"})", 0).accepted, "submit 3");
  const auto refused = bridge.submit(R"({"action":"d"})", 0);
  expect_true(!refused.accepted, "fourth submit refused");
  expect_eq(refused.ack_json,
            R"({"accepted":false,"request_id":0,)"
            R"("error":"too many requests in flight"})",
            "backpressure ack");
  expect_size(bridge.pending_count(), 3, "pending stays capped");
}

void test_disconnect_settles_and_reconnect_resubscribes() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);

  bridge.submit(R"({"action":"tune_radio"})", 0);
  bridge.take_socket_frames();
  expect_size(bridge.pending_count(), 1, "one request in flight");

  bridge.set_connected(false, 100);
  expect_size(bridge.pending_count(), 0, "disconnect settles everything");
  expect_true(!bridge.subscribed(), "a dropped socket is not subscribed");

  const auto messages = js_payloads(bridge);
  expect_size(messages.size(), 2, "failure then status");
  if (messages.size() == 2) {
    expect_eq(messages[0],
              R"({"kind":"response","request_id":2,"ok":false,)"
              R"("payload":null,"error":"console disconnected"})",
              "in-flight request fails on disconnect");
    expect_eq(messages[1],
              R"({"kind":"status","connected":false,"subscribed":false,)"
              R"("pending":0,"dropped_events":0})",
              "disconnected status");
  }

  // The console remembers nothing about a socket that went away, so the
  // subscription is re-asserted rather than assumed.
  bridge.set_connected(true, 200);
  const auto frames = bridge.take_socket_frames();
  expect_size(frames.size(), 1, "reconnect resubscribes");
  if (!frames.empty()) {
    expect_eq(frames[0],
              R"({"type":"gui_subscribe","request_id":3,)"
              R"("topics":["flight","facility"]})",
              "resubscribe frame");
  }
}

void test_hidden_panel_coalesces_events() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);

  bridge.set_panel_visible(false, 0);
  bridge.take_js_messages(64);

  bridge.on_socket_frame(gui_event("telemetry", R"({"alt":1000})"), 0);
  bridge.on_socket_frame(gui_event("telemetry", R"({"alt":2000})"), 1);
  bridge.on_socket_frame(gui_event("telemetry", R"({"alt":3000})"), 2);
  bridge.on_socket_frame(gui_event("comm_log", R"([{"text":"roger"}])"), 3);

  expect_size(js_payloads(bridge).size(), 0,
              "nothing is posted to a panel nobody is looking at");
  expect_size(bridge.dropped_events(), 2, "two superseded updates dropped");

  // Showing it again must not replay the burst - only what is currently true.
  bridge.set_panel_visible(true, 10);
  const auto messages = js_payloads(bridge);
  expect_size(messages.size(), 3, "status plus the latest of each event");
  if (messages.size() == 3) {
    expect_eq(messages[0],
              R"({"kind":"status","connected":true,"subscribed":true,)"
              R"("pending":0,"dropped_events":2})",
              "status on unhide");
    expect_eq(messages[1],
              R"({"kind":"event","event":"telemetry",)"
              R"("payload":{"alt":3000}})",
              "latest telemetry replayed");
    expect_eq(messages[2],
              R"({"kind":"event","event":"comm_log",)"
              R"("payload":[{"text":"roger"}]})",
              "comm_log replayed in first-seen order");
  }
}

void test_absent_panel_cannot_block_the_socket() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bridge.set_connected(true, 0);

  // The page never loaded. The receiver thread still hands frames over, and
  // must keep returning promptly with bounded memory behind it: this is the
  // path where a browser that fails to start would otherwise stall the radio.
  const auto pending_before = bridge.pending_count();
  for (int i = 0; i < 5000; ++i) {
    bridge.on_socket_frame(
        gui_event("telemetry", R"({"alt":)" + std::to_string(i) + "}"), 0);
    bridge.on_socket_frame(gui_response(static_cast<std::uint64_t>(i), "null"),
                           0);
  }
  expect_size(bridge.pending_count(), pending_before,
              "stray responses create no state");
  expect_size(bridge.take_js_messages(64).size(), 0,
              "nothing is queued for a panel that never loaded");

  // What the panel finally sees is the current truth, not five thousand frames.
  bridge.attach_panel(0);
  const auto messages = js_payloads(bridge);
  expect_size(messages.size(), 2, "status plus one cached snapshot");
  if (messages.size() == 2) {
    expect_eq(messages[1],
              R"({"kind":"event","event":"telemetry",)"
              R"("payload":{"alt":4999}})",
              "the newest cached telemetry");
  }
}

void test_js_queue_drops_oldest_when_full() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);

  // max_js_queue is 4. A panel that stops draining loses the oldest messages,
  // never the newest: stale state is worse than missing state on a radio panel.
  for (int i = 0; i < 6; ++i) {
    bridge.on_socket_frame(
        gui_event("event" + std::to_string(i), R"({"n":)" +
                                                   std::to_string(i) + "}"),
        0);
  }
  const auto messages = js_payloads(bridge);
  expect_size(messages.size(), 4, "queue stays capped");
  if (messages.size() == 4) {
    expect_eq(messages[0],
              R"({"kind":"event","event":"event2","payload":{"n":2}})",
              "oldest messages dropped first");
    expect_eq(messages[3],
              R"({"kind":"event","event":"event5","payload":{"n":5}})",
              "newest message kept");
  }
  expect_size(bridge.dropped_events(), 2, "drops are counted");
}

void test_detach_forgets_the_page() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);
  bridge.submit(R"({"action":"tune_radio"})", 0);
  bridge.on_socket_frame(gui_event("telemetry", R"({"alt":1000})"), 0);
  expect_size(bridge.pending_count(), 1, "one panel request in flight");

  bridge.detach_panel();
  expect_size(bridge.take_js_messages(64).size(), 0,
              "a closed page is owed nothing");
  expect_size(bridge.pending_count(), 0,
              "requests belonging to a closed page are abandoned");

  // Reloading gets the cached snapshot: the console is not asked to re-derive
  // what the plugin already heard.
  expect_eq(bridge.attach_panel(0), R"({"ok":true,"replayed":1})",
            "reattach replays the cache");
}

// A page reloads by saying ready again, and the panel is shown and hidden all
// flight. Neither may leave subscriptions piling up: `pending` is what the
// panel's own status line reports, so a growing count would show the pilot work
// in flight that is not.
void test_reattaching_does_not_accumulate_subscriptions() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bridge.set_connected(true, 0);

  for (int i = 0; i < 5; ++i) {
    bridge.attach_panel(static_cast<std::uint64_t>(i));
  }
  expect_size(bridge.pending_count(), 1,
              "five reloads leave one outstanding subscribe");

  // Only the newest subscribe is still correlated, so answering it is what
  // settles the flag.
  const auto frames = bridge.take_socket_frames();
  expect_size(frames.size(), 5, "each reload re-asserts the subscription");
  expect_true(bridge.on_socket_frame(gui_response(5, "null"), 10),
              "newest subscribe response consumed");
  expect_true(bridge.subscribed(), "subscribed after the console answers");

  // Showing and hiding is not a resubscribe at all: the console's stream did
  // not stop just because the window was out of the way.
  bridge.take_js_messages(64);
  for (int i = 0; i < 5; ++i) {
    bridge.set_panel_visible(false, 20);
    bridge.set_panel_visible(true, 21);
  }
  expect_size(bridge.take_socket_frames().size(), 0,
              "show/hide sends no frames");
  expect_size(bridge.pending_count(), 0, "show/hide adds nothing pending");
}

void test_non_gui_frames_fall_through() {
  zoal_atc::gui::GuiBridge bridge(test_config());
  bring_up(bridge);

  expect_true(!bridge.on_socket_frame(
                  R"({"type":"atc_reply","text":"roger","generation":4})", 0),
              "atc_reply is left to the reply parser");
  expect_true(!bridge.on_socket_frame(R"({"type":"telemetry_control"})", 0),
              "telemetry_control is left to the rate controller");
  expect_true(bridge.on_socket_frame(R"({"type":"gui_response"})", 0),
              "a malformed gui frame is consumed by the gui, not passed on");
}

} // namespace

int main() {
  test_submit_needs_a_console();
  test_attach_subscribes_and_acks();
  test_responses_match_out_of_order();
  test_requests_time_out();
  test_pending_is_bounded();
  test_disconnect_settles_and_reconnect_resubscribes();
  test_hidden_panel_coalesces_events();
  test_absent_panel_cannot_block_the_socket();
  test_js_queue_drops_oldest_when_full();
  test_detach_forgets_the_page();
  test_reattaching_does_not_accumulate_subscriptions();
  test_non_gui_frames_fall_through();

  if (g_failures != 0) {
    return EXIT_FAILURE;
  }
  return 0;
}
