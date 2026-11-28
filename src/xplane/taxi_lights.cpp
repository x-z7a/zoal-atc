#include "taxi_lights.hpp"

#include "zoal_atc/taxi/lead_on_lights.hpp"
#include "zoal_atc/taxi/taxi_route.hpp"

#include "XPLMDataAccess.h"
#include "XPLMGraphics.h"
#include "XPLMInstance.h"
#include "XPLMScenery.h"
#include "XPLMUtilities.h"

#include <chrono>
#include <deque>
#include <map>
#include <mutex>

namespace zoal_atc::xplane::taxi_lights {

namespace {

// How far ahead of the aircraft the route is lit, as real lead-on lights are
// switched in segments ahead of it.
constexpr double kLitAheadM = 400.0;
// Lights are placed again now and then, because the sim's local origin moves
// as the aircraft does and a placed instance does not move with it.
constexpr double kReplaceS = 5.0;
constexpr double kTickS = 0.25;

struct Queued {
  taxi::TaxiRoute route;
  bool allowed = true;
};

std::mutex g_inbox_mutex;
std::deque<Queued> g_inbox;

bool g_started = false;
taxi::LightsMode g_mode = taxi::LightsMode::Always;
std::string g_system_path;
// Reader thread only: the scenery this sim draws each airport from.
std::map<std::string, std::string> g_own_scenery;

XPLMObjectRef g_green = nullptr;
XPLMObjectRef g_red = nullptr;
XPLMProbeRef g_probe = nullptr;
XPLMDataRef g_lat = nullptr;
XPLMDataRef g_lon = nullptr;
XPLMDataRef g_airport_lights = nullptr;

taxi::RouteBook g_book;
taxi::LightPlan g_plan;
double g_along = 0.0;
double g_last_tick = -1.0;
double g_last_placed = -1.0;
std::map<std::size_t, XPLMInstanceRef> g_lit;
XPLMInstanceRef g_stop_bar = nullptr;

void log(const std::string &line) {
  XPLMDebugString(("zoal-atc: taxi lights: " + line + "\n").c_str());
}

double now_s() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The one dataref the light objects read, fed per instance: how much of the
// lens's own glow shows. X-Plane's taxi_g and taxi_r are airport lights, off
// by day, so by day the lens is the light and at night the sprite is
// (x-z7a/atc#1185).
const char *kInstanceDatarefs[] = {"zoal_atc/taxi_lights/daylight", nullptr};

float daylight() {
  if (g_airport_lights == nullptr) {
    return 1.0f;
  }
  const float night = XPLMGetDataf(g_airport_lights);
  return night <= 0.0f ? 1.0f : (night >= 1.0f ? 0.0f : 1.0f - night);
}

void place(XPLMInstanceRef instance, const taxi::Light &light) {
  double x = 0.0, y = 0.0, z = 0.0;
  XPLMWorldToLocal(light.lat_deg, light.lon_deg, 0.0, &x, &y, &z);
  XPLMProbeInfo_t info{};
  info.structSize = sizeof(info);
  if (g_probe != nullptr &&
      XPLMProbeTerrainXYZ(g_probe, static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(z), &info) == xplm_ProbeHitTerrain) {
    x = info.locationX;
    y = info.locationY;
    z = info.locationZ;
  }
  XPLMDrawInfo_t where{};
  where.structSize = sizeof(where);
  where.x = static_cast<float>(x);
  where.y = static_cast<float>(y);
  where.z = static_cast<float>(z);
  where.heading = static_cast<float>(light.heading_deg);
  const float data[] = {daylight()};
  XPLMInstanceSetPosition(instance, &where, data);
}

XPLMInstanceRef make(XPLMObjectRef object) {
  return XPLMCreateInstance(object, kInstanceDatarefs);
}

void put_out() {
  for (auto &[index, instance] : g_lit) {
    XPLMDestroyInstance(instance);
  }
  g_lit.clear();
  if (g_stop_bar != nullptr) {
    XPLMDestroyInstance(g_stop_bar);
    g_stop_bar = nullptr;
  }
}

void light(double now) {
  const auto ahead = taxi::lights_ahead(g_plan, g_along, kLitAheadM);
  const bool replace = g_last_placed < 0.0 || now - g_last_placed >= kReplaceS;
  std::map<std::size_t, XPLMInstanceRef> lit;
  for (const auto index : ahead) {
    auto found = g_lit.find(index);
    XPLMInstanceRef instance = nullptr;
    if (found != g_lit.end()) {
      instance = found->second;
      g_lit.erase(found);
      if (replace) {
        place(instance, g_plan.lights[index]);
      }
    } else {
      instance = make(g_green);
      if (instance == nullptr) {
        continue;
      }
      place(instance, g_plan.lights[index]);
    }
    lit.emplace(index, instance);
  }
  // Behind the aircraft now, or beyond the lit stretch.
  for (auto &[index, instance] : g_lit) {
    XPLMDestroyInstance(instance);
  }
  g_lit.swap(lit);

  const bool bar = taxi::stop_bar_ahead(g_plan, g_along, kLitAheadM);
  if (bar && g_stop_bar == nullptr) {
    g_stop_bar = make(g_red);
    if (g_stop_bar != nullptr) {
      place(g_stop_bar, *g_plan.stop_bar);
    }
  } else if (bar && replace) {
    place(g_stop_bar, *g_plan.stop_bar);
  } else if (!bar && g_stop_bar != nullptr) {
    XPLMDestroyInstance(g_stop_bar);
    g_stop_bar = nullptr;
  }
  if (replace) {
    g_last_placed = now;
  }
}

} // namespace

bool start(const std::string &plugin_root, const std::string &system_path,
           taxi::LightsMode mode) {
  if (mode == taxi::LightsMode::Off) {
    log("switched off (taxi_lights = off)");
    return false;
  }
  const std::string objects = plugin_root + "/objects/";
  g_green = XPLMLoadObject((objects + "taxi_lead_on_green.obj").c_str());
  g_red = XPLMLoadObject((objects + "taxi_stop_bar.obj").c_str());
  if (g_green == nullptr || g_red == nullptr) {
    log("the light objects are missing from " + objects + "; taxi routes are not lit");
    stop();
    return false;
  }
  g_probe = XPLMCreateProbe(xplm_ProbeY);
  g_lat = XPLMFindDataRef("sim/flightmodel/position/latitude");
  g_lon = XPLMFindDataRef("sim/flightmodel/position/longitude");
  g_airport_lights = XPLMFindDataRef("sim/graphics/scenery/airport_light_level");
  g_mode = mode;
  g_system_path = system_path;
  g_started = true;
  log(std::string("on (taxi_lights = ") + taxi::lights_mode_name(mode) + ")");
  return true;
}

void stop() {
  g_started = false;
  put_out();
  g_book.clear();
  g_plan = {};
  if (g_probe != nullptr) {
    XPLMDestroyProbe(g_probe);
    g_probe = nullptr;
  }
  if (g_green != nullptr) {
    XPLMUnloadObject(g_green);
    g_green = nullptr;
  }
  if (g_red != nullptr) {
    XPLMUnloadObject(g_red);
    g_red = nullptr;
  }
}

bool offer_frame(const std::string &payload) {
  if (payload.find("\"type\":\"taxi_route\"") == std::string::npos) {
    return false;
  }
  auto route = taxi::parse_taxi_route(payload);
  if (!route) {
    return false;
  }
  if (!g_started) {
    return true;
  }
  Queued queued{std::move(*route), true};
  if (g_mode == taxi::LightsMode::MatchingScenery && !queued.route.dark()) {
    // Read here rather than on the main thread: it reads scenery files.
    auto cached = g_own_scenery.find(queued.route.airport);
    if (cached == g_own_scenery.end()) {
      cached = g_own_scenery
                   .emplace(queued.route.airport,
                            taxi::scenery_for_airport(g_system_path, queued.route.airport))
                   .first;
    }
    queued.allowed = taxi::should_light(g_mode, queued.route.scenery, cached->second);
    if (!queued.allowed) {
      log("route at " + queued.route.airport + " drawn from " + queued.route.scenery +
          ", this sim shows " + cached->second + "; not lit (taxi_lights = matching)");
    }
  }
  std::lock_guard<std::mutex> lock(g_inbox_mutex);
  g_inbox.push_back(std::move(queued));
  while (g_inbox.size() > 16) {
    g_inbox.pop_front();
  }
  return true;
}

void tick() {
  if (!g_started) {
    return;
  }
  std::deque<Queued> inbox;
  {
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    inbox.swap(g_inbox);
  }
  for (auto &queued : inbox) {
    if (!queued.allowed) {
      // Another scenery's pavement: whatever this station lit goes out, and
      // nothing new is lit.
      queued.route.points.clear();
    }
    if (g_book.apply(queued.route)) {
      put_out();
      g_along = 0.0;
      g_last_placed = -1.0;
      g_plan = g_book.current() ? taxi::plan_lights(g_book.current()->points) : taxi::LightPlan{};
      if (g_book.current()) {
        log("lit " + g_book.current()->route_id + " at " + g_book.current()->airport + ", " +
            std::to_string(g_plan.lights.size()) + " lights over " +
            std::to_string(static_cast<int>(g_plan.length_m)) + " m");
      }
    }
  }
  if (g_plan.path.empty()) {
    return;
  }
  const double now = now_s();
  if (now - g_last_tick < kTickS) {
    return;
  }
  g_last_tick = now;
  auto along = taxi::along_route(g_plan, XPLMGetDatad(g_lat), XPLMGetDatad(g_lon), g_along);
  if (!along) {
    // Off the route: nothing ahead of the aircraft to follow.
    put_out();
    return;
  }
  g_along = *along;
  light(now);
}

} // namespace zoal_atc::xplane::taxi_lights
