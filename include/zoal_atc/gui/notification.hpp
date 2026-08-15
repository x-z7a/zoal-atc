#ifndef ZOAL_ATC_GUI_NOTIFICATION_HPP
#define ZOAL_ATC_GUI_NOTIFICATION_HPP

#include "zoal_atc/gui/gui_frames.hpp"

#include <string>

namespace zoal_atc::gui {

// Telling a pilot ATC just spoke, when they are not looking at the panel.
//
// This is presentation, not ATC logic: nothing here decides anything about a
// transmission, it displays one that already happened. The console still
// authorizes; the plugin stays thin.
//
// SDK-free and Skyscript-free on purpose. Everything below is a pure function
// of a frame, a preference and a visibility flag, so the whole policy is
// table-testable without a simulator, and the only part that touches Skyscript
// is the call that hands it a title and a body.

// NotificationPreferences is what the pilot chose, as it arrives from the
// console. Corner is carried as the console's string rather than a Skyscript
// enum so this core links without Skyscript; the edge maps it.
struct NotificationPreferences {
  bool enabled = true;
  // "top_left" | "top_right" | "bottom_left" | "bottom_right".
  std::string corner = "top_right";
  // Seconds on screen. Kept above zero: over a hidden browser nothing on the
  // toast can be clicked, so a notification that waits to be dismissed would
  // never leave.
  double timeout_secs = 8.0;
  bool sound = true;
};

// NotificationControl is the console->plugin command decoded from a
// `notification_control` frame, mirroring telemetry_control: the console owns
// the preference, the plugin honours it until superseded or the link drops.
struct NotificationControl {
  NotificationPreferences prefs;
  bool valid = false; // false when the payload was not a control frame
};

// parse_notification_control decodes a `notification_control` frame. A frame of
// any other type (or malformed JSON) returns {valid=false}. Absent fields keep
// their defaults, so a console that sends only `enabled` still works.
NotificationControl parse_notification_control(const std::string &json);

// NotificationController holds the commanded preference, falling back to the
// defaults when none has arrived or the console has gone away.
struct NotificationController {
  NotificationPreferences local{}; // the fallback baseline
  bool commanded = false;
  NotificationPreferences commanded_prefs{};

  // apply adopts a decoded command (ignores an invalid one).
  void apply(const NotificationControl &control);
  // reset clears the console command, reverting to the local defaults. Call on
  // console loss: a pilot who turned notifications off should not have them
  // return because the socket dropped, but neither should a stale command
  // outlive the console that sent it -- the local default is "on", which is the
  // safe direction for something whose failure mode is silence.
  void reset();
  // preferences is the effective setting.
  [[nodiscard]] NotificationPreferences preferences() const;
};

// NotificationRequest is a toast to raise, or nothing.
struct NotificationRequest {
  bool raise = false;
  std::string title;
  std::string body;
};

// notification_for decides whether one cached console event is worth a toast.
//
// The rule is the one the feature exists for: notify about what the pilot
// cannot already see. A visible panel is showing the radio log, so it raises
// nothing; a hidden one raises the transmission. That also keeps the plugin out
// of the browser's business -- it knows whether its own window is up, and does
// not need the page to tell it which tab is open.
NotificationRequest notification_for(const GuiEvent &event,
                                     const NotificationPreferences &prefs,
                                     bool panel_visible);

} // namespace zoal_atc::gui

#endif
