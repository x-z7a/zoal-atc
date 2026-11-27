#include "test_support.hpp"
#include "zoal_atc/taxi/taxi_route.hpp"

#include <iostream>

using namespace zoal_atc::taxi;
using test::near;
using test::require;

namespace {

// Exactly what the console's guidance carrier sends (atc internal/guidance).
const std::string kLit =
    R"({"type":"taxi_route","rev":7,"station":"CYOW_GND","route_id":"tx-1","airport":"CYOW",)"
    R"("destination_runway":true,"points":[{"lat_deg":45.3225,"lon_deg":-75.6692},)"
    R"({"lat_deg":45.323,"lon_deg":-75.668,"hold_short":"14"}],"taxiways":["A","B"],)"
    R"("destination":"14","scenery":"Global Airports"})";
const std::string kDark =
    R"({"type":"taxi_route","rev":9,"station":"CYOW_GND","route_id":"","airport":"",)"
    R"("destination_runway":false,"points":[],"taxiways":[],"destination":"","scenery":""})";

TaxiRoute parsed(const std::string &text) {
  auto route = parse_taxi_route(text);
  require(route.has_value(), "parses: " + text);
  return *route;
}

void the_console_frame_parses() {
  auto route = parsed(kLit);
  require(route.rev == 7 && route.station == "CYOW_GND" && route.route_id == "tx-1", "envelope");
  require(route.airport == "CYOW" && route.destination_runway, "airport");
  require(route.points.size() == 2, "points");
  near(route.points[1].lon_deg, -75.668, 1e-9, "lon");
  require(route.points[0].hold_short.empty() && route.points[1].hold_short == "14", "stop bar");
  require(route.taxiways.size() == 2 && route.taxiways[1] == "B", "taxiways");
  require(route.destination == "14" && route.scenery == "Global Airports", "free text");
  require(!route.dark(), "lit");
  require(parsed(kDark).dark(), "no points is dark");
  require(!parse_taxi_route(R"({"type":"traffic","seq":1})").has_value(), "not ours");
}

void a_dark_frame_puts_out_only_its_own_stations_lights() {
  RouteBook book;
  require(book.apply(parsed(kLit)), "lit");
  require(!book.apply(parsed(kLit)), "the same route again changes nothing");
  auto other = parsed(kDark);
  other.station = "CYOW_GND_2";
  require(!book.apply(other), "another station's dark frame");
  require(book.current().has_value(), "still lit");
  auto stale = parsed(kDark);
  stale.rev = 3;
  require(!book.apply(stale), "an older dark frame from the same station");
  require(book.apply(parsed(kDark)), "its own station's");
  require(!book.current().has_value(), "out");
}

void a_newer_route_replaces_and_an_older_one_does_not() {
  RouteBook book;
  book.apply(parsed(kLit));
  auto older = parsed(kLit);
  older.rev = 2;
  older.route_id = "tx-0";
  require(!book.apply(older), "older from the same station");
  require(book.current()->route_id == "tx-1", "kept");
  auto elsewhere = parsed(kLit);
  elsewhere.station = "CYOW_GND_2";
  elsewhere.rev = 1;
  elsewhere.route_id = "tx-9";
  require(book.apply(elsewhere), "another station's route replaces whatever its rev");
  require(book.current()->route_id == "tx-9", "replaced");
}

} // namespace

int main() {
  the_console_frame_parses();
  a_dark_frame_puts_out_only_its_own_stations_lights();
  a_newer_route_replaces_and_an_older_one_does_not();
  std::cout << "taxi_route tests passed\n";
  return 0;
}
