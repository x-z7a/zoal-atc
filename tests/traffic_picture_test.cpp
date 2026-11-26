#include "test_support.hpp"
#include "zoal_atc/traffic/traffic_picture.hpp"
#include "zoal_atc/traffic/traffic_registry.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace zoal_atc::traffic;

namespace {
Target at(const std::string &id, TrafficKind kind, double lat, double lon, double alt_ft) {
  Target t;
  t.identity.id = id;
  t.identity.kind = kind;
  t.identity.callsign = id;
  t.identity.icao_type = "B738";
  t.sample.t_s = 100;
  t.sample.lat_deg = lat;
  t.sample.lon_deg = lon;
  t.sample.alt_ft_msl = alt_ft;
  return t;
}

TrafficRegistry registry_with(std::vector<Target> targets) {
  TrafficRegistry r;
  TrafficFrame f;
  f.server_time_s = 100;
  f.targets = targets;
  r.apply(f, 1000.0);
  return r;
}
} // anonymous namespace

void distance_one_degree_of_longitude_at_the_equator() {
  test::near(distance_nm(0, 0, 0, 1), 60.04, 0.05, "distance_one_degree_of_longitude_at_the_equator");
}

void distance_same_point_is_zero() {
  test::near(distance_nm(45, -75, 45, -75), 0, 1e-9, "distance_same_point_is_zero");
}

void distance_cyow_to_cyyz() {
  test::near(distance_nm(45.3225, -75.6692, 43.6772, -79.6306), 196.0, 2.0, "distance_cyow_to_cyyz");
}

void bearing_cardinal_points() {
  test::near(bearing_deg(0, 0, 1, 0), 0, 1e-6, "bearing_deg(0,0,1,0)");
  test::near(bearing_deg(0, 0, 0, 1), 90, 1e-6, "bearing_deg(0,0,0,1)");
  test::near(bearing_deg(0, 0, -1, 0), 180, 1e-6, "bearing_deg(0,0,-1,0)");
  test::near(bearing_deg(0, 0, 0, -1), 270, 1e-6, "bearing_deg(0,0,0,-1)");
  test::require(bearing_deg(10, 10, 9, 9) >= 0 && bearing_deg(10, 10, 9, 9) < 360, "bearing never negative");
}

void nearby_sorts_nearest_first() {
  OwnPosition own;
  own.lat_deg = 45;
  own.lon_deg = -75;
  own.alt_ft_msl = 5000;
  own.valid = true;

  std::vector<Target> targets;
  targets.push_back(at("ai:FAR", TrafficKind::Ai, 46, -75, 5000));
  targets.push_back(at("ai:NEAR", TrafficKind::Ai, 45.1, -75, 3000));
  targets.push_back(at("participant:mid", TrafficKind::Participant, 45.5, -75, 9000));

  auto r = registry_with(targets);
  auto result = nearby(r, 1000.0, own);

  test::require(result.size() == 3, "nearby_sorts_nearest_first size");
  test::require(result[0].identity.id == "ai:NEAR", "nearby_sorts_nearest_first order NEAR");
  test::require(result[1].identity.id == "participant:mid", "nearby_sorts_nearest_first order mid");
  test::require(result[2].identity.id == "ai:FAR", "nearby_sorts_nearest_first order FAR");

  test::require(result[0].has_own == true, "nearby_sorts_nearest_first has_own");
  test::near(result[0].distance_nm, 6.0, 0.1, "nearby_sorts_nearest_first distance");
  test::near(result[0].bearing_deg, 0, 0.01, "nearby_sorts_nearest_first bearing");
  test::near(result[0].relative_alt_ft, -2000, 1e-6, "nearby_sorts_nearest_first relative_alt");

  test::near(result[1].relative_alt_ft, 4000, 1e-6, "nearby_sorts_nearest_first mid relative_alt");
}

