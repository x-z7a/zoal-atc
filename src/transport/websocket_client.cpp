#include "zoal_atc/transport/websocket_client.hpp"

#include "zoal_atc/transport/base64.hpp"
#include "zoal_atc/transport/endpoint_config.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <random>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace zoal_atc::transport {
namespace {

#if defined(_WIN32)
using SocketIOResult = int;

class WinsockRuntime {
public:
  WinsockRuntime() {
    WSADATA data{};
    ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~WinsockRuntime() {
    if (ok_) {
      WSACleanup();
    }
  }
  [[nodiscard]] bool ok() const { return ok_; }

private:
  bool ok_ = false;
};

WinsockRuntime &winsock_runtime() {
  static WinsockRuntime runtime;
  return runtime;
}

std::string socket_error_message() {
  return "socket error " + std::to_string(WSAGetLastError());
}

void close_socket(SocketHandle socket) {
  closesocket(static_cast<SOCKET>(socket));
}

void shutdown_socket(SocketHandle socket) {
  shutdown(static_cast<SOCKET>(socket), SD_BOTH);
}

int send_socket(SocketHandle socket, const char *data, int size) {
  return send(static_cast<SOCKET>(socket), data, size, 0);
}

int recv_socket(SocketHandle socket, char *data, int size) {
  return recv(static_cast<SOCKET>(socket), data, size, 0);
}
#else
using SocketIOResult = ssize_t;

std::string socket_error_message() { return std::strerror(errno); }

void close_socket(SocketHandle socket) {
  close(static_cast<int>(socket));
}

void shutdown_socket(SocketHandle socket) {
  shutdown(static_cast<int>(socket), SHUT_RDWR);
}

SocketIOResult send_socket(SocketHandle socket, const char *data, int size) {
#if defined(MSG_NOSIGNAL)
  return send(static_cast<int>(socket), data, static_cast<std::size_t>(size),
              MSG_NOSIGNAL);
#else
  return send(static_cast<int>(socket), data, static_cast<std::size_t>(size), 0);
#endif
}

SocketIOResult recv_socket(SocketHandle socket, char *data, int size) {
  return recv(static_cast<int>(socket), data, static_cast<std::size_t>(size), 0);
}
#endif

std::string random_websocket_key() {
  std::array<std::uint8_t, 16> bytes{};
  std::random_device rd;
  for (auto &byte : bytes) {
    byte = static_cast<std::uint8_t>(rd());
  }
  return base64_encode(bytes.data(), bytes.size());
}

std::array<std::uint8_t, 4> random_mask() {
  std::array<std::uint8_t, 4> mask{};
  std::random_device rd;
  for (auto &byte : mask) {
    byte = static_cast<std::uint8_t>(rd());
  }
  return mask;
}

void append_u16(std::vector<std::uint8_t> &out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u64(std::vector<std::uint8_t> &out, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
}

std::uint16_t read_u16(const std::uint8_t *data) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(data[0]) << 8U) |
      static_cast<std::uint16_t>(data[1]));
}

std::uint64_t read_u64(const std::uint8_t *data) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8U) | static_cast<std::uint64_t>(data[i]);
  }
  return value;
}

std::uint64_t steady_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// How long a single dial may take. The OS default is far longer — about 75
// seconds on macOS for a host that drops packets rather than refusing — and the
// client's lock is held throughout, which is what turned an absent console into
// a stalled sim.
constexpr int kConnectTimeoutMs = 5000;

bool set_non_blocking(SocketHandle handle, bool enable) {
#if defined(_WIN32)
  u_long mode = enable ? 1UL : 0UL;
  return ioctlsocket(static_cast<SOCKET>(handle), FIONBIO, &mode) == 0;
#else
  const int fd = static_cast<int>(handle);
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  const int updated = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  return fcntl(fd, F_SETFL, updated) == 0;
#endif
}

