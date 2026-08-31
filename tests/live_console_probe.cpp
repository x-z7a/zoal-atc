// live_console_probe drives the shipped client's own code against a running
// console, over a real socket, and reports what the pilot's panel would show.
//
// It exists because the two bugs it checks for were invisible to every test on
// either side. Each half was correct alone: the console answered every request
// it was asked, and the bridge settled every response it was given. What was
// broken was the conversation between them — a console that never speaks first,
// and a subscribe answered once per topic instead of once — and nothing that
// tests one side by itself can see that.
//
// It is not a simulator. It links the real WebSocketClient and the real
// GuiBridge, both of which are SDK-free, so what it proves is what the plugin's
// own code does when a console is on the other end. What it cannot prove is
// anything about X-Plane: no scenery, no datarefs, no CEF, and no pilot.
//
//   live_console_probe <host> <port> [token]

#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/transport/websocket_client.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::uint64_t now_ms() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

int failures = 0;

void check(bool ok, const std::string &what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
  if (!ok) {
    ++failures;
  }
}

// Heard is everything the socket has produced so far.
//
// One record rather than a return value, because a frame consumed inside one
// wait and wanted by a later one is simply gone — which is how the first
// version of this probe managed to watch a reply arrive and then report that
// none had.
struct Heard {
  std::atomic<bool> answered{false}; // a settled panel promise
  std::string spoken;                // the controller's transmission
};

// The receiver runs on a thread of its own, which is not an incidental
// choice: receive_text blocks until a frame arrives, and the plugin's own
// design puts it on the websocket thread with the flight loop sending from the
// main one. A single-threaded version of this probe deadlocks the moment it
// waits for an answer to something it has not sent yet — twice, in the writing
// of this file, which is a decent argument that the split is load-bearing
// rather than incidental.
struct Receiver {
  zoal_atc::transport::WebSocketClient &socket;
  zoal_atc::gui::GuiBridge &bridge;
  Heard &heard;
  std::atomic<bool> running{true};

  void operator()() {
    while (running.load()) {
      std::string payload;
      const auto status = socket.receive_text(payload);
      if (!running.load()) {
        return;
      }
      if (!status.ok || payload.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      // Exactly what the plugin's loop does, in the same order.
      bridge.set_connected(true, now_ms());
      if (!bridge.on_socket_frame(payload, now_ms()) &&
          payload.find("\"type\":\"atc_reply\"") != std::string::npos) {
        std::lock_guard<std::mutex> lock(said);
        heard.spoken = payload;
      }
    }
  }

  std::mutex said;
};

// flush is the flight loop's half: whatever the bridge wants said, said.
void flush(zoal_atc::transport::WebSocketClient &socket,
           zoal_atc::gui::GuiBridge &bridge, Heard &heard) {
  for (const auto &frame : bridge.take_socket_frames()) {
    (void)socket.send_text(frame);
  }
  for (const auto &message : bridge.take_js_messages(16)) {
    if (message.payload.find("\"kind\":\"response\"") != std::string::npos &&
        message.payload.find("\"ok\":true") != std::string::npos) {
      heard.answered = true;
    }
  }
  bridge.tick(now_ms());
}

template <typename Predicate>
bool waited(zoal_atc::transport::WebSocketClient &socket,
            zoal_atc::gui::GuiBridge &bridge, Heard &heard, Predicate predicate,
            std::chrono::milliseconds within) {
  const auto deadline = std::chrono::steady_clock::now() + within;
  while (std::chrono::steady_clock::now() < deadline) {
    flush(socket, bridge, heard);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  flush(socket, bridge, heard);
  return predicate();
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: live_console_probe <host> <port> [token]\n";
    return 2;
  }

  zoal_atc::transport::WebSocketEndpoint endpoint;
  endpoint.host = argv[1];
  endpoint.port = static_cast<std::uint16_t>(std::stoi(argv[2]));
  endpoint.path = "/plugin";
  if (argc > 3) {
    endpoint.auth_token = argv[3];
  }
  endpoint.client_id = "live-console-probe";
  endpoint.simbrief_id = "000000";

  zoal_atc::transport::WebSocketClient socket(endpoint);
  const auto connected = socket.connect();
  if (!connected.ok) {
    std::cerr << "could not reach the console: " << connected.message << "\n";
    return 2;
  }
  std::cout << "connected to " << endpoint.host << ":" << endpoint.port
            << endpoint.path << std::endl;

  zoal_atc::gui::GuiBridge bridge;
  Heard heard;
  Receiver receiver{socket, bridge, heard};
  std::thread receiving(std::ref(receiver));

  // Ground at KSFO, on the ground, where a radio check belongs. Sent
  // repeatedly because the client reports continuously and because the
  // projection reading it is a separate consumer of a separate stream: a call
  // cannot be assigned to a station until the console has seen the radio.
  const std::string telemetry =
      R"({"type":"telemetry","session_id":"probe","telemetry":{"latitude_deg":37.6213,)"
      R"("longitude_deg":-122.3790,"altitude_ft_msl":13,"com1_freq_mhz":121.8,)"
      R"("active_com":1,"on_ground":true,"airport_id":"KSFO","traffic_status":"known_empty"}})";

  std::cout << "\nthe console speaks first" << std::endl;
  (void)socket.send_text(telemetry);
  const bool greeted = waited(
      socket, bridge, heard, [&] { return bridge.connected(); },
      std::chrono::seconds(10));
  check(greeted,
        "the bridge marked itself connected, so the panel would accept input "
        "(without a frame from the console it refuses everything locally)");

  std::cout << "\nthe panel opens and subscribes" << std::endl;
  bridge.attach_panel(now_ms());
  const bool subscribed = waited(
      socket, bridge, heard, [&] { return bridge.subscribed(); },
      std::chrono::seconds(10));
  check(subscribed,
        "the subscribe was answered once and affirmatively, so the panel is "
        "reachable rather than every control on it deadened");

  for (int i = 0; i < 10; ++i) {
    (void)socket.send_text(telemetry);
    flush(socket, bridge, heard);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\nthe pilot types instead of speaking" << std::endl;
  const auto submitted = bridge.submit(
      R"({"action":"submit_text","payload":{"text":"San Francisco Ground, radio check"}})",
      now_ms());
  check(submitted.accepted,
        "the panel accepted the transmission for sending: " + submitted.ack_json);

  const bool settled = waited(
      socket, bridge, heard, [&] { return heard.answered.load(); },
      std::chrono::seconds(15));
  check(settled, "the console took the typed turn and settled the panel's promise");

  std::cout << "\nand the radio answers" << std::endl;
  const bool spoke = waited(
      socket, bridge, heard,
      [&] {
        std::lock_guard<std::mutex> lock(receiver.said);
        return !heard.spoken.empty();
      },
      std::chrono::seconds(120));
  check(spoke, spoke ? "a controller answered the typed call:\n         " + heard.spoken
                     : "a controller answered the typed call");

  receiver.running.store(false);
  socket.close();
  receiving.join();
  std::cout << "\n"
            << (failures == 0 ? std::string("all clear")
                              : "failures: " + std::to_string(failures))
            << std::endl;
  return failures == 0 ? 0 : 1;
}
