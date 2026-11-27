#include "test_support.hpp"
#include "zoal_atc/gui/traffic_panel.hpp"
#include "zoal_atc/gui/i18n.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace zoal_atc::gui;
using zoal_atc::traffic::Nearby;
using zoal_atc::traffic::TrafficKind;
using test::require;

void eq(const std::string &got, const std::string &want, const std::string &what) {
  require(got == want, what + ": got '" + got + "', want '" + want + "'");
}

int main() {
  set_ui_language("en");

  // Test distance formatting
  eq(format_distance_nm(0.04), "0.0 nm", "distance 0.04");
  eq(format_distance_nm(12.44), "12.4 nm", "distance 12.44");
  eq(format_distance_nm(99.96), "100 nm", "distance 99.96");
  eq(format_distance_nm(143.4), "143 nm", "distance 143.4");

  // Test bearing formatting
  eq(format_bearing(284.4), "284°", "bearing 284.4");
  eq(format_bearing(0.2), "360°", "bearing 0.2");
  eq(format_bearing(359.6), "360°", "bearing 359.6");
  eq(format_bearing(1), "001°", "bearing 1");
  eq(format_bearing(-90), "270°", "bearing -90");

  // Test altitude formatting
  eq(format_traffic_altitude(35000, false), "FL350", "altitude 35000,false");
  eq(format_traffic_altitude(18040, false), "FL180", "altitude 18040,false");
  eq(format_traffic_altitude(17960, false), "FL180", "altitude 17960,false");
  eq(format_traffic_altitude(3249, false), "3,200 ft", "altitude 3249,false");
  eq(format_traffic_altitude(40, false), "0 ft", "altitude 40,false");
  eq(format_traffic_altitude(3000, true), "GND", "altitude 3000,true");

  // Test relative altitude formatting
  eq(format_relative_altitude(1234), "+1,200", "relative 1234");
  eq(format_relative_altitude(-449), "\xE2\x88\x92" "400", "relative -449");
  eq(format_relative_altitude(30), "0", "relative 30");
  eq(format_relative_altitude(-60), "\xE2\x88\x92" "100", "relative -60");

  // Test vertical speed formatting
  eq(format_vertical_speed(150), "level", "vertical 150");
  eq(format_vertical_speed(-199), "level", "vertical -199");
  eq(format_vertical_speed(1520), "+1,500", "vertical 1520");
  eq(format_vertical_speed(-800), "\xE2\x88\x92" "800", "vertical -800");

  // Test vertical trend
  require(vertical_trend(250) == 1, "vertical_trend(250) == 1");
  require(vertical_trend(-250) == -1, "vertical_trend(-250) == -1");
  require(vertical_trend(0) == 0, "vertical_trend(0) == 0");

  // Test rows_and_counts
  Nearby a;
  a.identity.id = "ai:ACA451";
  a.identity.kind = TrafficKind::Ai;
  a.identity.callsign = "ACA451";
  a.identity.icao_type = "A320";
  a.identity.airline = "ACA";
  a.identity.destination = "CYYZ";
  a.has_own = true;
  a.distance_nm = 12.44;
  a.bearing_deg = 284;
  a.relative_alt_ft = 1234;
  a.sample.alt_ft_msl = 6234;
  a.sample.groundspeed_kts = 250.4;
  a.sample.vertical_speed_fpm = 1520;
  a.sample.on_ground = false;

  Nearby b;
  b.identity.id = "participant:x";
  b.identity.kind = TrafficKind::Participant;
  b.identity.callsign = "";
  b.identity.icao_type = "C172";
  b.identity.airline = "";
  b.has_own = true;
  b.distance_nm = 3;
  b.bearing_deg = 90;
  b.sample.on_ground = true;
  b.sample.alt_ft_msl = 300;
  b.sample.groundspeed_kts = 12;

  Nearby c;
  c.identity.id = "ai:WJA1";
  c.identity.kind = TrafficKind::Ai;
  c.identity.callsign = "WJA1";
  c.has_own = false;
  c.sample.alt_ft_msl = 35000;

  std::vector<Nearby> picture = {a, b, c};
  auto result = traffic_panel(picture, TrafficFilter::all, "ai:WJA1", "ai:ACA451");

  require(result.available, "available");
  require(result.total == 3, "total 3");
  require(result.pilots == 1, "pilots 1");
  require(result.ai == 2, "ai 2");
  require(result.rows.size() == 3, "rows size 3");
  require(result.tracked == "ai:WJA1", "tracked ai:WJA1");
  require(result.followed == "ai:ACA451", "followed ai:ACA451");

  const auto& row0 = result.rows[0];
  eq(row0.callsign, "ACA451", "row0 callsign");
  eq(row0.type, "A320 \xE2\x86\x92 CYYZ", "row0 type");
  eq(row0.distance, "12.4 nm", "row0 distance");
  eq(row0.bearing, "284°", "row0 bearing");
  eq(row0.altitude, "6,200 ft", "row0 altitude");
  eq(row0.relative, "+1,200", "row0 relative");
  eq(row0.speed, "250 kt", "row0 speed");
  eq(row0.vertical, "+1,500", "row0 vertical");
  require(row0.trend == 1, "row0 trend 1");
  require(row0.followed, "row0 followed");
  require(!row0.tracked, "row0 not tracked");
  require(!row0.pilot, "row0 not pilot");

  const auto& row1 = result.rows[1];
  eq(row1.callsign, "participant:x", "row1 callsign");
  eq(row1.type, "C172", "row1 type");
  eq(row1.altitude, "GND", "row1 altitude");
  eq(row1.relative, "", "row1 relative");
  eq(row1.vertical, "", "row1 vertical");
  require(row1.trend == 0, "row1 trend 0");
  require(row1.on_ground, "row1 on_ground");
  require(row1.pilot, "row1 pilot");
  eq(row1.speed, "12 kt", "row1 speed");

  const auto& row2 = result.rows[2];
  eq(row2.type, "—", "row2 type");
  eq(row2.distance, "—", "row2 distance");
  eq(row2.bearing, "—", "row2 bearing");
  eq(row2.relative, "", "row2 relative");
  eq(row2.altitude, "FL350", "row2 altitude");
  require(row2.tracked, "row2 tracked");
  require(!row2.followed, "row2 not followed");
  require(!row2.pilot, "row2 not pilot");

  // Test filters
  auto pilots_result = traffic_panel(picture, TrafficFilter::pilots, "ai:WJA1", "ai:ACA451");
  require(pilots_result.total == 3, "pilots total 3");
  require(pilots_result.rows.size() == 1, "pilots rows size 1");
  eq(pilots_result.rows[0].id, "participant:x", "pilots row id");

  auto ai_result = traffic_panel(picture, TrafficFilter::ai, "ai:WJA1", "ai:ACA451");
  require(ai_result.total == 3, "ai total 3");
  require(ai_result.rows.size() == 2, "ai rows size 2");
  eq(ai_result.rows[0].id, "ai:ACA451", "ai row0 id");
  eq(ai_result.rows[1].id, "ai:WJA1", "ai row1 id");

  // Test empty
  auto empty_result = traffic_panel({}, TrafficFilter::all, "", "");
  require(empty_result.available, "empty available");
  require(empty_result.rows.empty(), "empty rows");
  require(empty_result.total == 0, "empty total");

  std::cout << "traffic panel tests passed\n";
  return 0;
}
