#include "zoal_atc/gui/notification.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <algorithm>

namespace zoal_atc::gui {

namespace {

using zoal_atc::transport::json_bool_field;
using zoal_atc::transport::json_number_field;
using zoal_atc::transport::json_string_field;

// The event a transmission arrives as. The console publishes one per reply,
// already addressed to this aircraft.
constexpr const char *kATCReplyEvent = "atc_reply";

// A toast has room for a line or two. A long clearance is truncated rather than
// wrapped away to nothing, and the panel still holds the whole thing.
constexpr std::string::size_type kMaxBodyChars = 240;

bool is_known_corner(const std::string &corner) {
  return corner == "top_left" || corner == "top_right" ||
         corner == "bottom_left" || corner == "bottom_right";
}

std::string truncate(const std::string &text) {
  if (text.size() <= kMaxBodyChars) {
    return text;
  }
  return text.substr(0, kMaxBodyChars - 1) + "…";
}

// title_for names the seat that spoke, because "Ground" and "Tower" are
// different people to a pilot and the difference is the first thing they need.
// The console sends the position as its own identifier; this is the only place
// that turns one into words.
std::string title_for(const std::string &position) {
  if (position == "clearance_delivery") {
    return "Clearance Delivery";
  }
  if (position == "ground") {
    return "Ground";
  }
  if (position == "tower") {
    return "Tower";
  }
  if (position == "departure") {
    return "Departure";
  }
  if (position == "approach") {
    return "Approach";
  }
  if (position == "center") {
    return "Center";
  }
  if (position == "oceanic") {
    return "Oceanic";
  }
  if (position == "ramp" || position == "ramp_control") {
    return "Ramp";
  }
  if (position == "fbo_or_handler") {
    return "FBO";
  }
  if (position == "unicom_ctaf") {
    return "UNICOM";
  }
  // No position, or one this plugin predates. "ATC" is true of all of them.
  return "ATC";
}

} // namespace

NotificationControl parse_notification_control(const std::string &json) {
  NotificationControl control;
  const auto type = json_string_field(json, "type");
  if (!type.has_value() || *type != "notification_control") {
    return control; // not a control frame
  }
  control.valid = true;

  if (const auto enabled = json_bool_field(json, "enabled")) {
    control.prefs.enabled = *enabled;
  }
  if (const auto corner = json_string_field(json, "corner")) {
    // An unknown corner is a console the plugin does not understand, not a
    // reason to put the toast somewhere arbitrary.
    if (is_known_corner(*corner)) {
      control.prefs.corner = *corner;
    }
  }
  if (const auto timeout = json_number_field(json, "timeout_secs")) {
    // Zero means "until dismissed", which over a hidden browser means forever,
    // because nothing on the toast can be clicked. Clamped to something a
    // pilot can read and be rid of.
    control.prefs.timeout_secs = std::clamp(*timeout, 2.0, 30.0);
  }
  if (const auto sound = json_bool_field(json, "sound")) {
    control.prefs.sound = *sound;
  }
  return control;
}

void NotificationController::apply(const NotificationControl &control) {
  if (!control.valid) {
    return;
  }
  commanded = true;
  commanded_prefs = control.prefs;
}

void NotificationController::reset() {
  commanded = false;
  commanded_prefs = NotificationPreferences{};
}

NotificationPreferences NotificationController::preferences() const {
  return commanded ? commanded_prefs : local;
}

NotificationRequest notification_for(const GuiEvent &event,
                                     const NotificationPreferences &prefs,
                                     bool panel_visible) {
  NotificationRequest request;

  if (!prefs.enabled) {
    return request;
  }
  // The pilot is looking at the panel, which is showing the radio log. A toast
  // over it would repeat what is already on screen.
  if (panel_visible) {
    return request;
  }
  if (event.event != kATCReplyEvent) {
    return request;
  }

  const auto text = json_string_field(event.payload_json, "text");
  if (!text.has_value() || text->empty()) {
    // Nothing was said. An empty toast is worse than none.
    return request;
  }

  request.raise = true;
  request.title = title_for(json_string_field(event.payload_json, "spokenBy").value_or(""));
  request.body = truncate(*text);
  return request;
}

} // namespace zoal_atc::gui
