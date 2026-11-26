#include "zoal_atc/traffic/traffic_registry.hpp"

namespace zoal_atc::traffic {

TrafficRegistry::Changes TrafficRegistry::apply(const TrafficFrame &frame,
                                                double local_time_s) {
  Changes changes;
  if (frame.server_time_s > 0.0) {
    clock_.observe(frame.server_time_s, local_time_s);
  }

  for (const auto &target : frame.targets) {
    const Identity &who = target.identity;
    if (who.id.empty() || who.id == own_id_) {
      continue;
    }
    auto found = tracks_.find(who.id);
    if (found == tracks_.end()) {
      Track track{who, TrackSmoother(config_.smoother), local_time_s};
      track.smoother.add(target.sample);
      tracks_.emplace(who.id, std::move(track));
      changes.added.push_back(who);
      continue;
    }
    Track &track = found->second;
    if (!track.identity.same_model(who)) {
      changes.remodelled.push_back(who);
    } else if (track.identity.callsign != who.callsign) {
      changes.relabelled.push_back(who);
    }
    track.identity = who;
    track.smoother.add(target.sample);
    track.last_heard_local_s = local_time_s;
  }

  for (const auto &id : frame.removed) {
    if (tracks_.erase(id) > 0) {
      changes.removed.push_back(id);
    }
  }
  return changes;
}

TrafficRegistry::Changes TrafficRegistry::expire(double local_time_s) {
  Changes changes;
  const double render = render_time(local_time_s);
  for (auto it = tracks_.begin(); it != tracks_.end();) {
    if (local_time_s - it->second.last_heard_local_s > config_.stale_after_s) {
      changes.removed.push_back(it->first);
      it = tracks_.erase(it);
      continue;
    }
    it->second.smoother.prune_before(render);
    ++it;
  }
  return changes;
}

double TrafficRegistry::render_time(double local_time_s) const {
  return clock_.server_time(local_time_s) - config_.interpolation_delay_s;
}

std::optional<Sample> TrafficRegistry::state(const std::string &id,
                                             double local_time_s) const {
  auto found = tracks_.find(id);
  if (found == tracks_.end() || !clock_.synced()) {
    return std::nullopt;
  }
  return found->second.smoother.evaluate(render_time(local_time_s));
}

void TrafficRegistry::for_each(
    const std::function<void(const Identity &)> &fn) const {
  for (const auto &[id, track] : tracks_) {
    fn(track.identity);
  }
}

std::vector<std::string> TrafficRegistry::clear() {
  std::vector<std::string> ids;
  ids.reserve(tracks_.size());
  for (const auto &[id, track] : tracks_) {
    ids.push_back(id);
  }
  tracks_.clear();
  clock_.reset();
  return ids;
}

} // namespace zoal_atc::traffic
