#include "zoal_atc/telemetry/ownship_identity.hpp"

#include <algorithm>

namespace zoal_atc::telemetry {

AircraftIdentity ownship_identity(std::int32_t mode_s_id,
                                  const std::vector<char> &flight_id,
                                  const std::vector<char> &icao_type) {
  AircraftIdentity out;
  out.mode_s_id = mode_s_id;
  out.callsign = tcas_string_at(flight_id, 0);
  out.icao_type = tcas_string_at(icao_type, 0);
  return out;
}

std::string tcas_string_at(const std::vector<char> &blob, int index,
                           int slot_width) {
  if (index < 0 || slot_width <= 0) {
    return {};
  }
  const int base = index * slot_width;
  if (base < 0 || base >= static_cast<int>(blob.size())) {
    return {};
  }
  const int end =
      std::min(base + slot_width, static_cast<int>(blob.size()));

  std::string out;
  out.reserve(static_cast<std::size_t>(slot_width));
  for (int i = base; i < end; ++i) {
    const char c = blob[static_cast<std::size_t>(i)];
    if (c == '\0') {
      break; // NUL terminates the record
    }
    // Printable ASCII only: a junk byte must not corrupt the JSON frame.
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 0x20 && uc < 0x7F) {
      out.push_back(c);
    }
  }
  // Trim trailing spaces; some feeds pad rather than terminate.
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

std::string held_callsign(const std::string &read, std::string &last) {
  if (!read.empty()) {
    last = read;
  }
  return last;
}

} // namespace zoal_atc::telemetry