// connect_within dials with a bounded wait: a non-blocking connect, then select
// for writability, then SO_ERROR for the verdict.
//
// The socket is returned to blocking mode on success, because every read and
// write after the handshake assumes it.
bool connect_within(SocketHandle handle, const addrinfo *ai, int timeout_ms) {
#if defined(_WIN32)
  const SOCKET fd = static_cast<SOCKET>(handle);
  const int addr_len = static_cast<int>(ai->ai_addrlen);
#else
  const int fd = static_cast<int>(handle);
  const socklen_t addr_len = ai->ai_addrlen;
  // select() cannot express a descriptor at or past FD_SETSIZE. A plugin holds
  // a handful of sockets, so this is a guard against the impossible rather than
  // a case to handle.
  if (fd < 0 || fd >= FD_SETSIZE) {
    return false;
  }
#endif
  if (!set_non_blocking(handle, true)) {
    return false;
  }

  bool ok = false;
  if (::connect(fd, ai->ai_addr, addr_len) == 0) {
    ok = true;
  } else {
#if defined(_WIN32)
    const bool in_progress = WSAGetLastError() == WSAEWOULDBLOCK;
#else
    const bool in_progress = errno == EINPROGRESS;
#endif
    if (in_progress) {
      fd_set writable;
      FD_ZERO(&writable);
      FD_SET(fd, &writable);
      timeval tv{};
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
#if defined(_WIN32)
      const int ready = select(0, nullptr, &writable, nullptr, &tv);
#else
      const int ready = select(fd + 1, nullptr, &writable, nullptr, &tv);
#endif
      if (ready > 0) {
        // Writable only means the attempt finished; SO_ERROR says how.
        int error = 0;
#if defined(_WIN32)
        int error_len = static_cast<int>(sizeof(error));
        const int got = getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                   reinterpret_cast<char *>(&error), &error_len);
#else
        socklen_t error_len = sizeof(error);
        const int got = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_len);
#endif
        ok = got == 0 && error == 0;
      }
    }
  }

  if (!ok) {
    return false;
  }
  return set_non_blocking(handle, false);
}

} // namespace

WebSocketClient::WebSocketClient(WebSocketEndpoint endpoint)
    : endpoint_(std::move(endpoint)) {}

WebSocketClient::~WebSocketClient() { close(); }

WebSocketStatus WebSocketClient::connect() {
  std::lock_guard<std::mutex> lock(mutex_);
  return connect_locked();
}

WebSocketStatus WebSocketClient::connect_locked() {
  if (connected_.load(std::memory_order_relaxed)) {
    return WebSocketStatus::success();
  }

  // One dial, then wait. Every queued frame and every receive used to re-dial,
  // which against an unreachable host meant a fresh OS connect timeout each
  // time, and against a console rejecting the token meant a hot loop.
  const std::uint64_t now_ms = steady_ms();
  if (!reconnect_gate_.ready(now_ms)) {
    // Worded from the backoff rather than the countdown so the text is stable
    // while a wait runs: a caller that suppresses repeats then prints one line
    // per escalation, not one per second.
    return WebSocketStatus::failure(
        "console not reachable; retrying every " +
        std::to_string(reconnect_gate_.delay_ms() / 1000) + "s");
  }

#if defined(_WIN32)
  if (!winsock_runtime().ok()) {
    reconnect_gate_.note_failure(now_ms);
    return WebSocketStatus::failure("WSAStartup failed");
  }
#endif

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const std::string port = std::to_string(endpoint_.port);

  addrinfo *results = nullptr;
  const int gai =
      getaddrinfo(endpoint_.host.c_str(), port.c_str(), &hints, &results);
  if (gai != 0) {
    reconnect_gate_.note_failure(now_ms);
    return WebSocketStatus::failure("resolve " + endpoint_.host + ": " +
                                    gai_strerror(gai));
  }

  WebSocketStatus status = WebSocketStatus::failure("connect failed");
  for (addrinfo *ai = results; ai != nullptr; ai = ai->ai_next) {
    const auto fd =
        socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
#if defined(_WIN32)
    if (fd == INVALID_SOCKET) {
#else
    if (fd < 0) {
#endif
      status = WebSocketStatus::failure(socket_error_message());
      continue;
    }

    if (connect_within(static_cast<SocketHandle>(fd), ai, kConnectTimeoutMs)) {
      socket_ = static_cast<SocketHandle>(fd);
      status = send_handshake_locked();
      if (status.ok) {
        status = read_handshake_response_locked();
      }
      if (status.ok) {
        connected_.store(true, std::memory_order_release);
        // Introduce ourselves before anything else crosses the wire, so the
        // console can attribute every later frame to this aircraft rather than
        // to whatever the frames themselves claim (phase22 M1). A reconnect
        // runs through here too, so the socket is always attributed.
        status = send_hello_locked();
        if (status.ok) {
          break;
        }
      }
      close_locked();
    } else {
      status = WebSocketStatus::failure(socket_error_message());
      close_socket(static_cast<SocketHandle>(fd));
    }
  }
  freeaddrinfo(results);

  // A rejected upgrade (a wrong token) lands here just like an unreachable
  // host, and must be paced the same way: it is a server that has already said
  // no, and asking again immediately only asks faster.
  if (status.ok) {
    reconnect_gate_.note_success();
  } else {
    reconnect_gate_.note_failure(now_ms);
  }
  return status;
}

void WebSocketClient::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  close_locked();
}

