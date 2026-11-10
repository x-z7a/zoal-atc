#include "zoal_atc/gui/panel_session.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace zoal_atc::gui {

namespace {

using transport::json_array_elements;
using transport::json_escape;
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

std::optional<double> top_number(const std::string &json,
                                 std::string_view key) {
  const auto raw = json_raw_field(json, key);
  if (!raw.has_value() || raw->empty()) {
    return std::nullopt;
  }
  char *end = nullptr;
  const double value = std::strtod(raw->c_str(), &end);
  if (end == raw->c_str() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

std::uint64_t top_uint(const std::string &json, std::string_view key) {
  const auto value = top_number(json, key);
  return value.has_value() && *value > 0.0
             ? static_cast<std::uint64_t>(std::llround(*value))
             : 0U;
}

bool is_object(const std::string &json) {
  for (const char c : json) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    return c == '{';
  }
  return false;
}

std::string request_json(const std::string &action,
                         const std::string &payload_json) {
  std::string out = R"({"action":")" + json_escape(action) + "\"";
  if (!payload_json.empty()) {
    out += R"(,"payload":)" + payload_json;
  }
  return out + "}";
}

std::string quoted(const std::string &value) {
  return "\"" + json_escape(value) + "\"";
}

// Only a line stamped with when it was said can be recognised again: two
// unstamped lines with the same words may genuinely be two transmissions.
bool holds(const std::vector<HudEntry> &log, const HudEntry &entry) {
  if (entry.at.empty()) {
    return false;
  }
  for (const HudEntry &held : log) {
    if (held.at == entry.at && held.kind == entry.kind &&
        held.text == entry.text) {
      return true;
    }
  }
  return false;
}

} // namespace

// --- formatting ---------------------------------------------------------------

std::string format_thousands(double value) {
  const long long whole = std::llround(value);
  const bool negative = whole < 0;
  std::string out = std::to_string(negative ? -whole : whole);
  // Pinned to en-US grouping: aviation English, and a fixed format whatever
  // the sim machine's locale.
  for (std::size_t at = out.size(); at > 3; at -= 3) {
    out.insert(at - 3, ",");
  }
  return negative ? "-" + out : out;
}

namespace {

std::string fixed3(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", value);
  return buffer;
}

} // namespace

std::string format_frequency(double mhz) {
  return mhz != 0.0 ? fixed3(mhz) : kUnknown;
}

std::string format_standby(double mhz) {
  return mhz != 0.0 ? "sby " + fixed3(mhz) : "";
}

std::string format_altitude(const PanelTelemetry &telemetry) {
  return telemetry.on_ground
             ? "on ground"
             : format_thousands(telemetry.altitude_ft_msl) + " ft";
}

std::string format_groundspeed(double knots) {
  return std::to_string(std::llround(knots)) + " kt";
}

// An absent or stale feed is neither occupied nor clear: a bare count when
// surveillance is not current would read as "no traffic", which is the one
// thing the panel must not say when it does not know.
std::string format_traffic(const PanelTelemetry &telemetry) {
  if (telemetry.surveillance == "current") {
    return std::to_string(telemetry.traffic_count) + " contacts";
  }
  return telemetry.surveillance.empty() ? "unavailable"
                                        : telemetry.surveillance;
}

std::string format_facility_count(std::int64_t count) {
  return count == 1 ? "1 aircraft" : std::to_string(count) + " aircraft";
}

std::string format_plan_route(const std::optional<PanelFlightPlan> &plan) {
  if (!plan.has_value() || !plan->has_plan) {
    return kUnknown;
  }
  std::string route = plan->origin;
  if (!plan->destination.empty()) {
    route += route.empty() ? plan->destination
                           : " \xE2\x86\x92 " + plan->destination; // an arrow
  }
  return route.empty() ? "filed" : route;
}

