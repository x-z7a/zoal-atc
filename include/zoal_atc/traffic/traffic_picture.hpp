#ifndef ZOAL_ATC_TRAFFIC_TRAFFIC_PICTURE_HPP
#define ZOAL_ATC_TRAFFIC_TRAFFIC_PICTURE_HPP

#include "zoal_atc/traffic/target.hpp"
#include "zoal_atc/traffic/traffic_registry.hpp"

#include <vector>

namespace zoal_atc::traffic {

// The console's aircraft as the pilot sees them from the cockpit: where each
// one is drawn this frame, and how far and which way it is from own-ship.
// This is what the panel's Traffic tab lists and what the follow camera
// chases. SDK-free; the simulator shell supplies own-ship and local coords.

// Own-ship, from the simulator. `valid` is false until the first read.
struct OwnPosition {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double alt_ft_msl = 0.0;
  bool valid = false;
};

// One drawn aircraft relative to own-ship.
struct Nearby {
  Identity identity;
  Sample sample;              // the state drawn this frame
  bool has_own = false;       // false: the three fields below are zero
  double distance_nm = 0.0;   // great-circle distance from own-ship
  double bearing_deg = 0.0;   // true bearing from own-ship, [0, 360)
  double relative_alt_ft = 0.0; // sample.alt_ft_msl - own.alt_ft_msl
};

// Mean Earth radius in nautical miles, for distance_nm.
inline constexpr double kEarthRadiusNm = 3440.065;

// Great-circle distance in nm (haversine).
double distance_nm(double lat1_deg, double lon1_deg, double lat2_deg,
                   double lon2_deg);
// Initial true bearing from point 1 to point 2, normalised to [0, 360).
double bearing_deg(double lat1_deg, double lon1_deg, double lat2_deg,
                   double lon2_deg);

// Every track the registry can draw at `local_time_s` (tracks with no state
// yet are left out), nearest first. Ties, and the whole order when own-ship
// is not valid, go by identity.id ascending.
std::vector<Nearby> nearby(const TrafficRegistry &registry, double local_time_s,
                           const OwnPosition &own);

// --- the follow camera -------------------------------------------------------

// Where the chase camera sits relative to the aircraft it follows.
struct ChaseOffset {
  double distance_m = 60.0; // behind the aircraft, along the ground
  double height_m = 12.0;   // above it
  double orbit_deg = 0.0;   // swung clockwise round it; 180 looks at the nose
};

inline constexpr double kChaseMinDistanceM = 15.0;
inline constexpr double kChaseMaxDistanceM = 2000.0;

// A camera in X-Plane's local OpenGL frame: +x east, +y up, -z north.
// heading_deg is true, clockwise from north; pitch_deg is negative looking
// down.
struct CameraPose {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double heading_deg = 0.0;
  double pitch_deg = 0.0;
};

// The camera looking at a target at local (x, y, z) whose nose points
// `target_heading_deg`, from `offset`. Distance is clamped to
// [kChaseMinDistanceM, kChaseMaxDistanceM].
CameraPose chase_pose(double target_x, double target_y, double target_z,
                      double target_heading_deg, const ChaseOffset &offset);

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_TRAFFIC_PICTURE_HPP
