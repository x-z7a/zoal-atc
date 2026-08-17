#include "zoal_atc/transport/reconnect_gate.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

using zoal_atc::transport::ReconnectGate;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

// The first dial of a session must not be delayed: the pilot has just loaded
// the aircraft and the console is usually right there.
void first_attempt_is_immediate() {
  ReconnectGate gate;
  require(gate.ready(0), "a fresh gate refused the first dial");
  require(gate.ready(1'000'000), "a fresh gate refused a later first dial");
}

// Reported in flight, 2026-08-17: with the console stopped, the plugin re-dialled
// on every queued frame and every receive, and the sim lagged massively.
void a_failure_holds_off_the_next_attempt() {
  ReconnectGate gate;
  gate.note_failure(1000);

  require(!gate.ready(1000), "re-dialled immediately after a failure");
  require(!gate.ready(5999), "re-dialled before the five second wait elapsed");
  require(gate.ready(6000), "never re-dialled after the wait elapsed");
  require(gate.delay_ms() == ReconnectGate::kInitialDelayMs,
          "first backoff was not five seconds");
}

// A console that is gone for an hour must not be dialled seven hundred times.
void repeated_failures_back_off_to_a_cap() {
  ReconnectGate gate;
  std::uint64_t now = 0;
  std::uint64_t previous = 0;
  for (int i = 0; i < 12; ++i) {
    gate.note_failure(now);
    require(gate.delay_ms() >= previous, "backoff went backwards");
    require(gate.delay_ms() <= ReconnectGate::kMaxDelayMs,
            "backoff grew past the cap");
    previous = gate.delay_ms();
    now += gate.delay_ms();
  }
  require(previous == ReconnectGate::kMaxDelayMs,
          "backoff never reached the cap");
}

// A console restarted mid-flight has to be picked up promptly the *next* time it
// goes away, rather than inheriting an hour-old delay.
void success_clears_the_backoff() {
  ReconnectGate gate;
  gate.note_failure(0);
  gate.note_failure(30'000);
  gate.note_failure(90'000);
  require(gate.delay_ms() > ReconnectGate::kInitialDelayMs,
          "test did not actually escalate");

  gate.note_success();
  require(gate.ready(90'001), "a connected client was gated");
  require(gate.delay_ms() == 0, "backoff survived a successful connection");

  gate.note_failure(100'000);
  require(gate.delay_ms() == ReconnectGate::kInitialDelayMs,
          "a later outage inherited the old escalated delay");
}

// The wait is reported so a log line can say when the plugin will try again,
// rather than leaving the silence unexplained.
void remaining_time_is_reported() {
  ReconnectGate gate;
  gate.note_failure(1000);

  require(gate.remaining_ms(1000) == ReconnectGate::kInitialDelayMs,
          "remaining wait was wrong at the moment of failure");
  require(gate.remaining_ms(3000) == 3000, "remaining wait did not count down");
  require(gate.remaining_ms(6000) == 0, "a ready gate reported a wait");
  require(gate.remaining_ms(9999) == 0, "a long-ready gate reported a wait");
}

} // namespace

int main() {
  first_attempt_is_immediate();
  a_failure_holds_off_the_next_attempt();
  repeated_failures_back_off_to_a_cap();
  success_clears_the_backoff();
  remaining_time_is_reported();
  std::cout << "reconnect gate tests passed\n";
  return 0;
}
