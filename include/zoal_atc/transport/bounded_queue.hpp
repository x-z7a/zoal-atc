#ifndef ZOAL_ATC_TRANSPORT_BOUNDED_QUEUE_HPP
#define ZOAL_ATC_TRANSPORT_BOUNDED_QUEUE_HPP

#include <cstddef>
#include <deque>
#include <utility>

namespace zoal_atc::transport {

// push_bounded appends value and discards from the front until the queue holds
// at most cap entries. It returns how many were discarded.
//
// Oldest-first, because the queues this guards carry a position feed: when the
// console is not taking frames, the stale end is the worthless end, and the
// pilot is better served by where the aircraft is than by where it was.
//
// A cap of zero accepts nothing, and says so by reporting the incoming message
// as discarded rather than by looping forever trying to make room.
template <typename T>
std::size_t push_bounded(std::deque<T> &queue, T value, std::size_t cap) {
  if (cap == 0) {
    return 1;
  }
  std::size_t discarded = 0;
  while (queue.size() >= cap) {
    queue.pop_front();
    ++discarded;
  }
  queue.push_back(std::move(value));
  return discarded;
}

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_BOUNDED_QUEUE_HPP
