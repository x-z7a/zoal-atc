#ifndef ZOAL_ATC_TRAFFIC_GROUND_FIT_HPP
#define ZOAL_ATC_TRAFFIC_GROUND_FIT_HPP

namespace zoal_atc::traffic {

// Puts an aircraft the console says is on the ground on X-Plane's ground.
//
// The console's ground is a field elevation from navdata; X-Plane's is its
// terrain mesh, metres away and sloping. XPMP2's clamp only lifts a model out
// of the ground, so a console ground above the mesh leaves the model floating.
// GroundFit carries a vertical correction: on the ground it is whatever puts
// the wheels on the terrain; airborne it is the last ground correction,
// fading out over the first `fade_height_m` of height, so a liftoff does not
// jump. The correction moves at most `rate_mps`, so a touchdown settles
// rather than snaps. The result is never below the terrain.
//
// One per drawn aircraft, on the main thread; heights are X-Plane local y in
// metres (up), the terrain already including the model's wheel offset.
class GroundFit {
public:
  struct Config {
    double rate_mps = 3.0;
    double fade_height_m = 60.0;
  };

  GroundFit() = default;
  explicit GroundFit(Config config) : config_(config) {}

  // The height to draw at, for the console's height `drawn_y`, the terrain
  // `terrain_y` under it, `dt_s` seconds after the last call. The first call
  // takes the correction it wants at once.
  double update(double drawn_y, double terrain_y, bool on_ground, double dt_s);

  [[nodiscard]] double offset() const { return offset_; }

private:
  Config config_;
  bool started_ = false;
  double offset_ = 0.0;        // applied now
  double ground_offset_ = 0.0; // last wanted on the ground
};

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_GROUND_FIT_HPP
