#ifndef ZOAL_ATC_TRAFFIC_TARGET_HPP
#define ZOAL_ATC_TRAFFIC_TARGET_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zoal_atc::traffic {

// Who the console says an aircraft is. A participant is another real pilot
// connected to the console; an AI aircraft is one the console flies itself and
// which therefore obeys its controllers. The plugin renders both the same way
// and decides nothing about either.
enum class TrafficKind { Participant, Ai, Unknown };

TrafficKind parse_kind(const std::string &text);
const char *kind_name(TrafficKind kind);

// Identity changes rarely. A change of type, airline or livery is a model
// change for the renderer; a change of callsign is only a label.
struct Identity {
  std::string id; // stable console track id, e.g. "participant:abc", "ai:UAL123"
  TrafficKind kind = TrafficKind::Unknown;
  std::string callsign;
  std::string icao_type;
  std::string airline; // ICAO operator designator, e.g. "UAL"
  std::string livery;
  // 24-bit address the console assigned or the participant reports. The
  // renderer passes it to XPMP2 so the aircraft shows on TCAS with an address
  // the console recognises as its own (see docs/protocol.md, echo suppression).
  std::uint32_t mode_s = 0;

  [[nodiscard]] bool same_model(const Identity &other) const {
    return icao_type == other.icao_type && airline == other.airline &&
           livery == other.livery;
  }
};

// Light bits, in the order XPMP2 exposes them.
enum Light : std::uint32_t {
  kLightNav = 1u << 0,
  kLightBeacon = 1u << 1,
  kLightStrobe = 1u << 2,
  kLightTaxi = 1u << 3,
  kLightLanding = 1u << 4,
};

// One timestamped state of one aircraft. Time is the console's clock in
// seconds; the plugin maps it to its own with ClockSync.
struct Sample {
  double t_s = 0.0;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double alt_ft_msl = 0.0;
  double heading_deg = 0.0;
  double pitch_deg = 0.0;
  double roll_deg = 0.0;
  double groundspeed_kts = 0.0;
  double vertical_speed_fpm = 0.0;
  double track_deg = 0.0;
  bool on_ground = false;

  double gear = 1.0;  // 0 up .. 1 down
  double flaps = 0.0; // 0 .. 1
  double speedbrake = 0.0;
  double thrust = 0.0;
  std::uint32_t lights = 0;
};

struct Target {
  Identity identity;
  Sample sample;
  // How far behind the console clock to draw it, when the console says
  // (render_delay_ms): 0 for an AI aircraft, whose samples arrive ahead of
  // time. Unset, the registry's own interpolation delay.
  std::optional<double> render_delay_s;
};

// The console's `traffic` frame: everything near this aircraft, and what is
// no longer there. A target absent from a frame is not removed by its absence
// (frames may be partial); only `removed` or the registry's timeout removes it.
struct TrafficFrame {
  std::uint64_t seq = 0;
  double server_time_s = 0.0;
  std::vector<Target> targets;
  std::vector<std::string> removed;
};

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_TARGET_HPP
