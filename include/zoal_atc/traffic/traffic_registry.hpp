#ifndef ZOAL_ATC_TRAFFIC_TRAFFIC_REGISTRY_HPP
#define ZOAL_ATC_TRAFFIC_TRAFFIC_REGISTRY_HPP

#include "zoal_atc/traffic/clock_sync.hpp"
#include "zoal_atc/traffic/target.hpp"
#include "zoal_atc/traffic/track_smoother.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace zoal_atc::traffic {

// TrafficRegistry is the plugin's whole picture of other aircraft: one smoother
// per console track, plus the clock mapping that says what "now" is on the
// console. It tells the renderer which aircraft appeared, changed model or
// left, and answers where each one should be drawn this frame.
//
// SDK-free and single-threaded: the simulator's flight loop owns it, and the
// network thread hands it parsed frames through a queue.
class TrafficRegistry {
public:
  struct Config {
    // How far behind the console's newest report to draw. Two report
    // intervals at 1 Hz keeps almost every frame inside an interval.
    double interpolation_delay_s = 1.5;
    // A track silent this long is removed even if the console never said so.
    double stale_after_s = 15.0;
    TrackSmoother::Config smoother;
  };

  struct Changes {
    std::vector<Identity> added;
    std::vector<Identity> remodelled; // type/airline/livery changed
    std::vector<Identity> relabelled; // callsign changed only
    std::vector<std::string> removed;
    [[nodiscard]] bool empty() const {
      return added.empty() && remodelled.empty() && relabelled.empty() &&
             removed.empty();
    }
  };

  TrafficRegistry() = default;
  explicit TrafficRegistry(Config config) : config_(config) {}

  // Never render this track (our own aircraft, echoed back). Optional: the
  // console is not supposed to send it.
  void set_own_id(std::string id) { own_id_ = std::move(id); }

  Changes apply(const TrafficFrame &frame, double local_time_s);
  Changes expire(double local_time_s);

  // Draw time on the console clock for local time `local_time_s`, for a
  // track that named no delay of its own.
  [[nodiscard]] double render_time(double local_time_s) const;

  [[nodiscard]] std::optional<Sample> state(const std::string &id,
                                            double local_time_s) const;

  void for_each(const std::function<void(const Identity &)> &fn) const;

  [[nodiscard]] std::size_t size() const { return tracks_.size(); }
  [[nodiscard]] const ClockSync &clock() const { return clock_; }

  // Forget everything, e.g. on disconnect. Returns the ids that were known.
  std::vector<std::string> clear();

private:
  struct Track {
    Identity identity;
    TrackSmoother smoother;
    double last_heard_local_s = 0.0;
    // The delay the console named for it, kept until it names another.
    std::optional<double> render_delay_s;
  };

  [[nodiscard]] double render_time(const Track &track,
                                   double local_time_s) const;

  Config config_;
  ClockSync clock_;
  std::string own_id_;
  std::map<std::string, Track> tracks_;
};

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_TRAFFIC_REGISTRY_HPP
