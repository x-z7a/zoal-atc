#ifndef ZOAL_ATC_XPLANE_TRAFFIC_LAYER_HPP
#define ZOAL_ATC_XPLANE_TRAFFIC_LAYER_HPP

#include "zoal_atc/gui/traffic_panel.hpp"
#include "zoal_atc/telemetry/telemetry_frame.hpp"

#include <string>

// The traffic layer: other pilots and the console's AI aircraft, drawn with
// XPMP2 from the console's `traffic` frames (docs/traffic-protocol.md).
//
// xplane_plugin.cpp calls these from a handful of places and knows nothing
// else about traffic. Everything except offer_frame() and disconnected() runs
// on the simulator's main thread.
namespace zoal_atc::xplane::traffic_layer {

// Initialises XPMP2 and loads CSL models. plugin_root is the plugin folder,
// system_path X-Plane's. False (and a logged reason) when there is nothing to
// draw with, in which case the plugin does not declare traffic_v1.
bool start(const std::string &plugin_root, const std::string &system_path);
void stop();

// Takes and releases the simulator's AI/TCAS aircraft.
void enable();
void disable();

// Reader thread: true when payload was a traffic frame (now queued), so the
// caller stops looking at it.
bool offer_frame(const std::string &payload);
// Reader thread: the socket dropped; tracks are cleared on the next tick.
void disconnected();

// Flight loop: apply queued frames, expire silent tracks.
void tick();

// Telemetry: add the render fields, and drop TCAS returns that are our own
// injected aircraft so the console does not see them as strangers.
void decorate(telemetry::TelemetrySnapshot &snapshot);

// --- the panel's Traffic tab (main thread) -----------------------------------

// What the tab shows this frame: every drawn aircraft relative to own-ship,
// nearest first, `filter` applied.
gui::TrafficPanel panel(gui::TrafficFilter filter);

// Marks one aircraft (its label turns cyan); "" clears.
void track(const std::string &id);

// Chases one aircraft with the external camera; "" gives the camera back.
// The pilot changing view gives it back too.
void follow(const std::string &id);
// The chase camera: `factor` times as far (clamped), and swung `degrees`
// clockwise round the aircraft.
void chase_zoom(double factor);
void chase_orbit(double degrees);

} // namespace zoal_atc::xplane::traffic_layer

#endif // ZOAL_ATC_XPLANE_TRAFFIC_LAYER_HPP
