#include "zoal_atc/telemetry/telemetry_frame.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <cmath>
#include <sstream>

namespace zoal_atc::telemetry {

using transport::json_escape;
using transport::json_number;

std::string build_telemetry_frame(const TelemetrySnapshot &s,
                                  std::string_view session_id) {
  std::ostringstream out;
  out << "{\"type\":\"telemetry\",\"session_id\":\"" << json_escape(session_id)
      << "\",\"telemetry\":{";

  out << "\"latitude_deg\":" << json_number(s.latitude_deg)
      << ",\"longitude_deg\":" << json_number(s.longitude_deg)
      << ",\"altitude_ft_msl\":" << json_number(s.altitude_ft_msl)
      << ",\"height_agl_ft\":" << json_number(s.height_agl_ft)
      << ",\"groundspeed_kts\":" << json_number(s.groundspeed_kts)
      << ",\"indicated_airspeed_kts\":" << json_number(s.indicated_airspeed_kts)
      << ",\"vertical_speed_fpm\":" << json_number(s.vertical_speed_fpm)
      << ",\"heading_true_deg\":" << json_number(s.heading_true_deg)
      << ",\"on_ground\":" << (s.on_ground ? "true" : "false");

  out << ",\"com1_freq_mhz\":" << json_number(s.com1_freq_mhz)
      << ",\"com2_freq_mhz\":" << json_number(s.com2_freq_mhz)
      << ",\"com1_standby_mhz\":" << json_number(s.com1_standby_mhz)
      << ",\"com2_standby_mhz\":" << json_number(s.com2_standby_mhz)
      << ",\"active_com\":" << s.active_com;

  out << ",\"airport_id\":\"" << json_escape(s.airport_id) << "\""
      << ",\"airport_name\":\"" << json_escape(s.airport_name) << "\"";

  out << ",\"wind_direction_deg\":" << json_number(s.wind_direction_deg)
      << ",\"wind_speed_kt\":" << json_number(s.wind_speed_kt)
      << ",\"visibility_m\":" << json_number(s.visibility_m)
      << ",\"cloud_type\":" << s.cloud_type
      << ",\"cloud_base_ft_msl\":" << json_number(s.cloud_base_ft_msl)
      << ",\"temperature_c\":" << json_number(s.temperature_c)
      << ",\"dewpoint_c\":" << json_number(s.dewpoint_c)
      << ",\"qnh_inhg\":" << json_number(s.qnh_inhg)
      << ",\"qnh_hpa\":" << json_number(s.qnh_hpa);

  out << ",\"transponder_code\":" << s.transponder_code
      << ",\"transponder_mode\":" << s.transponder_mode;

  // The traffic block is always present, including its status, because an
  // absent block and an empty sky are different claims and only the plugin can
  // tell them apart (docs/phase21/02-the-feed.md).
  out << ",\"traffic_status\":\"" << traffic_status_name(s.traffic_status)
      << "\""
      << ",\"traffic_truncated\":" << (s.traffic_truncated ? "true" : "false")
      // The census: what the sim had, against what this frame carries. A
      // console that can only see the survivors cannot tell a bound doing its
      // job from a feed that has stopped working.
      << ",\"traffic_reported\":" << s.traffic_census.reported
      << ",\"traffic_seen\":" << s.traffic_census.seen
      << ",\"traffic_dropped_range\":" << s.traffic_census.dropped_range
      << ",\"traffic_dropped_vertical\":" << s.traffic_census.dropped_vertical
      << ",\"traffic_dropped_cap\":" << s.traffic_census.dropped_cap
      << ",\"traffic\":[";
  for (std::size_t i = 0; i < s.traffic.size(); ++i) {
    const TrafficTarget &t = s.traffic[i];
    if (i > 0) {
      out << ',';
    }
    out << "{\"id\":" << t.id << ",\"callsign\":\"" << json_escape(t.callsign)
        << "\",\"icao_type\":\"" << json_escape(t.icao_type) << "\""
        << ",\"lat_deg\":" << json_number(t.lat_deg)
        << ",\"lon_deg\":" << json_number(t.lon_deg)
        << ",\"alt_ft_msl\":" << json_number(t.alt_ft_msl)
        << ",\"groundspeed_kts\":" << json_number(t.groundspeed_kts)
        << ",\"vertical_speed_fpm\":" << json_number(t.vertical_speed_fpm)
        << ",\"track_deg\":" << json_number(t.track_deg)
        << ",\"heading_deg\":" << json_number(t.heading_deg)
        << ",\"on_ground\":" << (t.on_ground ? "true" : "false") << '}';
  }
  out << ']';

  out << ",\"paused\":" << (s.paused ? "true" : "false")
      << ",\"now_secs\":" << json_number(s.now_secs);

  out << "}}";
  return out.str();
}

double TelemetryRatePolicy::interval_secs(bool on_ground,
                                          double height_agl_ft) const {
  if (on_ground) {
    return surface_interval_secs;
  }
  if (height_agl_ft < terminal_ceiling_agl_ft) {
    return terminal_interval_secs;
  }
  return enroute_interval_secs;
}

bool TelemetryRatePolicy::due(double now_secs, double last_sent_secs,
                              bool on_ground, double height_agl_ft) const {
  if (last_sent_secs <= 0) {
    return true;
  }
  return (now_secs - last_sent_secs) >=
         interval_secs(on_ground, height_agl_ft);
}

TelemetryControl parse_telemetry_control(const std::string &json) {
  TelemetryControl control;
  const auto type = transport::json_string_field(json, "type");
  if (!type.has_value() || *type != "telemetry_control") {
    return control; // not a control frame
  }
  control.valid = true;
  if (const auto profile = transport::json_string_field(json, "profile")) {
    control.profile = *profile;
  }
  if (const auto interval = transport::json_number_field(json, "interval_secs")) {
    control.interval_secs = *interval;
  }
  return control;
}

TelemetryQuery parse_telemetry_query(const std::string &json) {
  TelemetryQuery query;
  const auto type = transport::json_string_field(json, "type");
  if (!type.has_value() || *type != "telemetry_query") {
    return query; // not a query frame
  }
  query.valid = true;
  if (const auto id = transport::json_uint_field(json, "query_id")) {
    query.query_id = *id;
  }
  query.datarefs = transport::json_string_array_field(json, "datarefs");
  return query;
}

RadioTuneCommand parse_radio_tune_command(const std::string &json) {
  RadioTuneCommand command;
  const auto type = transport::json_string_field(json, "type");
  if (!type.has_value() || *type != "radio_tune") {
    return command;
  }
  const auto mhz = transport::json_number_field(json, "frequency_mhz");
  if (!mhz.has_value() || *mhz < 118.0 || *mhz > 136.975) {
    return command;
  }
  command.valid = true;
  command.frequency_mhz = *mhz;
  if (const auto com = transport::json_uint_field(json, "com")) {
    command.com = static_cast<int>(*com);
  }
  if (const auto target = transport::json_string_field(json, "target")) {
    command.target = *target == "active" ? "active" : "standby";
  }
  return command;
}

std::string build_telemetry_query_result(
    std::uint64_t query_id,
    const std::vector<std::pair<std::string, double>> &values) {
  std::ostringstream out;
  out << "{\"type\":\"telemetry_query_result\",\"query_id\":" << query_id
      << ",\"values\":{";
  bool first = true;
  for (const auto &kv : values) {
    if (!first) {
      out << ',';
    }
    first = false;
    out << '"' << json_escape(kv.first) << "\":" << json_number(kv.second);
  }
  out << "}}";
  return out.str();
}

void TelemetryRateController::apply(const TelemetryControl &control) {
  if (!control.valid) {
    return;
  }
  commanded_secs = control.interval_secs > 0 ? control.interval_secs : 0;
}

void TelemetryRateController::reset() { commanded_secs = 0; }

double TelemetryRateController::interval_secs(bool on_ground,
                                              double height_agl_ft) const {
  if (commanded_secs > 0) {
    return commanded_secs;
  }
  return local.interval_secs(on_ground, height_agl_ft);
}

bool TelemetryRateController::due(double now_secs, double last_sent_secs,
                                  bool on_ground, double height_agl_ft) const {
  if (last_sent_secs <= 0) {
    return true;
  }
  return (now_secs - last_sent_secs) >=
         interval_secs(on_ground, height_agl_ft);
}

double meters_to_feet(double meters) { return meters * 3.28084; }

double mps_to_knots(double mps) { return mps * 1.9438445; }

double sm_to_meters(double statute_miles) { return statute_miles * 1609.34; }

double pascals_to_inhg(double pascals) { return pascals / 3386.389; }

double pascals_to_hpa(double pascals) { return pascals / 100.0; }

double com_hz833_to_mhz(int khz) { return static_cast<double>(khz) / 1000.0; }

int com_mhz_to_hz833(double mhz) {
  return static_cast<int>(std::lround(mhz * 1000.0));
}

} // namespace zoal_atc::telemetry
