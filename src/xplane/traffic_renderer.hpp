#ifndef ZOAL_ATC_XPLANE_TRAFFIC_RENDERER_HPP
#define ZOAL_ATC_XPLANE_TRAFFIC_RENDERER_HPP

#include "zoal_atc/traffic/traffic_registry.hpp"

#include <map>
#include <cstdint>
#include <memory>
#include <set>
#include <string>

namespace zoal_atc::xplane {

class ConsoleAircraft;

// TrafficRenderer keeps one XPMP2 aircraft per registry track. It applies the
// registry's add/remodel/relabel/remove changes; XPMP2 then calls each
// aircraft's UpdatePosition every frame, which asks the registry where to be.
class TrafficRenderer {
public:
  explicit TrafficRenderer(const traffic::TrafficRegistry &registry);
  ~TrafficRenderer();

  void apply(const traffic::TrafficRegistry::Changes &changes);
  void remove_all();

  [[nodiscard]] std::size_t size() const { return aircraft_.size(); }

  // The 24-bit addresses XPMP2 publishes our aircraft under on TCAS.
  [[nodiscard]] std::set<std::uint32_t> mode_s_ids() const;

private:
  const traffic::TrafficRegistry &registry_;
  std::map<std::string, std::unique_ptr<ConsoleAircraft>> aircraft_;
};

} // namespace zoal_atc::xplane

#endif // ZOAL_ATC_XPLANE_TRAFFIC_RENDERER_HPP
