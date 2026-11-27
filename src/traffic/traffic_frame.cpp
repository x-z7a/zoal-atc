#include "zoal_atc/traffic/traffic_frame.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <cmath>

namespace zoal_atc::traffic {

namespace json = zoal_atc::transport;

TrafficKind parse_kind(const std::string &text) {
  if (text == "participant") {
    return TrafficKind::Participant;
  }
  if (text == "ai") {
    return TrafficKind::Ai;
  }
  return TrafficKind::Unknown;
}

const char *kind_name(TrafficKind kind) {
  switch (kind) {
  case TrafficKind::Participant:
    return "participant";
  case TrafficKind::Ai:
    return "ai";
  case TrafficKind::Unknown:
    break;
  }
  return "unknown";
}

namespace {

double number_or(const std::string &json_text, const char *key,
                 double fallback) {
  auto value = json::json_number_field(json_text, key);
  return value && std::isfinite(*value) ? *value : fallback;
}

std::string string_or_empty(const std::string &json_text, const char *key) {
  return json::json_string_field(json_text, key).value_or(std::string{});
}

std::optional<Target> parse_target(const std::string &element) {
  Target target;
  auto id = json::json_string_field(element, "id");
  auto lat = json::json_number_field(element, "lat_deg");
  auto lon = json::json_number_field(element, "lon_deg");
  auto alt = json::json_number_field(element, "alt_ft_msl");
  auto t_ms = json::json_number_field(element, "t_ms");
  if (!id || id->empty() || !lat || !lon || !alt || !t_ms) {
    return std::nullopt;
  }
  if (std::fabs(*lat) > 90.0 || std::fabs(*lon) > 180.0) {
    return std::nullopt;
  }

  Identity &who = target.identity;
  who.id = *id;
  who.kind = parse_kind(string_or_empty(element, "kind"));
  who.callsign = string_or_empty(element, "callsign");
  who.icao_type = string_or_empty(element, "icao_type");
  who.airline = string_or_empty(element, "airline");
  who.livery = string_or_empty(element, "livery");
  who.destination = string_or_empty(element, "destination");
  who.mode_s = static_cast<std::uint32_t>(
      json::json_uint_field(element, "mode_s").value_or(0) & 0xFFFFFFu);

  Sample &s = target.sample;
  s.t_s = *t_ms / 1000.0;
  s.lat_deg = *lat;
  s.lon_deg = *lon;
  s.alt_ft_msl = *alt;
  s.heading_deg = number_or(element, "heading_deg", 0.0);
  s.pitch_deg = number_or(element, "pitch_deg", 0.0);
  s.roll_deg = number_or(element, "roll_deg", 0.0);
  s.groundspeed_kts = number_or(element, "groundspeed_kts", 0.0);
  s.vertical_speed_fpm = number_or(element, "vertical_speed_fpm", 0.0);
  s.track_deg = number_or(element, "track_deg", s.heading_deg);
  s.on_ground = json::json_bool_field(element, "on_ground").value_or(false);
  s.gear = number_or(element, "gear", s.on_ground ? 1.0 : 0.0);
  s.flaps = number_or(element, "flaps", 0.0);
  s.speedbrake = number_or(element, "speedbrake", 0.0);
  s.thrust = number_or(element, "thrust", 0.0);
  s.lights = static_cast<std::uint32_t>(
      json::json_uint_field(element, "lights").value_or(0));
  if (auto delay = json::json_number_field(element, "render_delay_ms");
      delay && *delay >= 0.0 && *delay <= 10000.0) {
    target.render_delay_s = *delay / 1000.0;
  }
  return target;
}

} // namespace

std::optional<TrafficFrame> parse_traffic_frame(const std::string &json_text) {
  // Envelope keys are read only where they lead the document; per-target keys
  // are read per element, so the client's flat key search never crosses from
  // one into the other.
  auto type = json::json_string_field(json_text, "type");
  if (!type || *type != kTrafficFrameType) {
    return std::nullopt;
  }

  TrafficFrame frame;
  frame.seq = json::json_uint_field(json_text, "seq").value_or(0);
  frame.server_time_s =
      number_or(json_text, "server_time_ms", 0.0) / 1000.0;

  if (auto raw = json::json_raw_field(json_text, "targets")) {
    if (auto elements = json::json_array_elements(*raw)) {
      frame.targets.reserve(elements->size());
      for (const auto &element : *elements) {
        if (auto target = parse_target(element)) {
          frame.targets.push_back(std::move(*target));
        }
      }
    }
  }
  frame.removed = json::json_string_array_field(json_text, "removed");
  return frame;
}

std::string encode_traffic_frame(const TrafficFrame &frame) {
  using json::json_escape;
  using json::json_number;
  std::string out = "{\"type\":\"traffic\",\"seq\":" +
                    std::to_string(frame.seq) + ",\"server_time_ms\":" +
                    std::to_string(static_cast<long long>(std::llround(frame.server_time_s * 1000.0))) +
                    ",\"targets\":[";
  for (std::size_t i = 0; i < frame.targets.size(); ++i) {
    const auto &who = frame.targets[i].identity;
    const auto &s = frame.targets[i].sample;
    if (i > 0) {
      out += ",";
    }
    out += "{\"id\":\"" + json_escape(who.id) + "\"";
    out += ",\"kind\":\"" + std::string(kind_name(who.kind)) + "\"";
    // Milliseconds are integers: %.9g would round a Unix-epoch value to 10 ms.
    out += ",\"t_ms\":" + std::to_string(static_cast<long long>(std::llround(s.t_s * 1000.0)));
    out += ",\"mode_s\":" + std::to_string(who.mode_s);
    out += ",\"lat_deg\":" + json_number(s.lat_deg);
    out += ",\"lon_deg\":" + json_number(s.lon_deg);
    out += ",\"alt_ft_msl\":" + json_number(s.alt_ft_msl);
    out += ",\"heading_deg\":" + json_number(s.heading_deg);
    out += ",\"pitch_deg\":" + json_number(s.pitch_deg);
    out += ",\"roll_deg\":" + json_number(s.roll_deg);
    out += ",\"groundspeed_kts\":" + json_number(s.groundspeed_kts);
    out += ",\"vertical_speed_fpm\":" + json_number(s.vertical_speed_fpm);
    out += ",\"track_deg\":" + json_number(s.track_deg);
    out += std::string(",\"on_ground\":") + (s.on_ground ? "true" : "false");
    out += ",\"gear\":" + json_number(s.gear);
    out += ",\"flaps\":" + json_number(s.flaps);
    out += ",\"speedbrake\":" + json_number(s.speedbrake);
    out += ",\"thrust\":" + json_number(s.thrust);
    out += ",\"lights\":" + std::to_string(s.lights);
    if (frame.targets[i].render_delay_s) {
      out += ",\"render_delay_ms\":" +
             std::to_string(static_cast<long long>(
                 std::llround(*frame.targets[i].render_delay_s * 1000.0)));
    }
    // Free text last, as on every frame this console sends.
    out += ",\"icao_type\":\"" + json_escape(who.icao_type) + "\"";
    out += ",\"airline\":\"" + json_escape(who.airline) + "\"";
    out += ",\"livery\":\"" + json_escape(who.livery) + "\"";
    if (!who.destination.empty()) {
      out += ",\"destination\":\"" + json_escape(who.destination) + "\"";
    }
    out += ",\"callsign\":\"" + json_escape(who.callsign) + "\"}";
  }
  out += "],\"removed\":[";
  for (std::size_t i = 0; i < frame.removed.size(); ++i) {
    out += (i > 0 ? ",\"" : "\"") + json_escape(frame.removed[i]) + "\"";
  }
  out += "]}";
  return out;
}

} // namespace zoal_atc::traffic
