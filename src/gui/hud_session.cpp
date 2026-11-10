#include "zoal_atc/gui/hud_session.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <optional>
#include <utility>

namespace zoal_atc::gui {

namespace {

using transport::json_raw_field;
using transport::json_unquote;

// Messages drained per frame. The bridge caps the queue; this caps the work.
constexpr std::size_t kMaxMessagesPerPump = 64;

// Top-level fields only: an envelope wraps a console payload with keys of its
// own ("kind" in a comm_log entry), which must not answer for the envelope.
std::optional<std::string> top_string(const std::string &json,
                                      std::string_view key) {
  const auto raw = json_raw_field(json, key);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  return json_unquote(*raw);
}

bool top_bool(const std::string &json, std::string_view key) {
  const auto raw = json_raw_field(json, key);
  return raw.has_value() && *raw == "true";
}

std::optional<std::uint64_t> top_uint(const std::string &json,
                                      std::string_view key) {
  const auto raw = json_raw_field(json, key);
  if (!raw.has_value() || raw->empty()) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : *raw) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return value;
}

const char *action_name(HudAction action) {
  switch (action) {
  case HudAction::reload_plan:
    return "refresh_flight_plan";
  case HudAction::end_flight:
    return "end_flight_session";
  case HudAction::open_panel:
    return "open_panel";
  case HudAction::none:
    break;
  }
  return "";
}

std::string request_json(const std::string &action) {
  // The reload names no pilot ID: the plugin's own SimBrief ID is used, as the
  // React HUD's empty pilotId asked for.
  if (action == "refresh_flight_plan") {
    return R"({"action":"refresh_flight_plan","payload":{"pilotId":""}})";
  }
  return R"({"action":")" + transport::json_escape(action) + R"("})";
}

} // namespace

HudTheme hud_theme_from_name(const std::string &name) {
  if (name == "daylight") {
    return HudTheme::daylight;
  }
  if (name == "night") {
    return HudTheme::night;
  }
  return HudTheme::graphite;
}

std::optional<HudEntry> parse_hud_entry(const std::string &payload_json) {
  HudEntry entry;
  entry.text = top_string(payload_json, "text").value_or("");
  if (entry.text.empty()) {
    return std::nullopt;
  }
  entry.kind = top_string(payload_json, "kind").value_or("");
  entry.at = top_string(payload_json, "at").value_or("");
  entry.spoken_by = top_string(payload_json, "spokenBy").value_or("");
  entry.addressed_to = top_string(payload_json, "addressedTo").value_or("");
  entry.overheard = top_bool(payload_json, "overheard");
  return entry;
}

HudSession::HudSession(GuiBridge &bridge) : bridge_(bridge) {}

void HudSession::attach(std::uint64_t steady_ms) {
  attached_ = true;
  menu_.set_ready(true);
  bridge_.attach_panel(steady_ms, GuiView::hud);
  settings_asked_ = false;
}

void HudSession::detach() {
  if (!attached_) {
    return;
  }
  attached_ = false;
  menu_.set_ready(false);
  pending_.clear();
  bridge_.detach_panel(GuiView::hud);
}

void HudSession::set_visible(bool visible, std::uint64_t steady_ms) {
  if (attached_) {
    bridge_.set_panel_visible(visible, steady_ms, GuiView::hud);
  }
}

void HudSession::pump(std::uint64_t steady_ms, std::int64_t wall_ms) {
  if (attached_ && !settings_asked_) {
    // The theme is the plugin's to say; until it answers, the default stands.
    // Asked here rather than on attach: the plugin attaches the HUD while it
    // is still starting, before the handler that answers is installed.
    settings_asked_ = true;
    std::string ignored;
    send("settings", Pending{Request::settings, HudAction::none}, steady_ms,
         ignored);
  }
  if (attached_) {
    for (const JsMessage &message :
         bridge_.take_js_messages(kMaxMessagesPerPump, GuiView::hud)) {
      handle(message.payload, wall_ms);
    }
  }
  menu_.tick(wall_ms);
  feed_.expire(wall_ms);
}

