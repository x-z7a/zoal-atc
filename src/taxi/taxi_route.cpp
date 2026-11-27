#include "zoal_atc/taxi/taxi_route.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <cmath>

namespace zoal_atc::taxi {

namespace json = zoal_atc::transport;

namespace {

std::optional<RoutePoint> parse_point(const std::string &element) {
  auto lat = json::json_number_field(element, "lat_deg");
  auto lon = json::json_number_field(element, "lon_deg");
  if (!lat || !lon || !std::isfinite(*lat) || !std::isfinite(*lon) ||
      std::fabs(*lat) > 90.0 || std::fabs(*lon) > 180.0 ||
      (*lat == 0.0 && *lon == 0.0)) {
    return std::nullopt;
  }
  RoutePoint point;
  point.lat_deg = *lat;
  point.lon_deg = *lon;
  point.hold_short = json::json_string_field(element, "hold_short").value_or(std::string());
  return point;
}

bool same_route(const TaxiRoute &a, const TaxiRoute &b) {
  if (a.station != b.station || a.route_id != b.route_id ||
      a.points.size() != b.points.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.points.size(); ++i) {
    if (a.points[i].lat_deg != b.points[i].lat_deg ||
        a.points[i].lon_deg != b.points[i].lon_deg ||
        a.points[i].hold_short != b.points[i].hold_short) {
      return false;
    }
  }
  return true;
}

} // namespace

std::optional<TaxiRoute> parse_taxi_route(const std::string &json_text) {
  // Envelope keys lead the document and no point carries one of them, so the
  // flat key search finds the envelope's; a point's keys are read per element.
  auto type = json::json_string_field(json_text, "type");
  if (!type || *type != kTaxiRouteFrameType) {
    return std::nullopt;
  }
  TaxiRoute route;
  route.rev = json::json_uint_field(json_text, "rev").value_or(0);
  route.station = json::json_string_field(json_text, "station").value_or(std::string());
  route.route_id = json::json_string_field(json_text, "route_id").value_or(std::string());
  route.airport = json::json_string_field(json_text, "airport").value_or(std::string());
  route.destination_runway =
      json::json_bool_field(json_text, "destination_runway").value_or(false);
  if (auto raw = json::json_raw_field(json_text, "points")) {
    if (auto elements = json::json_array_elements(*raw)) {
      for (const auto &element : *elements) {
        if (auto point = parse_point(element)) {
          route.points.push_back(std::move(*point));
        }
      }
    }
  }
  if (route.points.size() < 2) {
    route.points.clear();
  }
  route.taxiways = json::json_string_array_field(json_text, "taxiways");
  route.destination = json::json_string_field(json_text, "destination").value_or(std::string());
  route.scenery = json::json_string_field(json_text, "scenery").value_or(std::string());
  return route;
}

bool RouteBook::apply(const TaxiRoute &frame) {
  const bool same_station = current_ && current_->station == frame.station;
  if (same_station && frame.rev < current_->rev) {
    return false;
  }
  if (frame.dark()) {
    if (!same_station) {
      return false;
    }
    current_.reset();
    return true;
  }
  const bool changed = !current_ || !same_route(*current_, frame);
  current_ = frame;
  return changed;
}

} // namespace zoal_atc::taxi
