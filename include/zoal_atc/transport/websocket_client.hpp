#ifndef ZOAL_ATC_TRANSPORT_WEBSOCKET_CLIENT_HPP
#define ZOAL_ATC_TRANSPORT_WEBSOCKET_CLIENT_HPP

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace zoal_atc::transport {

using SocketHandle = intptr_t;

struct WebSocketEndpoint {
  std::string host = "127.0.0.1";
  std::uint16_t port = 8765;
  std::string path = "/plugin";
  // Sent as `Authorization: Bearer <token>` on the upgrade when non-empty.
  // Empty means an unauthenticated console, which is the loopback default.
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
  [[nodiscard]] bool connected() const;
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
  bool connected_ = false;
};

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_WEBSOCKET_CLIENT_HPP
