#include "zoal_atc/traffic/clock_sync.hpp"

#include <algorithm>
#include <chrono>

namespace zoal_atc::traffic {

double monotonic_seconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void ClockSync::observe(double server_time_s, double local_time_s) {
  const double sample = local_time_s - server_time_s;
  if (!synced_) {
    offset_ = sample;
    last_local_ = local_time_s;
    synced_ = true;
    return;
  }
  const double elapsed = std::max(0.0, local_time_s - last_local_);
  last_local_ = local_time_s;
  offset_ = std::min(offset_ + elapsed * kDriftPerSecond, sample);
}

} // namespace zoal_atc::traffic
