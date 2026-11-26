#include "zoal_atc/traffic/screen_projection.hpp"
#include <optional>

namespace {
void multiply(double out[4], const float m[16], const double v[4]) {
  for (int row = 0; row < 4; ++row)
    out[row] = v[0]*static_cast<double>(m[row]) + 
               v[1]*static_cast<double>(m[4+row]) + 
               v[2]*static_cast<double>(m[8+row]) + 
               v[3]*static_cast<double>(m[12+row]);
}
}

namespace zoal_atc::traffic {

std::optional<ScreenPoint> project_to_screen(const float world[16],
                                             const float projection[16],
                                             double x, double y, double z,
                                             bool zero_to_one_depth) {
  double eye[4];
  double point[4] = {x, y, z, 1.0};
  multiply(eye, world, point);
  
  double clip[4];
  multiply(clip, projection, eye);
  
  if (clip[3] <= 1e-9) {
    return std::nullopt;
  }
  
  double ndc_x = clip[0] / clip[3];
  double ndc_y = clip[1] / clip[3];
  double ndc_z = clip[2] / clip[3];
  
  if (zero_to_one_depth) {
    if (ndc_z < 0.0 || ndc_z > 1.0) {
      return std::nullopt;
    }
  } else {
    if (ndc_z < -1.0 || ndc_z > 1.0) {
      return std::nullopt;
    }
  }
  
  ScreenPoint result;
  result.x = ndc_x * 0.5 + 0.5;
  result.y = ndc_y * 0.5 + 0.5;
  result.depth = ndc_z;
  
  return result;
}

} // namespace zoal_atc::traffic
