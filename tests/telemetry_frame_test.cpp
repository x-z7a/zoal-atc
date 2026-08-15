#include "zoal_atc/telemetry/telemetry_frame.hpp"
#include "zoal_atc/transport/json_util.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace zoal_atc::telemetry;
using zoal_atc::transport::json_escape;
using zoal_atc::transport::json_number;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void require_contains(const std::string &haystack, const std::string &needle,
                      const std::string &message) {
  require(haystack.find(needle) != std::string::npos,
          message + " (missing `" + needle + "` in `" + haystack + "`)");
}

TelemetrySnapshot sample_snapshot() {
  TelemetrySnapshot s;
  s.latitude_deg = 37.4611;
  s.longitude_deg = -122.115;
  s.altitude_ft_msl = 7.2;
  s.height_agl_ft = 0.5;
  s.groundspeed_kts = 3.1;
  s.indicated_airspeed_kts = 2.4;
  s.vertical_speed_fpm = -640.0;
  s.heading_true_deg = 310.0;
  s.on_ground = true;
  s.com1_freq_mhz = 118.6;
  s.com1_standby_mhz = 121.6;
  s.active_com = 1;
  s.airport_id = "KPAO";
  s.airport_name = "Palo Alto";
  s.wind_direction_deg = 290;
  s.wind_speed_kt = 9;
  s.visibility_m = 16093;
  s.cloud_type = 1;
  s.cloud_base_ft_msl = 1500;
  s.temperature_c = 18;
  s.dewpoint_c = 12;
  s.qnh_inhg = 29.98;
  s.qnh_hpa = 1015.2;
  s.transponder_code = 1200;
  s.transponder_mode = 2;
  s.paused = true;
  s.now_secs = 123.5;
  return s;
}

// The frame must match the console wire contract (pluginws.TelemetrySnapshot):
// type "telemetry", session_id, and snake_case telemetry keys.
void frame_matches_wire_contract() {
  const std::string frame = build_telemetry_frame(sample_snapshot(), "xplane-user");

  require_contains(frame, "\"type\":\"telemetry\"", "frame type");
  require_contains(frame, "\"session_id\":\"xplane-user\"", "session id");
  require_contains(frame, "\"latitude_deg\":37.4611", "latitude");
  require_contains(frame, "\"longitude_deg\":-122.115", "longitude");
  require_contains(frame, "\"on_ground\":true", "on ground");
  require_contains(frame, "\"indicated_airspeed_kts\":2.4", "indicated airspeed");
  require_contains(frame, "\"vertical_speed_fpm\":-640", "vertical speed");
  require_contains(frame, "\"com1_freq_mhz\":118.6", "com1");
  require_contains(frame, "\"active_com\":1", "active com");
  require_contains(frame, "\"airport_id\":\"KPAO\"", "airport id");
  require_contains(frame, "\"airport_name\":\"Palo Alto\"", "airport name");
  require_contains(frame, "\"qnh_inhg\":29.98", "qnh inHg");
  require_contains(frame, "\"transponder_code\":1200", "squawk");
  require_contains(frame, "\"transponder_mode\":2", "transponder mode");
  require_contains(frame, "\"paused\":true", "simulator pause state");
  require_contains(frame, "\"now_secs\":123.5", "clock");
}

// Zero/default fields still render (the console treats zero as absent).
void empty_snapshot_renders() {
  const std::string frame = build_telemetry_frame(TelemetrySnapshot{}, "s");
  require_contains(frame, "\"on_ground\":false", "default on_ground");
  require_contains(frame, "\"paused\":false", "default paused");
  require_contains(frame, "\"airport_id\":\"\"", "empty airport");
}

// The rate policy follows the data-sources baseline: surface fast, terminal
// slower, enroute slow.
void rate_policy_is_state_dependent() {
  TelemetryRatePolicy policy;
  require(policy.interval_secs(true, 0) < policy.interval_secs(false, 1000),
          "surface samples faster than terminal");
  require(policy.interval_secs(false, 1000) <
              policy.interval_secs(false, 10000),
          "terminal samples faster than enroute");

  require(policy.due(10.0, 0, true, 0), "never-sent is always due");
  require(!policy.due(10.05, 10.0, true, 0), "not yet due at surface rate");
  require(policy.due(10.25, 10.0, true, 0), "due after the surface interval");
  require(!policy.due(10.6, 10.0, false, 10000),
          "enroute waits a full second");
  require(policy.due(11.05, 10.0, false, 10000), "enroute due after 1s");
}

void unit_conversions_are_correct() {
  require(std::abs(meters_to_feet(1.0) - 3.28084) < 1e-9, "m->ft");
  require(std::abs(mps_to_knots(10.0) - 19.438445) < 1e-6, "m/s->kt");
  require(std::abs(sm_to_meters(10.0) - 16093.4) < 1e-6, "sm->m");
  require(std::abs(pascals_to_inhg(101325.0) - 29.9213) < 1e-3, "Pa->inHg");
  require(std::abs(pascals_to_hpa(101325.0) - 1013.25) < 1e-9, "Pa->hPa");
  require(std::abs(com_hz833_to_mhz(118605) - 118.605) < 1e-9, "kHz->MHz");
  require(com_mhz_to_hz833(127.7) == 127700, "MHz->kHz int");
}

void json_helpers_escape_and_format() {
  require(json_escape("say \"again\"\n") == "say \\\"again\\\"\\n",
          "escape quotes and newline");
  require(json_number(0) == "0", "zero renders bare");
  require(json_number(-122.115) == "-122.115", "plain decimal");
  const double nan_value = std::nan("");
  require(json_number(nan_value) == "0", "NaN degrades to 0");
}

} // namespace

