#include "zoal_atc/gui/gui_frames.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <sstream>

namespace zoal_atc::gui {

namespace {

using zoal_atc::transport::json_escape;
using zoal_atc::transport::json_raw_field;
using zoal_atc::transport::json_unquote;

// json_value returns a payload that is safe to inline as a JSON value. An empty
// payload becomes `null` rather than nothing, so a frame is never malformed and
// the JavaScript statement Skyscript builds around it never fails to parse.
std::string json_value(const std::string &raw) {
  return raw.empty() ? "null" : raw;
}

std::optional<std::string> string_field(const std::string &json,
                                        const char *key) {
  const auto raw = json_raw_field(json, key);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  return json_unquote(*raw);
}

bool frame_is(const std::string &json, const char *type) {
  const auto found = string_field(json, "type");
  return found.has_value() && *found == type;
}

std::optional<std::uint64_t> uint_field(const std::string &json,
                                        const char *key) {
  const auto raw = json_raw_field(json, key);
  if (!raw.has_value() || raw->empty()) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char ch : *raw) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<std::uint64_t>(ch - '0');
  }
  return value;
}

bool bool_field(const std::string &json, const char *key) {
  const auto raw = json_raw_field(json, key);
  return raw.has_value() && *raw == "true";
}

} // namespace

std::string build_gui_request(const GuiRequest &request) {
  std::ostringstream frame;
  frame << R"({"type":"gui_request","request_id":)" << request.request_id
        << R"(,"action":")" << json_escape(request.action) << R"(","payload":)"
        << json_value(request.payload_json) << "}";
  return frame.str();
}

std::string build_gui_subscribe(std::uint64_t request_id,
                                const std::vector<std::string> &topics) {
  std::ostringstream frame;
  frame << R"({"type":"gui_subscribe","request_id":)" << request_id
        << R"(,"topics":[)";
  for (std::size_t i = 0; i < topics.size(); ++i) {
    if (i > 0) {
      frame << ",";
    }
    frame << "\"" << json_escape(topics[i]) << "\"";
  }
  frame << "]}";
  return frame.str();
}

std::optional<GuiResponse> parse_gui_response(const std::string &frame) {
  if (!frame_is(frame, "gui_response")) {
    return std::nullopt;
  }
  const auto request_id = uint_field(frame, "request_id");
  if (!request_id.has_value()) {
    // Nothing to correlate it with, so there is no panel promise to settle.
    return std::nullopt;
  }
  GuiResponse response;
  response.request_id = *request_id;
  response.ok = bool_field(frame, "ok");
  if (const auto payload = json_raw_field(frame, "payload");
      payload.has_value() && *payload != "null") {
    response.payload_json = *payload;
  }
  response.error = string_field(frame, "error").value_or("");
  return response;
}

std::optional<GuiEvent> parse_gui_event(const std::string &frame) {
  if (!frame_is(frame, "gui_event")) {
    return std::nullopt;
  }
  const auto name = string_field(frame, "event");
  if (!name.has_value() || name->empty()) {
    // The name is the cache key and the JS listener's switch; an unnamed event
    // is not deliverable to anything.
    return std::nullopt;
  }
  GuiEvent event;
  event.event = *name;
  if (const auto payload = json_raw_field(frame, "payload");
      payload.has_value() && *payload != "null") {
    event.payload_json = *payload;
  }
  return event;
}

bool is_gui_frame(const std::string &frame) {
  const auto type = string_field(frame, "type");
  return type.has_value() && type->rfind("gui_", 0) == 0;
}

std::optional<PanelRequest> parse_panel_request(const std::string &message) {
  const auto action = string_field(message, "action");
  if (!action.has_value() || action->empty()) {
    return std::nullopt;
  }
  PanelRequest request;
  request.action = *action;
  if (const auto payload = json_raw_field(message, "payload");
      payload.has_value() && *payload != "null") {
    request.payload_json = *payload;
  }
  return request;
}

std::string build_js_response(const GuiResponse &response) {
  std::ostringstream envelope;
  envelope << R"({"kind":"response","request_id":)" << response.request_id
           << R"(,"ok":)" << (response.ok ? "true" : "false")
           << R"(,"payload":)" << json_value(response.payload_json)
           << R"(,"error":")" << json_escape(response.error) << "\"}";
  return envelope.str();
}

std::string build_js_event(const GuiEvent &event) {
  std::ostringstream envelope;
  envelope << R"({"kind":"event","event":")" << json_escape(event.event)
           << R"(","payload":)" << json_value(event.payload_json) << "}";
  return envelope.str();
}

std::string build_js_status(const GuiStatus &status) {
  std::ostringstream envelope;
  envelope << R"({"kind":"status","connected":)"
           << (status.connected ? "true" : "false") << R"(,"subscribed":)"
           << (status.subscribed ? "true" : "false") << R"(,"pending":)"
           << status.pending << R"(,"dropped_events":)"
           << status.dropped_events << "}";
  return envelope.str();
}

std::string build_request_ack(bool accepted, std::uint64_t request_id,
                              const std::string &error) {
  std::ostringstream ack;
  ack << R"({"accepted":)" << (accepted ? "true" : "false")
      << R"(,"request_id":)" << request_id << R"(,"error":")"
      << json_escape(error) << "\"}";
  return ack.str();
}

} // namespace zoal_atc::gui