void nearby_without_own_sorts_by_id() {
  OwnPosition own;
  own.valid = false;

  std::vector<Target> targets;
  targets.push_back(at("ai:FAR", TrafficKind::Ai, 46, -75, 5000));
  targets.push_back(at("ai:NEAR", TrafficKind::Ai, 45.1, -75, 3000));
  targets.push_back(at("participant:mid", TrafficKind::Participant, 45.5, -75, 9000));

  auto r = registry_with(targets);
  auto result = nearby(r, 1000.0, own);

  test::require(result.size() == 3, "nearby_without_own_sorts_by_id size");
  test::require(result[0].identity.id == "ai:FAR", "nearby_without_own_sorts_by_id order FAR");
  test::require(result[1].identity.id == "ai:NEAR", "nearby_without_own_sorts_by_id order NEAR");
  test::require(result[2].identity.id == "participant:mid", "nearby_without_own_sorts_by_id order mid");

  for (const auto &n : result) {
    test::require(n.has_own == false, "nearby_without_own_sorts_by_id has_own false");
    test::near(n.distance_nm, 0, 1e-9, "nearby_without_own_sorts_by_id distance zero");
  }
}

void nearby_empty_registry() {
  TrafficRegistry r;
  OwnPosition own;
  own.valid = true;
  test::require(nearby(r, 1000.0, own).empty(), "nearby_empty_registry empty");
}

void chase_behind_north_heading() {
  ChaseOffset offset;
  offset.distance_m = 60;
  offset.height_m = 0;
  offset.orbit_deg = 0;

  CameraPose p = chase_pose(0, 0, 0, 0, offset);
  test::near(p.x, 0, 1e-9, "chase_behind_north_heading x");
  test::near(p.y, 0, 1e-9, "chase_behind_north_heading y");
  test::near(p.z, 60, 1e-9, "chase_behind_north_heading z");
  test::near(p.heading_deg, 0, 1e-9, "chase_behind_north_heading heading");
  test::near(p.pitch_deg, 0, 1e-9, "chase_behind_north_heading pitch");
}

void chase_behind_east_heading() {
  ChaseOffset offset;
  offset.distance_m = 60;
  offset.height_m = 0;
  offset.orbit_deg = 0;

  CameraPose p = chase_pose(0, 0, 0, 90, offset);
  test::near(p.x, -60, 1e-9, "chase_behind_east_heading x");
  test::near(p.z, 0, 1e-9, "chase_behind_east_heading z");
  test::near(p.heading_deg, 90, 1e-9, "chase_behind_east_heading heading");
}

void chase_orbit_half_turn_faces_the_nose() {
  ChaseOffset offset;
  offset.distance_m = 60;
  offset.height_m = 0;
  offset.orbit_deg = 180;

  CameraPose p = chase_pose(10, 5, 10, 0, offset);
  test::near(p.x, 10, 1e-9, "chase_orbit_half_turn_faces_the_nose x");
  test::near(p.y, 5, 1e-9, "chase_orbit_half_turn_faces_the_nose y");
  test::near(p.z, -50, 1e-9, "chase_orbit_half_turn_faces_the_nose z");
  test::near(p.heading_deg, 180, 1e-9, "chase_orbit_half_turn_faces_the_nose heading");
}

void chase_heading_wraps() {
  ChaseOffset offset;
  offset.distance_m = 60;
  offset.height_m = 10;
  offset.orbit_deg = 20;

  CameraPose p = chase_pose(0, 0, 0, 350, offset);
  test::near(p.heading_deg, 10, 1e-9, "chase_heading_wraps heading");
  test::require(p.pitch_deg < 0, "chase_heading_wraps pitch negative");
  test::near(p.y, 10, 1e-9, "chase_heading_wraps y");
}

void chase_distance_is_clamped() {
  ChaseOffset offset;
  offset.distance_m = 1;
  offset.height_m = 0;
  offset.orbit_deg = 0;

  CameraPose p = chase_pose(0, 0, 0, 0, offset);
  test::near(p.z, kChaseMinDistanceM, 1e-9, "chase_distance_is_clamped min distance");
  
  offset.distance_m = 1e6;
  p = chase_pose(0, 0, 0, 0, offset);
  test::near(p.z, kChaseMaxDistanceM, 1e-9, "chase_distance_is_clamped max distance");
}

int main() {
  distance_one_degree_of_longitude_at_the_equator();
  distance_same_point_is_zero();
  distance_cyow_to_cyyz();
  bearing_cardinal_points();
  nearby_sorts_nearest_first();
  nearby_without_own_sorts_by_id();
  nearby_empty_registry();
  chase_behind_north_heading();
  chase_behind_east_heading();
  chase_orbit_half_turn_faces_the_nose();
  chase_heading_wraps();
  chase_distance_is_clamped();

  std::cout << "traffic picture tests passed\n";
  return 0;
}