void control_frame_parses() {
  const std::string frame =
      R"({"type":"telemetry_control","profile":"alert","interval_secs":0.1,)"
      R"("add_datarefs":["sim/flightmodel/position/vh_ind_fpm2"]})";
  const auto control = parse_telemetry_control(frame);
  require(control.valid, "a telemetry_control frame parses as valid");
  require(control.profile == "alert", "profile is decoded");
  require(std::fabs(control.interval_secs - 0.1) < 1e-9,
          "interval_secs is decoded");

  // A non-control frame is rejected.
  const auto other = parse_telemetry_control(
      R"({"type":"atc_reply","text":"cleared"})");
  require(!other.valid, "a non-control frame is not a control command");

  // Malformed JSON degrades to invalid, never throws.
  require(!parse_telemetry_control("not json").valid,
          "malformed control payload degrades");
}

void controller_honors_console_then_falls_back() {
  TelemetryRateController controller;

  // With no command, it uses the local policy (surface fast, enroute slow).
  require(controller.interval_secs(true, 0) <
              controller.interval_secs(false, 10000),
          "no command -> local policy governs");

  // A console command overrides the local rate regardless of flight state.
  TelemetryControl alert;
  alert.valid = true;
  alert.interval_secs = 0.1;
  controller.apply(alert);
  require(std::fabs(controller.interval_secs(false, 10000) - 0.1) < 1e-9,
          "commanded interval overrides the local enroute rate");
  require(controller.due(10.15, 10.0, false, 10000),
          "due after the commanded 0.1s even enroute");
  require(!controller.due(10.05, 10.0, false, 10000),
          "not yet due before the commanded interval");

  // On disconnect the command clears and the local policy governs again.
  controller.reset();
  require(controller.interval_secs(false, 10000) ==
              TelemetryRatePolicy{}.enroute_interval_secs,
          "reset falls back to the local enroute rate");

  // An invalid command is ignored (feed never stalls on a bad frame).
  TelemetryControl bad;
  bad.valid = false;
  controller.apply(bad);
  require(controller.interval_secs(true, 0) ==
              TelemetryRatePolicy{}.surface_interval_secs,
          "invalid command is ignored, local policy stands");
}

void query_frame_parses() {
  const std::string frame =
      R"({"type":"telemetry_query","query_id":42,)"
      R"("datarefs":["sim/flightmodel/misc/h_ind","sim/cockpit2/gauges/altitude"]})";
  const auto query = parse_telemetry_query(frame);
  require(query.valid, "a telemetry_query frame parses as valid");
  require(query.query_id == 42, "query_id is decoded");
  require(query.datarefs.size() == 2, "both datarefs are decoded");
  require(query.datarefs[0] == "sim/flightmodel/misc/h_ind",
          "first dataref path is decoded");

  // A non-query frame is rejected; malformed JSON degrades, never throws.
  require(!parse_telemetry_query(R"({"type":"atc_reply"})").valid,
          "a non-query frame is not a query");
  require(!parse_telemetry_query("garbage").valid,
          "malformed query payload degrades");
  // An empty dataref list is valid (an empty answer round-trips).
  const auto empty =
      parse_telemetry_query(R"({"type":"telemetry_query","query_id":1,"datarefs":[]})");
  require(empty.valid && empty.datarefs.empty(),
          "an empty datarefs list parses");
}

void query_result_frame_builds() {
  const std::vector<std::pair<std::string, double>> values = {
      {"sim/flightmodel/misc/h_ind", 29.92},
      {"sim/cockpit2/gauges/altitude", 3500.0}};
  const std::string frame = build_telemetry_query_result(42, values);
  require_contains(frame, "\"type\":\"telemetry_query_result\"",
                   "result frame carries its type");
  require_contains(frame, "\"query_id\":42", "result frame echoes query_id");
  require_contains(frame, "\"sim/flightmodel/misc/h_ind\":29.92",
                   "result frame carries each value");

  // A no-readable-values result still forms a valid empty object.
  require_contains(build_telemetry_query_result(7, {}), "\"values\":{}",
                   "an empty result has an empty values object");
}

void radio_tune_frame_parses() {
  const auto tune = parse_radio_tune_command(
      R"({"type":"radio_tune","frequency_mhz":127.7,"com":1,"target":"active"})");
  require(tune.valid, "a radio_tune frame parses as valid");
  require(std::fabs(tune.frequency_mhz - 127.7) < 1e-9,
          "frequency_mhz is decoded");
  require(tune.com == 1, "COM is decoded");
  require(tune.target == "active", "target is decoded");

  const auto standby =
      parse_radio_tune_command(R"({"type":"radio_tune","frequency_mhz":121.6})");
  require(standby.valid && standby.target == "standby" && standby.com == 0,
          "missing target/COM defaults to standby on selected COM");

  require(!parse_radio_tune_command(R"({"type":"atc_reply"})").valid,
          "a non-radio frame is not a radio tune");
  require(!parse_radio_tune_command(
               R"({"type":"radio_tune","frequency_mhz":200})")
               .valid,
          "out-of-band frequency is rejected");
}

int main() {
  frame_matches_wire_contract();
  empty_snapshot_renders();
  rate_policy_is_state_dependent();
  control_frame_parses();
  controller_honors_console_then_falls_back();
  query_frame_parses();
  query_result_frame_builds();
  radio_tune_frame_parses();
  unit_conversions_are_correct();
  json_helpers_escape_and_format();
  std::cout << "zoal_atc_telemetry_tests: all tests passed\n";
  return 0;
}
