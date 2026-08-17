#ifndef ZOAL_ATC_TRANSPORT_WEBSOCKET_CLIENT_HPP
#define ZOAL_ATC_TRANSPORT_WEBSOCKET_CLIENT_HPP

#include "zoal_atc/transport/reconnect_gate.hpp"

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace zoal_atc::transport {

using SocketHandle = intptr_t;

struct WebSocketEndpoint {
  // The hosted console, because that is where an unconfigured plugin has to
  // land. We host the console and ship only the plugin, so the pilot who
  // downloads a release is not running a console of their own — a loopback
  // default would dial a port nothing is listening on, and the panel's URL is
  // read-only, so their only way out would be hand-editing the config file the
  // panel exists to spare them. Developers running a console locally are the
  // ones who override now: `url = ws://127.0.0.1:8765/plugin` in
  // Output/preferences/zoal_atc.cfg, or ZOAL_ATC_CONSOLE_URL.
  std::string host = "atc.zoal.app";
  std::uint16_t port = 80;
  std::string path = "/plugin";
  // Sent as `Authorization: Bearer <token>` on the upgrade when non-empty.
  // Empty means a console that asks for no shared secret.
  std::string auth_token;
  // Stable identity for this plugin installation, generated on first run and
  // persisted in the config file (phase22 M1). It identifies the airframe, not
  // the flight — the analogue of a Mode S address rather than a callsign — and
  // is what lets the console tell two connected aircraft apart. Empty means a
  // pre-handshake plugin, which the console treats as the single legacy flight.
  std::string client_id;
  // SimBrief pilot ID, so the console can fetch this aircraft's OFP itself.
  // A pilot flying against someone else's console has no UI to type it into
  // (phase22 M11); empty simply means no flight plan, which the arrival chain
  // already tolerates.
  std::string simbrief_id;
};

struct WebSocketStatus {
  bool ok = true;
  std::string message;

  static WebSocketStatus success() { return {true, {}}; }
  static WebSocketStatus failure(std::string reason) {
    return {false, std::move(reason)};
  }
};

class TextMessageSink {
public:
  virtual ~TextMessageSink() = default;

  virtual WebSocketStatus send_text(std::string_view payload) = 0;
};

class WebSocketClient : public TextMessageSink {
public:
  explicit WebSocketClient(WebSocketEndpoint endpoint = {});
  ~WebSocketClient();

  WebSocketClient(const WebSocketClient &) = delete;
  WebSocketClient &operator=(const WebSocketClient &) = delete;

  WebSocketStatus connect();
  void close();

  // Swaps the bearer token and drops the connection so the next re-dial carries
  // it. Both halves have to happen under one lock: a token changed while the
  // old socket lived would sit unused until something else disconnected, and
  // the pilot would be told a bad credential was saved and working.
  //
  // The reconnect itself is the receiver loop's job - it already re-dials
  // whatever it finds here.
  void reauthenticate(std::string token);

  // The token currently in use, for a panel that offers to change it.
  [[nodiscard]] std::string auth_token() const;

  // Deliberately lock-free. The flight loop asks this every frame, and the lock
  // it used to take is held across a blocking dial — so with the console away,
  // X-Plane's frame loop stalled for as long as the OS took to give up on the
  // connect. Reported as "massive lag" in flight on 2026-08-17.
  [[nodiscard]] bool connected() const {
    return connected_.load(std::memory_order_acquire);
  }
  WebSocketStatus send_text(std::string_view payload) override;
  [[nodiscard]] WebSocketStatus receive_text(std::string &payload);

private:
  [[nodiscard]] WebSocketStatus connect_locked();
  [[nodiscard]] WebSocketStatus recv_exact(std::uint8_t *data,
                                           std::size_t size);
  [[nodiscard]] WebSocketStatus send_all_locked(const std::uint8_t *data,
                                                std::size_t size);
  [[nodiscard]] WebSocketStatus send_text_locked(std::string_view payload);
  // Introduces this installation to the console as the first frame of every
  // connection, so a reconnect re-attributes the socket too (phase22 M1).
  [[nodiscard]] WebSocketStatus send_hello_locked();
  [[nodiscard]] WebSocketStatus send_handshake_locked();
  [[nodiscard]] WebSocketStatus read_handshake_response_locked();
  void close_locked();

  WebSocketEndpoint endpoint_;
  mutable std::mutex mutex_;
  SocketHandle socket_ = -1;
  // Atomic so connected() can answer without the lock; every write still happens
  // under mutex_.
  std::atomic<bool> connected_{false};
  // Paces re-dialling. Guarded by mutex_, like the socket it protects.
  ReconnectGate reconnect_gate_;
};

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_WEBSOCKET_CLIENT_HPP
