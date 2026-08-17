#include "zoal_atc/transport/bounded_queue.hpp"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>

using zoal_atc::transport::push_bounded;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void under_the_cap_nothing_is_discarded() {
  std::deque<int> queue;
  for (int i = 0; i < 4; ++i) {
    require(push_bounded(queue, i, 4) == 0, "discarded below the cap");
  }
  require(queue.size() == 4, "queue did not hold everything under the cap");
  require(queue.front() == 0 && queue.back() == 3, "order was not preserved");
}

// The console being away must cost a fixed amount of memory, not a growing one.
void the_cap_holds() {
  std::deque<int> queue;
  for (int i = 0; i < 1000; ++i) {
    push_bounded(queue, i, 8);
  }
  require(queue.size() == 8, "queue grew past the cap");
}

// The stale end is the worthless end: what survives is the newest, so a
// reconnect resumes from roughly now instead of replaying the whole outage.
void the_oldest_is_what_goes() {
  std::deque<int> queue;
  for (int i = 0; i < 10; ++i) {
    push_bounded(queue, i, 3);
  }
  require(queue.size() == 3, "cap not held");
  require(queue.front() == 7, "oldest survivor was wrong");
  require(queue.back() == 9, "newest message was not kept");
}

void discards_are_counted() {
  std::deque<int> queue;
  std::size_t total = 0;
  for (int i = 0; i < 10; ++i) {
    total += push_bounded(queue, i, 3);
  }
  require(total == 7, "discard count was wrong");
}

// A zero cap accepts nothing and must say so, rather than looping forever
// trying to make room it can never have.
void a_zero_cap_accepts_nothing() {
  std::deque<int> queue;
  require(push_bounded(queue, 1, 0) == 1, "a zero cap did not report a drop");
  require(queue.empty(), "a zero cap accepted a message");
}

} // namespace

int main() {
  under_the_cap_nothing_is_discarded();
  the_cap_holds();
  the_oldest_is_what_goes();
  discards_are_counted();
  a_zero_cap_accepts_nothing();
  std::cout << "bounded queue tests passed\n";
  return 0;
}
