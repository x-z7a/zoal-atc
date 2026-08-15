#ifndef ZOAL_ATC_TELEMETRY_TELEMETRY_FRAME_HPP
#define ZOAL_ATC_TELEMETRY_TELEMETRY_FRAME_HPP

#include "zoal_atc/telemetry/traffic.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The dataref-sampling half of the plugin edge (docs/data-sources.md): an
// SDK-free snapshot of the starter dataref subset, its wire-frame JSON builder
// (the explicit plugin<->console contract in console/internal/pluginws
// TelemetrySnapshot — snake_case keys, shared with the Go side), and the
// state-dependent sampling-rate policy. The X-Plane dataref reads themselves are
// SDK glue in xplane_plugin.cpp; everything here is testable without the sim.
namespace zoal_atc::telemetry {

// TelemetrySnapshot mirrors the wire schema field-for-field. Values are already
// converted to wire units (feet, knots, MHz, inHg/hPa) — see the conversion
// helpers below. Fields the plugin cannot sample stay zero and degrade
// gracefully console-side.
struct TelemetrySnapshot {
  // Kinematics
  double latitude_deg = 0;
  double longitude_deg = 0;
  double altitude_ft_msl = 0;
  double height_agl_ft = 0;
  double groundspeed_kts = 0;
  double indicated_airspeed_kts = 0;
  double vertical_speed_fpm = 0;
  double heading_true_deg = 0;
  bool on_ground = false;

  // Radios
  double com1_freq_mhz = 0;
  double com2_freq_mhz = 0;
  double com1_standby_mhz = 0;
  double com2_standby_mhz = 0;
  int active_com = 0;

  // Airport (nearest-field lookup; static detail arrives via navdata frames)
  std::string airport_id;
  std::string airport_name;

  // Weather (own-ship interpolated; the real METAR frame is authoritative)
  double wind_direction_deg = 0;
  double wind_speed_kt = 0;
  double visibility_m = 0;
  int cloud_type = 0;
  double cloud_base_ft_msl = 0;
  double temperature_c = 0;
  double dewpoint_c = 0;
  double qnh_inhg = 0;
  double qnh_hpa = 0;

  // Transponder
  int transponder_code = 0;
  int transponder_mode = 0;

  // Traffic (phase21). The bounded surveillance picture; `traffic_status` is
  // what lets the console tell an empty sky from a missing feed, so it is sent
  // on every frame even when the list is empty.
  std::vector<TrafficTarget> traffic;
  TrafficStatus traffic_status = TrafficStatus::unavailable;
  bool traffic_truncated = false;
  // What the sim had, against what this frame carries.
  TrafficCensus traffic_census;

  // Simulator state / clock
  bool paused = false;
  double now_secs = 0;
};

// build_telemetry_frame renders the snapshot as the `telemetry` WebSocket frame.
std::string build_telemetry_frame(const TelemetrySnapshot &snapshot,
                                  std::string_view session_id);

// TelemetryRatePolicy is the state-dependent sampling baseline from
// docs/data-sources.md ("Sampling rates"): surface ops sample fast, terminal
// slower, enroute slow. Values are tunable config, not design constants.
struct TelemetryRatePolicy {
  double surface_interval_secs = 0.2;  // 5 Hz — taxi/runway incursion monitoring
  double terminal_interval_secs = 0.5; // 2 Hz — approach/pattern/departure
  double enroute_interval_secs = 1.0;  // 1 Hz — cruise
  double terminal_ceiling_agl_ft = 5000;

  // interval_secs returns the send interval for the current flight state.
  double interval_secs(bool on_ground, double height_agl_ft) const;