void HudSession::on_settings_changed(const std::string &payload_json) {
  if (const auto theme = top_string(payload_json, "theme")) {
    theme_ = hud_theme_from_name(*theme);
  }
}

void HudSession::press_reload(std::uint64_t steady_ms, std::int64_t wall_ms) {
  run(menu_.press_reload(wall_ms), steady_ms, wall_ms);
}

void HudSession::press_new_flight(std::uint64_t steady_ms,
                                  std::int64_t wall_ms) {
  run(menu_.press_new_flight(wall_ms), steady_ms, wall_ms);
}

void HudSession::press_open_panel(std::uint64_t steady_ms) {
  run(menu_.press_open_panel(), steady_ms, 0);
}

void HudSession::run(HudAction action, std::uint64_t steady_ms,
                     std::int64_t) {
  if (action == HudAction::none) {
    return;
  }
  std::string error;
  if (!send(action_name(action), Pending{Request::action, action}, steady_ms,
            error)) {
    menu_.action_failed(action, error);
  }
}

bool HudSession::send(const std::string &action, Pending pending,
                      std::uint64_t steady_ms, std::string &error) {
  if (!attached_) {
    error = "the HUD is not attached to the console";
    return false;
  }
  const SubmitResult result =
      bridge_.submit(request_json(action), steady_ms, GuiView::hud);
  if (!result.accepted) {
    error = top_string(result.ack_json, "error").value_or("");
    return false;
  }
  // Local actions answer inside submit, so the answer may already be queued;
  // recording the request before the next pump is all that ordering needs.
  pending_[result.request_id] = pending;
  return true;
}

void HudSession::handle(const std::string &envelope, std::int64_t wall_ms) {
  const auto kind = top_string(envelope, "kind");
  if (!kind.has_value()) {
    return;
  }
  if (*kind == "status") {
    handle_status(envelope);
  } else if (*kind == "event") {
    handle_event(envelope, wall_ms);
  } else if (*kind == "response") {
    handle_response(envelope, wall_ms);
  }
}

void HudSession::handle_status(const std::string &envelope) {
  reachable_ = top_bool(envelope, "connected") && top_bool(envelope, "subscribed");
}

void HudSession::handle_event(const std::string &envelope,
                              std::int64_t wall_ms) {
  const auto event = top_string(envelope, "event");
  const auto payload = json_raw_field(envelope, "payload");
  if (!event.has_value() || !payload.has_value()) {
    return;
  }
  if (*event == "comm_log") {
    if (const auto entry = parse_hud_entry(*payload)) {
      feed_.add(*entry, wall_ms);
    }
  } else if (*event == "flight_snapshot") {
    callsign_ = top_string(*payload, "callsign").value_or("");
  }
}

void HudSession::handle_response(const std::string &envelope,
                                 std::int64_t wall_ms) {
  const auto id = top_uint(envelope, "request_id");
  if (!id.has_value()) {
    return;
  }
  const auto found = pending_.find(*id);
  if (found == pending_.end()) {
    return;
  }
  const Pending pending = found->second;
  pending_.erase(found);
  const bool ok = top_bool(envelope, "ok");

  if (pending.kind == Request::settings) {
    if (ok) {
      if (const auto payload = json_raw_field(envelope, "payload")) {
        on_settings_changed(*payload);
      }
    }
    return;
  }
  if (!ok) {
    menu_.action_failed(pending.action,
                        top_string(envelope, "error").value_or(""));
    return;
  }
  menu_.action_succeeded(pending.action, wall_ms);
  if (pending.action == HudAction::end_flight) {
    // A new flight: what the last one was told is not this one's business.
    feed_.clear();
  }
}

} // namespace zoal_atc::gui
