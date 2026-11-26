#ifndef ZOAL_ATC_TRAFFIC_SCREEN_PROJECTION_HPP
#define ZOAL_ATC_TRAFFIC_SCREEN_PROJECTION_HPP

#include <optional>

namespace zoal_atc::traffic {

// Where a point in X-Plane's local frame lands on the screen, for the traffic
// labels. The matrices are the sim's own (sim/graphics/view/world_matrix and
// projection_matrix_3d), column-major as OpenGL keeps them. SDK-free.

struct ScreenPoint {
  double x = 0.0;     // 0 at the left edge of the 3-D view, 1 at the right
  double y = 0.0;     // 0 at the bottom, 1 at the top
  double depth = 0.0; // normalised device depth; larger is farther
};

// nullopt when the point is behind the camera (clip w <= 0) or outside the
// depth range: [0, 1] when `zero_to_one_depth` (Vulkan/Metal), else [-1, 1]
// (OpenGL). A point off the sides is still returned, with x or y outside
// [0, 1]: the caller decides how far off-screen is too far.
std::optional<ScreenPoint> project_to_screen(const float world[16],
                                             const float projection[16],
                                             double x, double y, double z,
                                             bool zero_to_one_depth);

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_SCREEN_PROJECTION_HPP
