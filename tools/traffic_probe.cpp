// traffic_probe: a headless zoal-mp client. Connects to a console (or
// tools/mockconsole), reports a fixed ownship position, and prints the traffic
// it receives through the same TrafficRegistry the plugin uses.
//
// Usage: traffic_probe [ws://host:port/path] [lat] [lon] [seconds]
#include "zoal_atc/telemetry/telemetry_frame.hpp"
#include "zoal_atc/traffic/traffic_frame.hpp"
#include "zoal_atc/traffic/traffic_registry.hpp"

#include "zoal_atc/transport/endpoint_config.hpp"
#include "zoal_atc/transport/websocket_client.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <iomanip>

int main(int argc, char **argv) {
  std::string url = "ws://127.0.0.1:8766/plugin";
  double lat = 37.6188;
  double lon = -122.375;
  long run_time_sec = 30;

  if (argc > 1) url = argv[1];
  if (argc > 2) lat = std::strtod(argv[2], nullptr);
  if (argc > 3) lon = std::strtod(argv[3], nullptr);
  if (argc > 4) run_time_sec = std::strtol(argv[4], nullptr, 10);

  auto parsed = zoal_atc::transport::parse_websocket_url(url);
  if (!parsed.ok) {
    std::cerr << "traffic_probe: bad url: " << parsed.error << std::endl;
    return 2;
  }

  auto endpoint = parsed.endpoint;
  endpoint.client_id = "traffic-probe";
  endpoint.capabilities.push_back(zoal_atc::traffic::kTrafficCapability);

  zoal_atc::transport::WebSocketClient client(endpoint);

  zoal_atc::telemetry::TelemetrySnapshot own;
  own.latitude_deg = lat;
  own.longitude_deg = lon;
  own.altitude_ft_msl = 13.0;
  own.on_ground = true;
  own.callsign = "PROBE1";
  own.icao_type = "C172";
  own.has_render = true;
  own.gear = 1.0;

  auto now_s = []() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  };

  std::mutex mu;
  zoal_atc::traffic::TrafficRegistry registry;
  std::uint64_t frames = 0;
  std::uint64_t targets_seen = 0;
  std::atomic<bool> running{true};

  std::thread reader([&]() {
    while (running) {
      std::string payload;
      auto st = client.receive_text(payload);
      if (!st.ok) {
        if (running) {
          std::cerr << "traffic_probe: receive: " << st.message << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }

      double t = now_s();
      auto frame = zoal_atc::traffic::parse_traffic_frame(payload);
      if (frame) {
        std::lock_guard<std::mutex> lock(mu);
        registry.apply(*frame, t);
        frames++;
        targets_seen += frame->targets.size();
      }
    }
  });

  client.connect();

  // Send initial telemetry
  client.send_text(zoal_atc::telemetry::build_telemetry_frame(own, ""));

  double start_time = now_s();
  bool send_error_printed = false;
  double next_send = start_time + 0.5;
  double next_print = start_time + 1.0;

  while (running) {
    double current_time = now_s();
    if ((current_time - start_time) >= run_time_sec && run_time_sec != 0) {
      running = false;
      break;
    }

    // Send telemetry every 500ms
    if (current_time >= next_send) {
      next_send += 0.5;
      if (client.connected()) {
        auto send_st = client.send_text(zoal_atc::telemetry::build_telemetry_frame(own, ""));
        if (!send_st.ok && !send_error_printed) {
          std::cerr << "traffic_probe: send: " << send_st.message << std::endl;
          send_error_printed = true;
        }
      }
    }

    // Print registry state every 1000ms
    if (current_time >= next_print) {
      next_print += 1.0;
      std::lock_guard<std::mutex> lock(mu);
      registry.expire(current_time);

      std::cout << "--- t=" << std::fixed << std::setprecision(1) << (current_time - start_time)
                << " frames=" << frames
                << " tracks=" << registry.size()
                << " clock_offset=" << std::fixed << std::setprecision(3) << registry.clock().offset()
                << " synced=" << (registry.clock().synced() ? 1 : 0) << std::endl;

      std::vector<zoal_atc::traffic::Identity> identities;
      registry.for_each([&identities](const zoal_atc::traffic::Identity &id) {
        identities.push_back(id);
      });

      for (const auto &id : identities) {
        auto state = registry.state(id.id, current_time);
        if (!state) {
          std::cout << "  " << id.id << " (no state yet)" << std::endl;
        } else {
          const auto &s = *state;
          std::string kind_str = zoal_atc::traffic::kind_name(id.kind);
          std::cout << "  " << std::left << std::setw(22) << id.id
                    << " " << kind_str
                    << " " << std::left << std::setw(8) << id.callsign
                    << " " << std::left << std::setw(5) << id.icao_type
                    << " lat=" << std::fixed << std::setprecision(6) << s.lat_deg
                    << " lon=" << std::fixed << std::setprecision(6) << s.lon_deg
                    << " alt=" << std::fixed << std::setprecision(0) << s.alt_ft_msl
                    << " hdg=" << std::fixed << std::setprecision(0) << s.heading_deg
                    << " gs=" << std::fixed << std::setprecision(0) << s.groundspeed_kts
                    << " " << (s.on_ground ? "GND" : "AIR") << std::endl;
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  running = false;
  client.close();
  reader.join();

  if (frames > 0) {
    std::cout << "traffic_probe: done, " << frames << " frames, " << targets_seen << " target reports" << std::endl;
    return 0;
  } else {
    return 1;
  }
}