void WebSocketClient::reauthenticate(std::string token) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (endpoint_.auth_token == token) {
    // Saving the token you already had should not drop a working connection.
    return;
  }
  endpoint_.auth_token = std::move(token);
  // A new credential is new information. Whatever backoff the old one earned by
  // being rejected is no longer evidence about this one, and a pilot who has
  // just fixed their token should not sit out a thirty second wait for it.
  reconnect_gate_.reset();
  close_locked();
}

std::string WebSocketClient::auth_token() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return endpoint_.auth_token;
}

void WebSocketClient::close_locked() {
  if (socket_ != -1) {
    shutdown_socket(socket_);
    close_socket(socket_);
  }
  socket_ = -1;
  connected_.store(false, std::memory_order_release);
}

WebSocketStatus WebSocketClient::send_text(std::string_view payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  WebSocketStatus status = connect_locked();
  if (!status.ok) {
    return status;
  }
  return send_text_locked(payload);
}

WebSocketStatus WebSocketClient::send_hello_locked() {
  // A plugin with no identity stays silent here and the console falls back to
  // the single legacy flight, which is exactly the pre-M1 behaviour.
  if (endpoint_.client_id.empty()) {
    return WebSocketStatus::success();
  }
  // client_id is character-fenced at parse time (endpoint_config.cpp), so it
  // cannot carry a quote or backslash into this literal.
  std::string frame = std::string("{\"type\":\"hello\",\"protocol\":2,") +
                      "\"client_id\":\"" + endpoint_.client_id + "\"";
  if (!endpoint_.simbrief_id.empty()) {
    frame += ",\"simbrief_id\":\"" + endpoint_.simbrief_id + "\"";
  }
  frame += "}";
  return send_text_locked(frame);
}

WebSocketStatus WebSocketClient::send_text_locked(std::string_view payload) {
  std::vector<std::uint8_t> frame;
  frame.reserve(payload.size() + 16);
  frame.push_back(0x81U);

  if (payload.size() <= 125) {
    frame.push_back(static_cast<std::uint8_t>(0x80U | payload.size()));
  } else if (payload.size() <= 0xFFFFU) {
    frame.push_back(0x80U | 126U);
    append_u16(frame, static_cast<std::uint16_t>(payload.size()));
  } else {
    frame.push_back(0x80U | 127U);
    append_u64(frame, static_cast<std::uint64_t>(payload.size()));
  }

  const auto mask = random_mask();
  frame.insert(frame.end(), mask.begin(), mask.end());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(payload[i]) ^ mask[i % mask.size()]));
  }

  WebSocketStatus status = send_all_locked(frame.data(), frame.size());
  if (!status.ok) {
    close_locked();
  }
  return status;
}

WebSocketStatus WebSocketClient::receive_text(std::string &payload) {
  payload.clear();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    WebSocketStatus status = connect_locked();
    if (!status.ok) {
      return status;
    }
  }

  for (;;) {
    std::array<std::uint8_t, 2> header{};
    WebSocketStatus status = recv_exact(header.data(), header.size());
    if (!status.ok) {
      return status;
    }

    const bool final_frame = (header[0] & 0x80U) != 0U;
    const std::uint8_t opcode = static_cast<std::uint8_t>(header[0] & 0x0FU);
    const bool masked = (header[1] & 0x80U) != 0U;
    std::uint64_t payload_size = static_cast<std::uint64_t>(header[1] & 0x7FU);

    if (payload_size == 126U) {
      std::array<std::uint8_t, 2> extended{};
      status = recv_exact(extended.data(), extended.size());
      if (!status.ok) {
        return status;
      }
      payload_size = read_u16(extended.data());
    } else if (payload_size == 127U) {
      std::array<std::uint8_t, 8> extended{};
      status = recv_exact(extended.data(), extended.size());
      if (!status.ok) {
        return status;
      }
      payload_size = read_u64(extended.data());
    }

    constexpr std::uint64_t kMaxInboundFrameBytes = 1024U * 1024U;
    if (payload_size > kMaxInboundFrameBytes ||
        payload_size > static_cast<std::uint64_t>(
                           std::numeric_limits<std::size_t>::max())) {
      close();
      return WebSocketStatus::failure("websocket frame too large");
    }

    std::array<std::uint8_t, 4> mask{};
    if (masked) {
      status = recv_exact(mask.data(), mask.size());
      if (!status.ok) {
        return status;
      }
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(payload_size));
    if (!data.empty()) {
      status = recv_exact(data.data(), data.size());
      if (!status.ok) {
        return status;
      }
      if (masked) {
        for (std::size_t i = 0; i < data.size(); ++i) {
          data[i] ^= mask[i % mask.size()];
        }
      }
    }

    if (opcode == 0x8U) {
      close();
      return WebSocketStatus::failure("websocket closed");
    }
    if (opcode == 0x1U) {
      if (!final_frame) {
        close();
        return WebSocketStatus::failure(
            "fragmented websocket text frames are unsupported");
      }
      payload.assign(reinterpret_cast<const char *>(data.data()), data.size());
      return WebSocketStatus::success();
    }
    if (opcode == 0x9U || opcode == 0xAU) {
      continue;
    }
  }
}

