#ifndef ZOAL_ATC_TAXI_SCENERY_MATCH_HPP
#define ZOAL_ATC_TAXI_SCENERY_MATCH_HPP

#include <string>
#include <vector>

// Whether to light a route drawn from scenery the pilot may not have.
//
// The console draws a route from its own installation's apt.dat. A pilot
// with other scenery for the field has other pavement, and the lights can
// land on the grass. The pilot decides (zoal_atc.cfg `taxi_lights`):
// always (the default), only when the scenery matches, or never.
namespace zoal_atc::taxi {

enum class LightsMode { Always, MatchingScenery, Off };

// "always", "matching" and "off", case-insensitive, surrounding spaces
// ignored. Anything else, including empty, is Always.
LightsMode parse_lights_mode(const std::string &text);
// "always", "matching", "off".
const char *lights_mode_name(LightsMode mode);

inline constexpr const char *kGlobalAirports = "Global Airports";

// The scenery packs of Custom Scenery/scenery_packs.ini in the order X-Plane
// draws them (first wins): each enabled `SCENERY_PACK <path>` line's path,
// trailing '/' or '\\' removed, '\\' turned to '/'. The `*GLOBAL_AIRPORTS*`
// entry is returned as kGlobalAirports. `SCENERY_PACK_DISABLED` lines, the
// header and blank lines are skipped. Lines may end in "\r\n".
std::vector<std::string> scenery_pack_order(const std::string &ini_text);

// Whether apt.dat text holds the airport: a header row of code 1, 16 or 17
// whose fifth whitespace-separated field is icao (case-insensitive).
bool apt_dat_has_airport(const std::string &apt_text, const std::string &icao);

// The scenery X-Plane draws icao from, named as the console names it: the
// folder name (last path component) of the first pack, in
// scenery_pack_order of <xplane_root>/Custom Scenery/scenery_packs.ini,
// whose "Earth nav data/apt.dat" has it, or kGlobalAirports once the
// *GLOBAL_AIRPORTS* entry is reached or no pack has it. A relative pack path
// is under xplane_root; an absolute one is used as it is. A missing or
// unreadable file is a pack without the airport.
std::string scenery_for_airport(const std::string &xplane_root, const std::string &icao);

// Off: never. Always: always. MatchingScenery: only when route_scenery is
// not empty and equals own_scenery exactly.
bool should_light(LightsMode mode, const std::string &route_scenery,
                  const std::string &own_scenery);

} // namespace zoal_atc::taxi

#endif // ZOAL_ATC_TAXI_SCENERY_MATCH_HPP
