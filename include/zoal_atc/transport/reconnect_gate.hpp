#ifndef ZOAL_ATC_TRANSPORT_RECONNECT_GATE_HPP
#define ZOAL_ATC_TRANSPORT_RECONNECT_GATE_HPP

#include <cstdint>

namespace zoal_atc::transport {

// ReconnectGate paces re-dialling a console that is not answering.
//
// Without it, every queued frame and every receive attempt re-dialled: a DNS
// resolve plus a TCP connect, back to back, for as long as the console stayed
// away. That is wasteful when the console is merely down, and actively harmful
// when the host is unreachable rather than refusing, because each attempt then
// blocks for the OS connect timeout while holding the client's lock.
//
// A wrong auth token is the same shape and worse: the socket connects, the
// upgrade is rejected, and the plugin immediately tries again — a hot loop
// against a server that has already said no.
//
// So: one attempt, then wait. The first delay is five seconds (long enough to
// stop being a loop, short enough that a console restarted mid-flight is picked
// up before the pilot notices), doubling to thirty so a console that is gone for
// an hour is not dialled seven hundred times.
//
// Pure and clock-injected: the caller passes the time, so the pacing is testable
// without sockets or sleeping.
class ReconnectGate {
public:
  static constexpr std::uint64_t kInitialDelayMs = 5000;
  static constexpr std::uint64_t kMaxDelayMs = 30000;

  // ready reports whether a dial may be attempted now. A gate that has not
  // failed yet is always ready: the first connection must not be delayed.
  [[nodiscard]] bool ready(std::uint64_t now_ms) const {
    return !blocked_ || now_ms >= retry_at_ms_;
  }

  // note_failure records a dial that did not produce a usable connection, and
  // arms the next wait.
  void note_failure(std::uint64_t now_ms) {
    delay_ms_ = delay_ms_ == 0 ? kInitialDelayMs : delay_ms_ * 2;
    if (delay_ms_ > kMaxDelayMs) {
      delay_ms_ = kMaxDelayMs;
    }
    retry_at_ms_ = now_ms + delay_ms_;
    blocked_ = true;
  }

  // note_success clears the backoff, so a console that drops again later gets
  // the same prompt first retry rather than inheriting an hour-old delay.
  void note_success() { reset(); }

  // reset also clears it without a connection having succeeded, for when
  // something changed that makes the next dial worth trying immediately — a
  // pilot correcting a rejected auth token is not obliged to wait out a backoff
  // their old token earned.
  void reset() {
    delay_ms_ = 0;
    retry_at_ms_ = 0;
    blocked_ = false;
  }

  // The wait currently in force, for a log line that tells the pilot when the
  // plugin will try again rather than leaving the silence unexplained.
  [[nodiscard]] std::uint64_t delay_ms() const { return delay_ms_; }

  // Milliseconds remaining before the next attempt; zero when ready.
  [[nodiscard]] std::uint64_t remaining_ms(std::uint64_t now_ms) const {
    if (ready(now_ms)) {
      return 0;
    }
    return retry_at_ms_ - now_ms;
  }

private:
  std::uint64_t delay_ms_ = 0;
  std::uint64_t retry_at_ms_ = 0;
  bool blocked_ = false;
};

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_RECONNECT_GATE_HPP