WebSocketStatus WebSocketClient::recv_exact(std::uint8_t *data,
                                            std::size_t size) {
  std::size_t received = 0;
  while (received < size) {
    SocketHandle socket = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_.load(std::memory_order_relaxed) || socket_ == -1) {
        return WebSocketStatus::failure("websocket not connected");
      }
      socket = socket_;
    }

    const int chunk = static_cast<int>(std::min<std::size_t>(
        size - received, static_cast<std::size_t>(16 * 1024)));
    const SocketIOResult n =
        recv_socket(socket, reinterpret_cast<char *>(data + received), chunk);
    if (n <= 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (socket_ == socket) {
        close_locked();
      }
      return WebSocketStatus::failure(n == 0 ? "websocket closed"
                                             : socket_error_message());
    }
    received += static_cast<std::size_t>(n);
  }
  return WebSocketStatus::success();
}

WebSocketStatus
WebSocketClient::send_all_locked(const std::uint8_t *data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const int chunk = static_cast<int>(std::min<std::size_t>(
        size - sent, static_cast<std::size_t>(16 * 1024)));
    const SocketIOResult n =
        send_socket(socket_, reinterpret_cast<const char *>(data + sent), chunk);
    if (n <= 0) {
      return WebSocketStatus::failure(socket_error_message());
    }
    sent += static_cast<std::size_t>(n);
  }
  return WebSocketStatus::success();
}

WebSocketStatus WebSocketClient::send_handshake_locked() {
  std::ostringstream request;
  request << "GET " << endpoint_.path << " HTTP/1.1\r\n"
          << "Host: " << host_header(endpoint_) << "\r\n"
          << "Upgrade: websocket\r\n"
          << "Connection: Upgrade\r\n"
          << "Sec-WebSocket-Key: " << random_websocket_key() << "\r\n"
          << "Sec-WebSocket-Version: 13\r\n";
  if (!endpoint_.auth_token.empty()) {
    request << "Authorization: Bearer " << endpoint_.auth_token << "\r\n";
  }
  request << "\r\n";
  const std::string payload = request.str();
  return send_all_locked(reinterpret_cast<const std::uint8_t *>(payload.data()),
                         payload.size());
}

WebSocketStatus WebSocketClient::read_handshake_response_locked() {
  std::string response;
  response.reserve(1024);
  std::array<char, 512> buffer{};
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(3);
  while (response.find("\r\n\r\n") == std::string::npos) {
    if (std::chrono::steady_clock::now() > deadline) {
      return WebSocketStatus::failure("websocket handshake timed out");
    }
    const SocketIOResult n =
        recv_socket(socket_, buffer.data(), static_cast<int>(buffer.size()));
    if (n <= 0) {
      return WebSocketStatus::failure(socket_error_message());
    }
    response.append(buffer.data(), static_cast<std::size_t>(n));
    if (response.size() > 8192) {
      return WebSocketStatus::failure("websocket handshake response too large");
    }
  }

  if (response.rfind("HTTP/1.1 101", 0) != 0 &&
      response.rfind("HTTP/1.0 101", 0) != 0) {
    return WebSocketStatus::failure("websocket upgrade rejected");
  }
  return WebSocketStatus::success();
}

} // namespace zoal_atc::transport
