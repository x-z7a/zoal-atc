#ifndef ZOAL_ATC_TRAFFIC_TRAFFIC_FRAME_HPP
#define ZOAL_ATC_TRAFFIC_TRAFFIC_FRAME_HPP

#include "zoal_atc/traffic/target.hpp"

#include <optional>
#include <string>

namespace zoal_atc::traffic {

inline constexpr const char *kTrafficFrameType = "traffic";
inline constexpr const char *kTrafficCapability = "traffic_v1";

// Parses a console `traffic` frame (docs/protocol.md). Returns nullopt when the
// document is not a traffic frame. A malformed target is skipped, not fatal:
// one bad aircraft must not blank the sky.
std::optional<TrafficFrame> parse_traffic_frame(const std::string &json);

// Encodes a frame in the wire format. Used by tests and by tools that replay
// recorded traffic; the plugin itself only parses.
std::string encode_traffic_frame(const TrafficFrame &frame);

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_TRAFFIC_FRAME_HPP
