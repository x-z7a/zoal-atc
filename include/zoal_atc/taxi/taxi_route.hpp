#ifndef ZOAL_ATC_TAXI_TAXI_ROUTE_HPP
#define ZOAL_ATC_TAXI_TAXI_ROUTE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The taxi_route frame: the route Ground cleared this aircraft along, once the
// pilot read it back, for the cockpit to light as green lead-on lights with a
// red stop bar at the hold short (docs/taxi-route-protocol.md). The plugin
// only draws it: the console decides the route, where it stops and when the
// lights go out.
namespace zoal_atc::taxi {

inline constexpr const char *kTaxiRouteCapability = "taxi_route_v1";
inline constexpr const char *kTaxiRouteFrameType = "taxi_route";

struct RoutePoint {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  // The runway the aircraft stops before here: a stop bar. Empty for none.
  std::string hold_short;
};

struct TaxiRoute {
  // The station's state version, so an older frame from the same station
  // arriving late is ignored.
  std::uint64_t rev = 0;
  std::string station;
  std::string route_id;
  std::string airport;
  bool destination_runway = false;
  std::vector<RoutePoint> points;
  std::vector<std::string> taxiways;
  std::string destination;
  // The scenery the console drew the route from: a custom scenery package's
  // folder name, or "Global Airports".
  std::string scenery;

  // No points: the station's lights go out.
  bool dark() const { return points.empty(); }
};

// nullopt when json is not a taxi_route frame. Points outside the globe are
// dropped; a frame with one usable point left is dark.
std::optional<TaxiRoute> parse_taxi_route(const std::string &json);

// The route being lit, kept from the frames as they arrive.
//
// A lit route from any station replaces the one shown, except one from the
// same station with an older rev. A dark frame puts the lights out only when
// it comes from the station whose route is shown, with a rev no older: Ground
// at one position finishing with an aircraft says nothing about the route
// another position gave it.
class RouteBook {
public:
  // True when what should be lit changed: a different route, or none.
  bool apply(const TaxiRoute &frame);
  const std::optional<TaxiRoute> &current() const { return current_; }
  void clear() { current_.reset(); }

private:
  std::optional<TaxiRoute> current_;
};

} // namespace zoal_atc::taxi

#endif // ZOAL_ATC_TAXI_TAXI_ROUTE_HPP
