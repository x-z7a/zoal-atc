#ifndef ZOAL_ATC_TRAFFIC_CLOCK_SYNC_HPP
#define ZOAL_ATC_TRAFFIC_CLOCK_SYNC_HPP

namespace zoal_atc::traffic {

// This machine's monotonic clock in seconds: the local time base for
// ClockSync, frame receive stamps and the renderer.
double monotonic_seconds();

// ClockSync maps the console's clock onto this machine's monotonic clock.
//
// Every frame gives one observation: offset = local_receive - server_send.
// That is the true clock offset plus a one-way network delay that is never
// negative, so the smallest offset seen is the best estimate. Taking the
// minimum alone would let one lucky packet pin it forever and never follow a
// drifting clock, so the estimate creeps upward slowly (kDriftPerSecond) and
// any observation below it pulls it straight back down.
class ClockSync {
public:
  static constexpr double kDriftPerSecond = 0.002; // 2 ms of slack per second

  void observe(double server_time_s, double local_time_s);

  [[nodiscard]] bool synced() const { return synced_; }

  // Console time that corresponds to `local_time_s`.
  [[nodiscard]] double server_time(double local_time_s) const {
    return local_time_s - offset_;
  }
  [[nodiscard]] double offset() const { return offset_; }

  void reset() { synced_ = false; }

private:
  bool synced_ = false;
  double offset_ = 0.0;
  double last_local_ = 0.0;
};

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_CLOCK_SYNC_HPP
