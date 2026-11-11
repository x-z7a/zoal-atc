// The reply thread and a redial must not close each other's sockets.
//
// Into Sydney on 10 November the reply thread was blocked reading a dead
// socket when the sending thread closed it and redialled. The new socket was
// handed the old one's number, and the reply thread, waking on the dead
// socket, found that number current and closed the new connection a second
// after it opened. The plugin dialled the console twice, and the console kept
// the abandoned socket as the aircraft's flight.
#include "zoal_atc/transport/websocket_client.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

#if !defined(_WIN32)
// A console that upgrades every connection and then only listens, recording
// for each connection whether its peer closed it.
class Console {
public:
  Console() {
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    require(::bind(listener_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0, "bind");
    require(::listen(listener_, 8) == 0, "listen");
    socklen_t len = sizeof(addr);
    ::getsockname(listener_, reinterpret_cast<sockaddr *>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    acceptor_ = std::thread([this] { accept_loop(); });
  }
  ~Console() {
    stopping_ = true;
    ::shutdown(listener_, SHUT_RDWR);
    ::close(listener_);
    acceptor_.join();
    for (auto &t : serving_) {
      t.join();
    }
  }
  std::uint16_t port() const { return port_; }
  std::size_t connections() {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_.size();
  }
  bool closed(std::size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    return index < closed_.size() && closed_[index];
  }
  void hang_up_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int fd : fds_) {
      ::shutdown(fd, SHUT_RDWR);
    }
  }

private:
  void accept_loop() {
    while (!stopping_) {
      const int fd = ::accept(listener_, nullptr, nullptr);
      if (fd < 0) {
        return;
      }
      std::size_t index;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        index = closed_.size();
        closed_.push_back(false);
        fds_.push_back(fd);
      }
      serving_.emplace_back([this, fd, index] { serve(fd, index); });
    }
  }
  void serve(int fd, std::size_t index) {
    std::string request;
    char buf[1024];
    while (request.find("\r\n\r\n") == std::string::npos) {
      const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) {
        mark(index);
        ::close(fd);
        return;
      }
      request.append(buf, static_cast<std::size_t>(n));
    }
    const std::string reply =
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: test\r\n\r\n";
    ::send(fd, reply.data(), reply.size(), 0);
    for (;;) {
      const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) {
        break;
      }
    }
    mark(index);
    ::close(fd);
  }
  void mark(std::size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_[index] = true;
  }

  int listener_ = -1;
  std::uint16_t port_ = 0;
  std::atomic<bool> stopping_{false};
  std::thread acceptor_;
  std::vector<std::thread> serving_;
  std::mutex mutex_;
  std::vector<bool> closed_;
  std::vector<int> fds_;
};

void test_a_redial_survives_the_reader_waking_on_the_old_socket() {
  Console console;
  zoal_atc::transport::WebSocketEndpoint endpoint;
  endpoint.host = "127.0.0.1";
  endpoint.port = console.port();
  zoal_atc::transport::WebSocketClient client(endpoint);
  require(client.connect().ok, "first connection");

  // The reply thread blocks reading the first socket, as it always is.
  std::atomic<bool> woke{false};
  std::thread reader([&] {
    std::string payload;
    (void)client.receive_text(payload);
    woke = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // The sending thread gives the socket up and dials again.
  client.close();
  require(client.send_text("{\"type\":\"telemetry\"}").ok, "the redial connects and sends");

  for (int i = 0; i < 100 && !woke; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  require(woke, "the reader woke from the closed socket");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  require(console.connections() == 2, "two connections, one after the other");
  require(console.closed(0), "the first is closed");
  require(!console.closed(1), "the redial is still open: the reader did not close it");
  require(client.connected(), "and the client still holds it");

  client.close();
  console.hang_up_all();
  reader.join();
}
#endif

} // namespace

int main() {
#if !defined(_WIN32)
  test_a_redial_survives_the_reader_waking_on_the_old_socket();
#endif
  std::cout << "websocket client tests passed\n";
  return 0;
}