std::string format_plan_detail(const std::optional<PanelFlightPlan> &plan) {
  if (!plan.has_value() || !plan->has_plan) {
    return "";
  }
  std::vector<std::string> parts;
  if (!plan->aircraft_icao.empty()) {
    parts.push_back(plan->aircraft_icao);
  }
  if (plan->cruise_alt_ft != 0.0) {
    parts.push_back(format_thousands(plan->cruise_alt_ft) + " ft");
  }
  if (!plan->first_fix.empty()) {
    parts.push_back("first " + plan->first_fix);
  }
  if (!plan->route.empty()) {
    parts.push_back(plan->route);
  }
  std::string out;
  for (const std::string &part : parts) {
    out += out.empty() ? part : " \xC2\xB7 " + part; // a middle dot
  }
  return out;
}

std::string format_dropped_events(std::uint64_t dropped) {
  return dropped > 0 ? std::to_string(dropped) + " updates dropped" : "";
}

std::string describe_link(const PanelLink &link) {
  if (!link.connected) {
    return "disconnected";
  }
  if (!link.subscribed) {
    return "subscribing";
  }
  return link.pending > 0
             ? "connected (" + std::to_string(link.pending) + " in flight)"
             : "connected";
}

std::optional<double> parse_frequency(const std::string &text) {
  const char *start = text.c_str();
  while (*start == ' ' || *start == '\t') {
    ++start;
  }
  char *end = nullptr;
  const double value = std::strtod(start, &end);
  if (end == start || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<PanelTelemetry> parse_panel_telemetry(const std::string &json) {
  if (!is_object(json)) {
    return std::nullopt;
  }
  PanelTelemetry t;
  t.frequency_mhz = top_number(json, "frequencyMhz").value_or(0.0);
  t.standby_mhz = top_number(json, "standbyMhz").value_or(0.0);
  t.altitude_ft_msl = top_number(json, "altitudeFtMsl").value_or(0.0);
  t.groundspeed_kts = top_number(json, "groundspeedKts").value_or(0.0);
  t.on_ground = top_bool(json, "onGround");
  t.paused = top_bool(json, "paused");
  t.airport = top_string(json, "airport").value_or("");
  t.surveillance = top_string(json, "surveillance").value_or("");
  t.traffic_count = static_cast<std::int64_t>(
      std::llround(top_number(json, "trafficCount").value_or(0.0)));
  return t;
}

std::optional<PanelFlightPlan> parse_panel_flight_plan(const std::string &json) {
  if (!is_object(json)) {
    return std::nullopt;
  }
  PanelFlightPlan plan;
  plan.has_plan = top_bool(json, "hasPlan");
  plan.origin = top_string(json, "origin").value_or("");
  plan.destination = top_string(json, "destination").value_or("");
  plan.aircraft_icao = top_string(json, "aircraftIcao").value_or("");
  plan.route = top_string(json, "route").value_or("");
  plan.first_fix = top_string(json, "firstFix").value_or("");
  plan.cruise_alt_ft = top_number(json, "cruiseAltFt").value_or(0.0);
  return plan;
}

const char *hud_theme_name(HudTheme theme) {
  switch (theme) {
  case HudTheme::daylight:
    return "daylight";
  case HudTheme::night:
    return "night";
  case HudTheme::graphite:
    break;
  }
  return "graphite";
}

// --- the session ----------------------------------------------------------------

PanelSession::PanelSession(GuiBridge &bridge) : bridge_(bridge) {}

void PanelSession::attach(std::uint64_t steady_ms) {
  attached_ = true;
  settings_asked_ = false;
  bridge_.attach_panel(steady_ms, GuiView::panel);
}

void PanelSession::detach() {
  if (!attached_) {
    return;
  }
  attached_ = false;
  pending_.clear();
  home_ = {};
  settings_ = {};
  local_ = {};
  bridge_.detach_panel(GuiView::panel);
}

void PanelSession::pump(std::uint64_t steady_ms, std::int64_t wall_ms) {
  if (!attached_) {
    return;
  }
  if (!settings_asked_) {
    // Read what is stored before offering to change it. Asked here rather
    // than on attach: the plugin attaches while it is still starting, before
    // the handler that answers is installed.
    settings_asked_ = true;
    send("settings", "", Pending{Request::settings_read, &local_}, steady_ms);
    send("connection_settings", "", Pending{Request::connection_read, &local_},
         steady_ms);
  }
  for (const JsMessage &message :
       bridge_.take_js_messages(kMaxMessagesPerPump, GuiView::panel)) {
    handle(message.payload, wall_ms, steady_ms);
  }
  if (armed_ && wall_ms - armed_at_ms_ >= kPanelArmedMs) {
    // A panel left alone does not stay one press away from ending the flight.
    armed_ = false;
  }
}

void PanelSession::on_settings_changed(const std::string &payload_json) {
  if (const auto theme = top_string(payload_json, "theme")) {
    theme_ = hud_theme_from_name(*theme);
  }
}

PanelFreshness PanelSession::freshness(std::int64_t wall_ms) const {
  if (!reachable()) {
    return PanelFreshness::unreachable;
  }
  if (telemetry_.has_value() && telemetry_->paused) {
    return PanelFreshness::paused;
  }
  if (last_telemetry_wall_ms_.has_value() &&
      wall_ms - *last_telemetry_wall_ms_ >= kTelemetryStaleMs) {
    return PanelFreshness::stale;
  }
  return PanelFreshness::fresh;
}

std::string PanelSession::freshness_message(std::int64_t wall_ms) const {
  switch (freshness(wall_ms)) {
  case PanelFreshness::unreachable:
    return "Console unreachable \xE2\x80\x94 values are last known";
  case PanelFreshness::paused:
    return "Simulator paused";
  case PanelFreshness::stale: {
    const auto age = wall_ms - last_telemetry_wall_ms_.value_or(wall_ms);
    return "No telemetry for " + std::to_string((age + 500) / 1000) + "s";
  }
  case PanelFreshness::fresh:
    break;
  }
  return "";
}

// --- actions --------------------------------------------------------------------

void PanelSession::tune(const std::string &text, std::uint64_t steady_ms) {
  const auto mhz = parse_frequency(text);
  if (!mhz.has_value()) {
    // The only check the panel makes, and only because an unparseable string
    // is not a frequency at all.
    home_.error = "\"" + text + "\" is not a frequency";
    return;
  }
  // Answered by the plugin, which writes COM1's active frequency dataref.
  send("tune_radio",
       R"({"mhz":)" + transport::json_number(*mhz) +
           R"(,"com":1,"target":"active"})",
       Pending{Request::tune, &home_}, steady_ms);
}

void PanelSession::transmit(const std::string &text, std::uint64_t steady_ms) {
  send("submit_text", R"({"text":)" + quoted(text) + "}",
       Pending{Request::transmit, &home_}, steady_ms);
}

void PanelSession::import_plan(std::uint64_t steady_ms) {
  // One press, no typing: the console already remembers this aircraft's pilot
  // ID, and a pilot who wants their plan on the panel should not have to visit
  // Settings to say so.
  send("refresh_flight_plan", R"({"pilotId":""})",
       Pending{Request::import_plan, &home_}, steady_ms);
}

void PanelSession::press_new_flight(std::uint64_t steady_ms,
                                    std::int64_t wall_ms) {
  if (!armed_) {
    armed_ = true;
    armed_at_ms_ = wall_ms;
    return;
  }
  armed_ = false;
  send("end_flight_session", "", Pending{Request::end_flight, &home_},
       steady_ms);
}

void PanelSession::save_token(const std::string &token,
                              std::uint64_t steady_ms) {
  send("save_connection_settings", R"({"token":)" + quoted(token) + "}",
       Pending{Request::save_token, &local_}, steady_ms);
}

void PanelSession::save_pilot_id(const std::string &pilot_id,
                                 std::uint64_t steady_ms) {
  send("save_settings", R"({"simbriefPilotId":)" + quoted(pilot_id) + "}",
       Pending{Request::save_settings, &local_}, steady_ms);
}

void PanelSession::pick_theme(HudTheme theme, std::uint64_t steady_ms) {
  Pending pending{Request::save_theme, &local_, theme_};
  theme_ = theme;
  send("save_settings",
       R"({"theme":")" + std::string(hud_theme_name(theme)) + R"("})",
       pending, steady_ms);
}

void PanelSession::import_now(std::uint64_t steady_ms) {
  // Named rather than left to the console: a console only learns a changed ID
  // on the next connection, and sending it means an import works the moment
  // it is saved.
  send("refresh_flight_plan",
       R"({"pilotId":)" + quoted(simbrief_pilot_id_) + "}",
       Pending{Request::import_plan, &settings_}, steady_ms);
}

// --- the bridge -------------------------------------------------------------------

void PanelSession::send(const std::string &action,
                        const std::string &payload_json, Pending pending,
                        std::uint64_t steady_ms) {
  PanelRunner *runner = pending.runner;
  if (runner != nullptr) {
    runner->error.clear();
  }
  if (!attached_) {
    if (runner != nullptr) {
      runner->error = "the panel is not attached to the plugin";
    }
    return;
  }
  const SubmitResult result = bridge_.submit(request_json(action, payload_json),
                                             steady_ms, GuiView::panel);
  if (!result.accepted) {
    if (runner != nullptr) {
      runner->error = top_string(result.ack_json, "error").value_or("refused");
    }
    if (pending.kind == Request::save_theme) {
      theme_ = pending.previous_theme;
    }
    if (pending.kind == Request::comm_sync) {
      arrived_during_sync_.reset();
    }
    return;
  }
  if (runner != nullptr) {
    ++runner->in_flight;
  }
  // Local actions answer inside submit, so the answer may already be queued;
  // recording the request before the next pump is all that ordering needs.
  pending_[result.request_id] = pending;
}

void PanelSession::handle(const std::string &envelope, std::int64_t wall_ms,
                          std::uint64_t steady_ms) {
  const auto kind = top_string(envelope, "kind");
  if (!kind.has_value()) {
    return;
  }
  if (*kind == "status") {
    handle_status(envelope, steady_ms);
  } else if (*kind == "event") {
    handle_event(envelope, wall_ms);
  } else if (*kind == "response") {
    handle_response(envelope);
  }
}

void PanelSession::handle_status(const std::string &envelope,
                                 std::uint64_t steady_ms) {
  const PanelLink previous = link_;
  link_.connected = top_bool(envelope, "connected");
  link_.subscribed = top_bool(envelope, "subscribed");
  link_.pending = top_uint(envelope, "pending");
  link_.dropped_events = top_uint(envelope, "dropped_events");

  const bool linked = reachable();
  const bool was_linked = previous.connected && previous.subscribed;
  const bool missed = link_.dropped_events > previous.dropped_events;
  if (!linked || (was_linked && !missed)) {
    return;
  }
  // The link came back, or lines were lost before anybody saw them: the
  // console has kept every line all along (it is the radio record), so ask it
  // rather than trust what was shown.
  if (!arrived_during_sync_.has_value()) {
    arrived_during_sync_.emplace();
    send("comm_log", R"({"limit":)" + std::to_string(kPanelCommLimit) + "}",
         Pending{Request::comm_sync, nullptr}, steady_ms);
  }
  if (!was_linked) {
    // The flight plan is not published on a schedule -- it changes when
    // somebody imports one -- so the panel asks each time the link comes up.
    send("flight_plan", "", Pending{Request::plan_read, nullptr}, steady_ms);
  }
}

void PanelSession::handle_event(const std::string &envelope,
                                std::int64_t wall_ms) {
  const auto event = top_string(envelope, "event");
  const auto payload = json_raw_field(envelope, "payload");
  if (!event.has_value() || !payload.has_value()) {
    return;
  }
  if (*event == "comm_log") {
    if (const auto entry = parse_hud_entry(*payload)) {
      append_comm(*entry);
    }
  } else if (*event == "telemetry") {
    if (auto telemetry = parse_panel_telemetry(*payload)) {
      telemetry_ = std::move(telemetry);
      last_telemetry_wall_ms_ = wall_ms;
    }
  } else if (*event == "flight_snapshot") {
    if (is_object(*payload)) {
      has_flight_ = top_bool(*payload, "hasSession");
      callsign_ = top_string(*payload, "callsign").value_or("");
      lifecycle_ = top_string(*payload, "lifecycle").value_or("");
    }
  } else if (*event == "facility_snapshot") {
    if (is_object(*payload)) {
      facility_count_ = static_cast<std::int64_t>(
          std::llround(top_number(*payload, "count").value_or(0.0)));
    }
  } else if (*event == "flight_plan") {
    if (auto plan = parse_panel_flight_plan(*payload)) {
      plan_ = std::move(plan);
    }
  }
}

void PanelSession::handle_response(const std::string &envelope) {
  const auto id = top_uint(envelope, "request_id");
  const auto found = pending_.find(id);
  if (found == pending_.end()) {
    return;
  }
  const Pending pending = found->second;
  pending_.erase(found);
  const bool ok = top_bool(envelope, "ok");
  const std::string payload = json_raw_field(envelope, "payload").value_or("null");
  if (pending.runner != nullptr) {
    pending.runner->in_flight =
        pending.runner->in_flight > 0 ? pending.runner->in_flight - 1 : 0;
    if (!ok) {
      pending.runner->error = top_string(envelope, "error").value_or("");
      if (pending.runner->error.empty()) {
        pending.runner->error = "the request was refused";
      }
    }
  }

  switch (pending.kind) {
  case Request::settings_read:
  case Request::save_settings:
    if (ok) {
      apply_settings(payload);
    }
    break;
  case Request::save_theme:
    if (ok) {
      apply_settings(payload);
    } else {
      // The screen never shows a choice the plugin did not keep.
      theme_ = pending.previous_theme;
    }
    break;
  case Request::connection_read:
  case Request::save_token:
    if (ok) {
      apply_connection(payload);
    }
    break;
  case Request::comm_sync:
    if (ok) {
      finish_comm_sync(payload);
    } else {
      // A console that cannot answer leaves the window showing what it has.
      arrived_during_sync_.reset();
    }
    break;
  case Request::plan_read:
  case Request::import_plan:
    if (ok) {
      if (auto plan = parse_panel_flight_plan(payload)) {
        plan_ = std::move(plan);
      }
    }
    break;
  case Request::end_flight:
    if (ok) {
      // A different journey under the same flight ID: the console clears its
      // record of the last one, and the window starts again with it.
      set_comm({});
    }
    break;
  case Request::tune:
  case Request::transmit:
    break;
  }
}

void PanelSession::apply_settings(const std::string &payload_json) {
  if (!is_object(payload_json)) {
    return;
  }
  settings_loaded_ = true;
  simbrief_pilot_id_ = top_string(payload_json, "simbriefPilotId").value_or("");
  if (const auto theme = top_string(payload_json, "theme")) {
    theme_ = hud_theme_from_name(*theme);
  }
  ++settings_revision_;
}

void PanelSession::apply_connection(const std::string &payload_json) {
  if (!is_object(payload_json)) {
    return;
  }
  connection_loaded_ = true;
  token_ = top_string(payload_json, "token").value_or("");
  console_url_ = top_string(payload_json, "url").value_or("");
  ++connection_revision_;
}

void PanelSession::append_comm(const HudEntry &entry) {
  if (arrived_during_sync_.has_value()) {
    arrived_during_sync_->push_back(entry);
  }
  if (holds(comm_, entry)) {
    // The plugin replays the newest line on attach, and a line the window
    // already has is not a second transmission.
    return;
  }
  std::vector<HudEntry> next = comm_;
  next.push_back(entry);
  set_comm(std::move(next));
}

void PanelSession::finish_comm_sync(const std::string &payload_json) {
  std::vector<HudEntry> since =
      arrived_during_sync_.value_or(std::vector<HudEntry>{});
  arrived_during_sync_.reset();
  const auto elements = json_array_elements(payload_json);
  if (!elements.has_value()) {
    return;
  }
  std::vector<HudEntry> next;
  auto add = [&](const HudEntry &entry) {
    if (!holds(next, entry)) {
      next.push_back(entry);
    }
  };
  for (const std::string &element : *elements) {
    if (const auto entry = parse_hud_entry(element)) {
      add(*entry);
    }
  }
  for (const HudEntry &entry : since) {
    add(entry);
  }
  set_comm(std::move(next));
}

void PanelSession::set_comm(std::vector<HudEntry> entries) {
  if (entries.size() > kPanelCommLimit) {
    entries.erase(entries.begin(),
                  entries.end() - static_cast<std::ptrdiff_t>(kPanelCommLimit));
  }
  if (entries.empty() && comm_.empty()) {
    return;
  }
  comm_ = std::move(entries);
  ++comm_revision_;
}

} // namespace zoal_atc::gui
