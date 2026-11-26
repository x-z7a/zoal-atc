// Accuracy of TrackSmoother against a known trajectory sampled the way a real
// feed delivers it: ~1 Hz, jittered, occasionally reordered or dropped.
#include "test_support.hpp"

#include "zoal_atc/traffic/track_smoother.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

namespace {
using namespace zoal_atc::traffic;

const double kPi = 3.14159265358979323846;

Sample truth_circle(double t) {
  const double R = 3000.0;           // meters
  const double V = 120.0;            // m/s
  const double w = V / R;            // rad/s
  const double a = w * t;            // angle
  const double x = R * sin(a);       // east (m)
  const double y = R * cos(a);       // north (m)
  
  Sample s;
  s.t_s = t;
  s.lat_deg = 37.0 + y / 111132.0;
  s.lon_deg = -122.0 + x / (111320.0 * cos(37.0 * kPi / 180.0));
  s.alt_ft_msl = 5000.0 + 1000.0 * t / 60.0;
  s.heading_deg = fmod(a * 180.0 / kPi + 90.0, 360.0);
  s.track_deg = s.heading_deg;
  s.groundspeed_kts = V / 0.514444;
  s.vertical_speed_fpm = 1000.0;
  s.pitch_deg = 0.0;
  s.roll_deg = 20.0;
  s.on_ground = false;
  s.gear = 1.0;
  s.flaps = 0.0;
  s.speedbrake = 0.0;
  s.thrust = 1.0;
  s.lights = 0;
  
  return s;
}

double error_m(const Sample& a, const Sample& b) {
  const double dy = (a.lat_deg - b.lat_deg) * 111132.0;
  const double dx = (a.lon_deg - b.lon_deg) * 111320.0 * cos(37.0 * kPi / 180.0);
  return sqrt(dx * dx + dy * dy);
}

void clean_feed_is_accurate() {
  TrackSmoother::Config config;
  config.max_samples = 128;
  TrackSmoother smoother(config);
  
  // Add truth_circle(t) for t = 0,1,2,...,60
  for (int i = 0; i <= 60; ++i) {
    smoother.add(truth_circle(static_cast<double>(i)));
  }
  
  double max_horizontal_error = 0.0;
  double max_alt_error = 0.0;
  
  // For render times t from 1.0 to 59.0 in steps of 0.05
  for (int i = 0; i < 1180; ++i) {  // 59 - 1 = 58 seconds, step 0.05 => 1160 steps + 20 more for safety
    double t = 1.0 + i * 0.05;
    auto result = smoother.evaluate(t);
    test::require(result.has_value(), "clean feed: evaluation failed");
    
    Sample truth = truth_circle(t);
    double horizontal_error = error_m(*result, truth);
    double alt_error = std::abs(result->alt_ft_msl - truth.alt_ft_msl);
    
    max_horizontal_error = std::max(max_horizontal_error, horizontal_error);
    max_alt_error = std::max(max_alt_error, alt_error);
  }
  
  test::require(max_horizontal_error < 5.0, "clean feed: horizontal error too large");
  test::require(max_alt_error < 2.0, "clean feed: altitude error too large");
  
  std::cout << "clean feed: max horizontal " << max_horizontal_error 
            << " m, max alt " << max_alt_error << " ft\n";
}

void jittered_reordered_feed_is_smooth() {
  TrackSmoother::Config config;
  config.max_samples = 128;
  TrackSmoother smoother(config);
  
  std::mt19937 rng(1234);
  std::uniform_real_distribution<double> jitter(-0.15, 0.15);
  std::bernoulli_distribution drop(0.1);
  std::uniform_real_distribution<double> swap_prob(0.0, 1.0);
  
  // Generate report times with jitter
  std::vector<double> times;
  for (int k = 0; k <= 60; ++k) {
    if (k == 0 || k == 60) {
      times.push_back(static_cast<double>(k));
    } else {
      double t_k = k + jitter(rng);
      if (!drop(rng)) {
        times.push_back(t_k);
      }
    }
  }
  
  // Shuffle adjacent pairs with probability 0.2
  for (size_t i = 0; i < times.size() - 1; i += 2) {
    if (swap_prob(rng) < 0.2) {
      std::swap(times[i], times[i+1]);
    }
  }
  
  // Add all with the sample being truth_circle(t_k)
  for (double t : times) {
    smoother.add(truth_circle(t));
  }
  
  double max_horizontal_error = 0.0;
  double max_step_distance = 0.0;
  
  // For render times 2.0..58.0 step 0.02
  double prev_lat = 0.0;
  double prev_lon = 0.0;
  bool first = true;
  
  for (int i = 0; i < 2800; ++i) {  // (58 - 2) / 0.02 = 2800 steps
    double t = 2.0 + i * 0.02;
    auto result = smoother.evaluate(t);
    test::require(result.has_value(), "jittered feed: evaluation failed");
    
    Sample truth = truth_circle(t);
    double horizontal_error = error_m(*result, truth);
    max_horizontal_error = std::max(max_horizontal_error, horizontal_error);
    
    if (!first) {
      double step_distance = error_m(*result, {0.0, prev_lat, prev_lon, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false, 0.0, 0.0, 0.0, 0.0, 0});
      max_step_distance = std::max(max_step_distance, step_distance);
    }
    
    prev_lat = result->lat_deg;
    prev_lon = result->lon_deg;
    first = false;
  }
  
  test::require(max_horizontal_error < 25.0, "jittered feed: horizontal error too large");
  test::require(max_step_distance < 4.8, "jittered feed: visible jump");
  
  std::cout << "jittered feed: max horizontal " << max_horizontal_error 
            << " m, max step " << max_step_distance << " m\n";
}

void outage_extrapolates_then_holds() {
  TrackSmoother::Config config;
  config.max_samples = 128;
  config.max_extrapolation_s = 3.0;
  TrackSmoother smoother(config);
  
  // Add truth_circle(t) for t = 0..10 only
  for (int i = 0; i <= 10; ++i) {
    smoother.add(truth_circle(static_cast<double>(i)));
  }
  
  // Evaluate at t = 11.0 and require error vs truth_circle(11.0) < 150.0 m
  auto result11 = smoother.evaluate(11.0);
  test::require(result11.has_value(), "outage: evaluation at 11.0 failed");
  Sample truth11 = truth_circle(11.0);
  double error11 = error_m(*result11, truth11);
  test::require(error11 < 150.0, "outage: extrapolation error too large at t=11.0");
  
  // Evaluate at 13.0 and 30.0: require the two positions equal within 1e-9 degrees lat and lon
  auto result13 = smoother.evaluate(13.0);
  test::require(result13.has_value(), "outage: evaluation at 13.0 failed");
  
  auto result30 = smoother.evaluate(30.0);
  test::require(result30.has_value(), "outage: evaluation at 30.0 failed");
  
  test::near(result13->lat_deg, result30->lat_deg, 1e-9, "outage: did not hold after the bound");
  test::near(result13->lon_deg, result30->lon_deg, 1e-9, "outage: did not hold after the bound");
  
  std::cout << "outage: extrapolation and hold working correctly\n";
}

} // anonymous namespace

int main() {
  clean_feed_is_accurate();
  jittered_reordered_feed_is_smooth();
  outage_extrapolates_then_holds();
  
  std::cout << "track_smoother_accuracy_test: ok\n";
  return 0;
}
