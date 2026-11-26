#ifndef ZOAL_ATC_TELEMETRY_OWNSHIP_IDENTITY_HPP
#define ZOAL_ATC_TELEMETRY_OWNSHIP_IDENTITY_HPP

#include <cstdint>
#include <string>
#include <vector>

// Who this aircraft is, as X-Plane says: slot 0 of the TCAS arrays.
//
// The plugin reads slot 0 and nothing else. The other slots are the traffic
// around the aircraft, and the console no longer asks for them: everything
// near a pilot is either another pilot, who reports their own position, or an
// aircraft the console flies itself (docs/architecture/traffic.md in the
// console repository). Reporting what the simulator's TCAS sees is what made
// a multiplayer layer's injected traffic flood every frequency with alerts.
//
// SDK-free: the reads are glue in xplane_plugin.cpp.
namespace zoal_atc::telemetry {

// Width of one record in the `byte[512]` string arrays: 7 characters + NUL.
inline constexpr int kTcasStringSlotWidth = 8;

// AircraftIdentity rides with authoritative ownship telemetry so the console
// never has to borrow identity from anywhere else.
struct AircraftIdentity {
  std::int32_t mode_s_id = 0;
  std::string callsign;
  std::string icao_type;
};

// ownship_identity is slot 0: the Mode S id the simulator gave it, and the
// first record of the flight_id and icao_type blobs.
AircraftIdentity ownship_identity(std::int32_t mode_s_id,
                                  const std::vector<char> &flight_id,
                                  const std::vector<char> &icao_type);

// tcas_string_at reads one fixed-width record out of a byte[512] array,
// stopping at the first NUL and keeping only printable ASCII. An out-of-range
// index, or a slot holding nothing usable, yields "".
std::string tcas_string_at(const std::vector<char> &blob, int index,
                           int slot_width = kTcasStringSlotWidth);

// held_callsign is the ownship callsign to report: the one slot 0 holds now,
// or, while it holds none, the last it held. Slot 0 is the simulator's TCAS
// array, which multiplayer and traffic plugins also write. Into Sydney on
// 10 November it went blank on the ground after landing, the console then held
// a flight with no callsign, and every Ground transmission addressed nobody.
// A different callsign replaces the held one at once: that is the pilot
// changing it, not the array forgetting it.
std::string held_callsign(const std::string &read, std::string &last);

} // namespace zoal_atc::telemetry

#endif // ZOAL_ATC_TELEMETRY_OWNSHIP_IDENTITY_HPP