  // due reports whether a new sample should be sent at now_secs given the last
  // send time (0 = never sent).
  bool due(double now_secs, double last_sent_secs, bool on_ground,
           double height_agl_ft) const;
};

// TelemetryControl is the console->plugin sampling command decoded from a
// `telemetry_control` frame (P2-M4 "the console decides; the plugin obeys"). The
// console owns the policy; the plugin honors the commanded interval until a new
// command supersedes it or the connection drops. `interval_secs <= 0` means the
// frame carried no usable interval (honor by profile/keep local policy).
struct TelemetryControl {
  std::string profile;         // "surface" | "terminal" | "enroute" | "alert"
  double interval_secs = 0;    // commanded push interval; <= 0 = unset
  bool valid = false;          // false when the payload was not a control frame
};

// parse_telemetry_control decodes a `telemetry_control` frame. A frame of any
// other type (or malformed JSON) returns {valid=false}. SDK-free / testable.
TelemetryControl parse_telemetry_control(const std::string &json);

// TelemetryRateController combines the console command with the local rate
// policy: while a valid console interval is set it governs sampling; with none
// (no command yet, or cleared on disconnect) it falls back to the local
// TelemetryRatePolicy so the feed never stalls (graceful degradation).
struct TelemetryRateController {
  TelemetryRatePolicy local;   // fallback baseline
  double commanded_secs = 0;   // console-commanded interval; <= 0 = none

  // apply adopts a decoded control command (ignores an invalid one).
  void apply(const TelemetryControl &control);
  // reset clears any console command, reverting to the local policy (call on
  // plugin disconnect / console loss).
  void reset();
  // interval_secs is the effective send interval: the commanded value when set,
  // else the local policy for the current flight state.
  double interval_secs(bool on_ground, double height_agl_ft) const;
  // due reports whether a new sample is owed at now_secs.
  bool due(double now_secs, double last_sent_secs, bool on_ground,
           double height_agl_ft) const;
};

// TelemetryQuery is a console->plugin one-off pull request decoded from a
// `telemetry_query` frame (P2-M4 T4): read the listed datarefs once and answer
// with a telemetry_query_result correlated by query_id. A non-query / malformed
// payload yields {valid=false}. SDK-free / testable; the SDK glue does the
// actual XPLM dataref reads.
struct TelemetryQuery {
  std::uint64_t query_id = 0;
  std::vector<std::string> datarefs;
  bool valid = false;
};

// parse_telemetry_query decodes a `telemetry_query` frame.
TelemetryQuery parse_telemetry_query(const std::string &json);

// RadioTuneCommand is a console->plugin command decoded from a `radio_tune`
// frame. `com == 0` means tune the currently selected COM radio; `target` is
// "active" or "standby".
struct RadioTuneCommand {
  double frequency_mhz = 0;
  int com = 0;
  std::string target = "standby";
  bool valid = false;
};

// parse_radio_tune_command decodes a `radio_tune` frame.
RadioTuneCommand parse_radio_tune_command(const std::string &json);

// build_telemetry_query_result renders the answer frame for query_id: a
// `telemetry_query_result` carrying the sampled value of each readable dataref.
// Values are (dataref path, value) pairs; unreadable paths are simply omitted by
// the caller (graceful degradation).
std::string build_telemetry_query_result(
    std::uint64_t query_id,
    const std::vector<std::pair<std::string, double>> &values);

// Unit conversions from raw dataref units to wire units.
double meters_to_feet(double meters);
double mps_to_knots(double mps);
double sm_to_meters(double statute_miles);
double pascals_to_inhg(double pascals);
double pascals_to_hpa(double pascals);
// com_hz833_to_mhz converts the 8.33-kHz-aware frequency dataref value (kHz,
// e.g. 118605) to MHz (118.605).
double com_hz833_to_mhz(int khz);
// com_mhz_to_hz833 converts MHz to the integer value written to X-Plane's
// 8.33-kHz-aware COM actuator datarefs.
int com_mhz_to_hz833(double mhz);

} // namespace zoal_atc::telemetry

#endif // ZOAL_ATC_TELEMETRY_TELEMETRY_FRAME_HPP
