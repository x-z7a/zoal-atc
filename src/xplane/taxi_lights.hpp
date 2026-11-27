#ifndef ZOAL_ATC_XPLANE_TAXI_LIGHTS_HPP
#define ZOAL_ATC_XPLANE_TAXI_LIGHTS_HPP

#include "zoal_atc/taxi/scenery_match.hpp"

#include <string>

// Follow the greens: the taxi route Ground cleared, once read back, drawn as
// green lead-on lights on the stretch ahead of the aircraft and a red stop bar
// at the hold short (docs/taxi-route-protocol.md). Only this cockpit's own
// route, and only in this sim: another pilot at the same field sees theirs.
//
// Wiring only. What is lit and where is zoal_atc::taxi; this places the
// objects. Everything except offer_frame() runs on the main thread.
namespace zoal_atc::xplane::taxi_lights {

// Loads the light objects from <plugin_root>/objects. False (and a logged
// reason) when the pilot switched the lights off or the objects are missing,
// in which case the plugin does not declare taxi_route_v1.
bool start(const std::string &plugin_root, const std::string &system_path,
           taxi::LightsMode mode);
void stop();

// Reader thread: true when payload was a taxi_route frame (now queued).
bool offer_frame(const std::string &payload);

// Flight loop: apply queued routes, light the stretch ahead.
void tick();

} // namespace zoal_atc::xplane::taxi_lights

#endif // ZOAL_ATC_XPLANE_TAXI_LIGHTS_HPP
