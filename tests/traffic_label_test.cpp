#include "test_support.hpp"
#include "zoal_atc/gui/traffic_label.hpp"
#include "zoal_atc/gui/traffic_panel.hpp"
#include "zoal_atc/gui/i18n.hpp"
#include "zoal_atc/traffic/screen_projection.hpp"
#include <iostream>
#include <string>

using namespace zoal_atc::gui;
using zoal_atc::traffic::Nearby;
using zoal_atc::traffic::TrafficKind;
using zoal_atc::traffic::project_to_screen;

namespace {
void eq(const std::string &got, const std::string &want, const std::string &what) {
  test::require(got == want, what + ": got '" + got + "', want '" + want + "'");
}

void test_detail_by_distance() {
  LabelRange r;
  test::require(label_detail(500, r, false) == LabelDetail::full, "detail_by_distance: 500m");
  test::require(label_detail(5000, r, false) == LabelDetail::compact, "detail_by_distance: 5000m");
  test::require(label_detail(20000, r, false) == LabelDetail::callsign, "detail_by_distance: 20000m");
  test::require(label_detail(20000, r, true) == LabelDetail::full, "detail_by_distance: 20000m tracked");
}

void test_opacity_fades_in_the_last_fifth() {
  LabelRange r;
  test::near(label_opacity(10000, r, false), 1.0, 1e-5, "opacity_fades_in_the_last_fifth: 10000m");
  test::near(label_opacity(30000, r, false), 0.35, 1e-5, "opacity_fades_in_the_last_fifth: 30000m");
  test::near(label_opacity(27000, r, false), 0.675, 1e-5, "opacity_fades_in_the_last_fifth: 27000m");
  test::near(label_opacity(40000, r, true), 1.0, 1e-5, "opacity_fades_in_the_last_fifth: 40000m tracked");
}

void test_shown_within_range_or_tracked() {
  LabelRange r;
  test::require(!label_shown(31000, r, false), "shown_within_range_or_tracked: 31000m not tracked");
  test::require(label_shown(31000, r, true), "shown_within_range_or_tracked: 31000m tracked");
  test::require(label_shown(100, r, false), "shown_within_range_or_tracked: 100m not tracked");
}

void test_airborne_ai_full() {
  LabelRange r;
  Nearby n;
  n.identity.id = "ai:ACA451";
  n.identity.kind = TrafficKind::Ai;
  n.identity.callsign = "ACA451";
  n.identity.icao_type = "A320";
  n.identity.airline = "ACA";
  n.has_own = true;
  n.distance_nm = 14.3;
  n.sample.alt_ft_msl = 24000;
  n.sample.vertical_speed_fpm = 1520;
  n.sample.groundspeed_kts = 410.4;
  n.sample.on_ground = false;

  TrafficLabel l = traffic_label(n, 1000, r, "ai:ACA451", "");
  eq(l.id, "ai:ACA451", "airborne_ai_full: id");
  test::require(!l.pilot, "airborne_ai_full: pilot");
  test::require(l.tracked, "airborne_ai_full: tracked");
  test::require(!l.followed, "airborne_ai_full: followed");
  test::require(l.detail == LabelDetail::full, "airborne_ai_full: detail");
  test::near(l.opacity, 1.0, 1e-6, "airborne_ai_full: opacity");
  eq(l.callsign, "ACA451", "airborne_ai_full: callsign");
  eq(l.type, "A320", "airborne_ai_full: type (the callsign says ACA)");
  eq(l.altitude, "FL240", "airborne_ai_full: altitude");
  eq(l.vertical, "+1,500", "airborne_ai_full: vertical");
  eq(l.speed, "410 kt", "airborne_ai_full: speed");
  eq(l.distance, "14.3 nm", "airborne_ai_full: distance");
  test::require(l.trend == 1, "airborne_ai_full: trend");
}

void test_pilot_on_ground_far() {
  LabelRange r;
  Nearby n;
  n.identity.id = "participant:x";
  n.identity.kind = TrafficKind::Participant;
  n.identity.callsign = "";
  n.identity.icao_type = "C172";
  n.identity.airline = "";
  n.has_own = false;
  n.sample.on_ground = true;
  n.sample.alt_ft_msl = 300;
  n.sample.groundspeed_kts = 12;
  n.sample.vertical_speed_fpm = 500;

  TrafficLabel l = traffic_label(n, 20000, r, "", "participant:x");
  test::require(l.pilot, "pilot_on_ground_far: pilot");
  test::require(l.followed, "pilot_on_ground_far: followed");
  test::require(!l.tracked, "pilot_on_ground_far: tracked");
  test::require(l.detail == LabelDetail::callsign, "pilot_on_ground_far: detail");
  eq(l.callsign, "participant:x", "pilot_on_ground_far: callsign");
  eq(l.type, "C172", "pilot_on_ground_far: type");
  eq(l.altitude, "GND", "pilot_on_ground_far: altitude");
  eq(l.vertical, "", "pilot_on_ground_far: vertical");
  eq(l.distance, "", "pilot_on_ground_far: distance");
  eq(l.speed, "12 kt", "pilot_on_ground_far: speed");
  test::require(l.trend == 0, "pilot_on_ground_far: trend");
}

void test_no_type() {
  LabelRange r;
  Nearby n;
  n.identity.id = "ai:test";
  n.identity.kind = TrafficKind::Ai;
  n.identity.callsign = "TEST";
  n.identity.icao_type = "";
  n.identity.airline = "";
  n.has_own = false;
  n.sample.on_ground = false;
  n.sample.alt_ft_msl = 1000;
  n.sample.groundspeed_kts = 100;
  n.sample.vertical_speed_fpm = 0;

  TrafficLabel l = traffic_label(n, 1000, r, "", "");
  eq(l.type, "", "no_type: type");
}

// The callsign already names the operator, so the tag says where the
// aircraft is going instead, and the operator only where the callsign
// does not say it.
void test_identity_line() {
  zoal_atc::traffic::Identity who;
  who.callsign = "SKW5704";
  who.icao_type = "E75L";
  who.airline = "SKW";
  eq(identity_line(who), "E75L", "identity_line: airline in the callsign");
  who.destination = "KSFO";
  eq(identity_line(who), "E75L \xE2\x86\x92 KSFO", "identity_line: destination");
  who.icao_type = "";
  eq(identity_line(who), "\xE2\x86\x92 KSFO", "identity_line: destination, no type");
  zoal_atc::traffic::Identity reg;
  reg.callsign = "CGABC";
  reg.icao_type = "C172";
  reg.airline = "ACA";
  eq(identity_line(reg), "C172 \xC2\xB7 ACA", "identity_line: a registration flying for an operator");
  reg.icao_type = "";
  eq(identity_line(reg), "ACA", "identity_line: operator only");
}

void test_projection_identity() {
  float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  
  auto p = project_to_screen(I, I, 0, 0, 0, false);
  test::require(p.has_value(), "projection_identity: identity at origin");
  test::near(p->x, 0.5, 1e-9, "projection_identity: x at origin");
  test::near(p->y, 0.5, 1e-9, "projection_identity: y at origin");
  test::near(p->depth, 0.0, 1e-9, "projection_identity: depth at origin");

  p = project_to_screen(I, I, 1, -1, 0.5, false);
  test::require(p.has_value(), "projection_identity: identity at (1,-1,0.5)");
  test::near(p->x, 1.0, 1e-9, "projection_identity: x at (1,-1,0.5)");
  test::near(p->y, 0.0, 1e-9, "projection_identity: y at (1,-1,0.5)");
  test::near(p->depth, 0.5, 1e-9, "projection_identity: depth at (1,-1,0.5)");

  p = project_to_screen(I, I, 0, 0, 2, false);
  test::require(!p.has_value(), "projection_identity: identity behind screen");

  p = project_to_screen(I, I, 0, 0, -0.5, true);
  test::require(!p.has_value(), "projection_identity: identity behind camera with zero_to_one");
}

void test_projection_behind_camera() {
  float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  float P[16] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
  P[0] = 1;
  P[5] = 1;
  P[10] = 1;
  P[11] = -1;

  auto p = project_to_screen(I, P, 0, 0, 5, false);
  test::require(!p.has_value(), "projection_behind_camera: point behind camera");

  p = project_to_screen(I, P, 0.5, 0, -2, false);
  test::require(p.has_value(), "projection_behind_camera: point in front of camera");
  test::near(p->x, 0.625, 1e-9, "projection_behind_camera: x coordinate");
}

} // anonymous namespace

int main() {
  zoal_atc::gui::set_ui_language("en");
  
  test_detail_by_distance();
  test_opacity_fades_in_the_last_fifth();
  test_shown_within_range_or_tracked();
  test_airborne_ai_full();
  test_pilot_on_ground_far();
  test_no_type();
  test_identity_line();
  test_projection_identity();
  test_projection_behind_camera();
  
  std::cout << "traffic label tests passed\n";
  return 0;
}
