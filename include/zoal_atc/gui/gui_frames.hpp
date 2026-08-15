#ifndef ZOAL_ATC_GUI_GUI_FRAMES_HPP
#define ZOAL_ATC_GUI_GUI_FRAMES_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The GUI half of the plugin<->console wire protocol, plus the plugin<->browser
// envelopes Skyscript carries.
//
// Three hops, one request:
//
//   browser  --console.request-->  plugin  --gui_request-->  console
//   browser  <--console.event---   plugin  <--gui_response-  console
//
// The console payload is opaque here. The plugin forwards it verbatim in both
// directions and never parses it: the GUI is a projection of console state, and
// a second copy of that state in C++ is exactly what the phase must not grow.
//
// No frame carries a flight id. A GUI request is attributed to the socket it
// arrived on, which is what makes "the browser may request my session; it may
// not name another flight and get it" true by construction rather than by check.
namespace zoal_atc::gui {

// GuiRequest is one panel action or query awaiting a console answer.
struct GuiRequest {
  std::uint64_t request_id = 0;
  std::string action;
  // Raw JSON value. Empty means the panel sent no payload.
  std::string payload_json;
};

std::string build_gui_request(const GuiRequest &request);

// build_gui_subscribe asks the console for the initial snapshots and the event
// stream. It is re-sent on every reconnect: the console holds no memory of a
// socket that went away.
std::string build_gui_subscribe(std::uint64_t request_id,
                                const std::vector<std::string> &topics);

// GuiResponse is the console's answer to one GuiRequest, correlated by id.
struct GuiResponse {
  std::uint64_t request_id = 0;
  bool ok = false;
  std::string payload_json;
  std::string error;
};

std::optional<GuiResponse> parse_gui_response(const std::string &frame);

// GuiEvent is a server-pushed update nobody asked for by id.
struct GuiEvent {
  std::string event;
  std::string payload_json;
};

std::optional<GuiEvent> parse_gui_event(const std::string &frame);

// is_gui_frame reports whether a console frame belongs to the GUI at all. The
// receiver loop routes on this rather than on a successful parse, so a
// malformed gui_* frame is dropped by the GUI instead of falling through to the
// telemetry and navdata parsers behind it.
bool is_gui_frame(const std::string &frame);

// PanelRequest is the console.request envelope the panel's JavaScript posts.
struct PanelRequest {
  std::string action;
  std::string payload_json;
};

std::optional<PanelRequest> parse_panel_request(const std::string &message);

// GuiStatus is what the panel needs to tell the truth about itself while the
// console is down, recovering, or has not yet answered.
struct GuiStatus {
  bool connected = false;
  bool subscribed = false;
  std::size_t pending = 0;
  std::size_t dropped_events = 0;
};

// The console.event envelopes. Skyscript inlines the payload into a JavaScript
// statement, so every builder emits a complete JSON value - `null` rather than
// nothing when there is no payload.
std::string build_js_response(const GuiResponse &response);
std::string build_js_event(const GuiEvent &event);
std::string build_js_status(const GuiStatus &status);

// build_request_ack is what a console.request call resolves to. It is only the
// receipt: the answer arrives later on console.event, because the Skyscript
// handler runs on the X-Plane main thread and must never wait on a socket.
std::string build_request_ack(bool accepted, std::uint64_t request_id,
                              const std::string &error);

} // namespace zoal_atc::gui

#endif // ZOAL_ATC_GUI_GUI_FRAMES_HPP
