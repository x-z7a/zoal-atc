#include "traffic_layer.hpp"

#include "traffic_renderer.hpp"

#include "zoal_atc/traffic/clock_sync.hpp"
#include "zoal_atc/traffic/traffic_frame.hpp"
#include "zoal_atc/traffic/traffic_registry.hpp"
#include "zoal_atc/transport/bounded_queue.hpp"

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "XPMPMultiplayer.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace zoal_atc::xplane::traffic_layer {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxQueuedFrames = 64;
constexpr double kExpireIntervalS = 1.0;

struct Received {
  zoal_atc::traffic::TrafficFrame frame;
  double at = 0;
};

bool g_started = false;
bool g_enabled = false;
std::unique_ptr<zoal_atc::traffic::TrafficRegistry> g_registry;
std::unique_ptr<TrafficRenderer> g_renderer;
double g_last_expire = 0;

std::mutex g_inbox_mutex;
std::deque<Received> g_inbox;
std::atomic<bool> g_disconnected{false};

struct RenderRefs {
  XPLMDataRef hpath, theta, phi, gear, flaps, speedbrake, throttle;
  XPLMDataRef nav, beacon, strobe, taxi, landing, livery;
};
RenderRefs g_refs{};

void log(const std::string &line) {
  XPLMDebugString(("zoal-atc: traffic: " + line + "\n").c_str());
}

bool has_csl(const fs::path &dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return false;
  }
  for (const auto &entry : fs::recursive_directory_iterator(dir, ec)) {
    if (entry.path().filename() == "xsb_aircraft.txt") {
      return true;
    }
  }
  return false;
}

// CSL models: our own folder first, then the packages other traffic plugins
// already installed, so a pilot with IVAO, LiveTraffic or xPilot models does
// not download them twice.
std::vector<fs::path> csl_candidates(const fs::path &root, const fs::path &plugins) {
  return {
      root / "Resources" / "CSL",
      plugins / "IVAO_CSL" / "CSL",
      plugins / "LiveTraffic" / "Resources" / "CSL",
      plugins / "xPilot" / "Resources" / "CSL",
      plugins / "X-Ivap Resources" / "CSL",
  };
}

// XPMP2 resources (Doc8643.txt, related.txt, Obj8DataRefs.txt, MapIcons.png):
// ours, or those beside the CSL package we found.
fs::path resource_dir(const fs::path &root, const fs::path &csl) {
  std::error_code ec;
  const fs::path ours = root / "Resources" / "xpmp2";
  if (fs::is_regular_file(ours / "related.txt", ec)) {
    return ours;
  }
  if (fs::is_regular_file(csl.parent_path() / "related.txt", ec)) {
    return csl.parent_path();
  }
  return ours;
}

int xpmp_prefs(const char *, const char *key, int fallback) {
  if (std::strcmp(key, XPMP_CFG_ITM_CLAMPALL) == 0) {
    return 0; // per aircraft: on the ground only
  }
  if (std::strcmp(key, XPMP_CFG_ITM_HANDLE_DUP_ID) == 0) {
    return 1;
  }
  return fallback;
}

XPLMDataRef find(const char *name) { return XPLMFindDataRef(name); }
float getf(XPLMDataRef ref) { return ref ? XPLMGetDataf(ref) : 0.0f; }
int geti(XPLMDataRef ref) { return ref ? XPLMGetDatai(ref) : 0; }

} // namespace

bool start(const std::string &plugin_root, const std::string &system_path) {
  const fs::path root(plugin_root);
  const fs::path plugins = fs::path(system_path) / "Resources" / "plugins";
  fs::path csl;
  for (const auto &candidate : csl_candidates(root, plugins)) {
    if (has_csl(candidate)) {
      csl = candidate;
      break;
    }
  }
  if (csl.empty()) {
    log("no CSL models found (looked in " + (root / "Resources" / "CSL").string() +
        " and other traffic plugins' folders); traffic is off");
    return false;
  }
  const fs::path resources = resource_dir(root, csl);
  if (const char *err = XPMPMultiplayerInit("zoal-atc", resources.string().c_str(), xpmp_prefs,
                                            "A320", "ZATC");
      err && *err) {
    log(std::string("XPMP2 init failed: ") + err);
    return false;
  }
  if (const char *err = XPMPLoadCSLPackage(csl.string().c_str()); err && *err) {
    log(std::string("CSL load reported: ") + err);
  }
  log("CSL models from " + csl.string() + ", " + std::to_string(XPMPGetNumberOfInstalledModels()) +
      " models; resources " + resources.string());

  g_registry = std::make_unique<zoal_atc::traffic::TrafficRegistry>();
  g_renderer = std::make_unique<TrafficRenderer>(*g_registry);
  g_refs = RenderRefs{
      find("sim/flightmodel/position/hpath"),
      find("sim/flightmodel/position/theta"),
      find("sim/flightmodel/position/phi"),
      find("sim/flightmodel2/gear/deploy_ratio"),
      find("sim/flightmodel2/controls/flap1_deploy_ratio"),
      find("sim/flightmodel2/controls/speedbrake_ratio"),
      find("sim/cockpit2/engine/actuators/throttle_ratio_all"),
      find("sim/cockpit/electrical/nav_lights_on"),
      find("sim/cockpit/electrical/beacon_lights_on"),
      find("sim/cockpit/electrical/strobe_lights_on"),
      find("sim/cockpit/electrical/taxi_light_on"),
      find("sim/cockpit/electrical/landing_lights_on"),
      find("sim/aircraft/view/acf_livery_path"),
  };
  g_started = true;
  return true;
}

void stop() {
  if (!g_started) {
    return;
  }
  disable();
  g_renderer.reset();
  g_registry.reset();
  XPMPMultiplayerCleanup();
  g_started = false;
}

void enable() {
  if (!g_started || g_enabled) {
    return;
  }
  // Taking the AI/TCAS aircraft is what replaces X-Plane's own AI traffic
  // with the console's: theirs are not drawn while we hold them.
  if (const char *err = XPMPMultiplayerEnable(); err && *err) {
    log(std::string("could not take the AI/TCAS aircraft: ") + err);
  }
  XPMPEnableAircraftLabels(true);
  g_enabled = true;
}

void disable() {
  if (!g_enabled) {
    return;
  }
  if (g_renderer) {
    g_renderer->remove_all();
  }
  XPMPMultiplayerDisable();
  g_enabled = false;
}

bool offer_frame(const std::string &payload) {
  // Cheap reject before parsing: every frame on the socket passes here.
  if (payload.find("\"type\":\"traffic\"") == std::string::npos) {
    return false;
  }
  auto frame = zoal_atc::traffic::parse_traffic_frame(payload);
  if (!frame) {
    return false;
  }
  if (!g_started) {
    return true; // ours, but nothing to draw with
  }
  const double now = zoal_atc::traffic::monotonic_seconds();
  std::lock_guard<std::mutex> lock(g_inbox_mutex);
  zoal_atc::transport::push_bounded(g_inbox, Received{std::move(*frame), now}, kMaxQueuedFrames);
  return true;
}

void disconnected() { g_disconnected.store(true); }

void tick() {
  if (!g_started || !g_registry || !g_renderer) {
    return;
  }
  if (g_disconnected.exchange(false)) {
    // A new socket is a new session: its clock and its track ids may both
    // have changed.
    g_registry->clear();
    g_renderer->remove_all();
  }
  std::deque<Received> inbox;
  {
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    inbox.swap(g_inbox);
  }
  for (const auto &received : inbox) {
    g_renderer->apply(g_registry->apply(received.frame, received.at));
  }
  const double now = zoal_atc::traffic::monotonic_seconds();
  if (now - g_last_expire >= kExpireIntervalS) {
    g_last_expire = now;
    g_renderer->apply(g_registry->expire(now));
  }
}

void decorate(telemetry::TelemetrySnapshot &s) {
  if (!g_started) {
    return;
  }
  s.has_render = true;
  s.track_deg = getf(g_refs.hpath);
  s.pitch_deg = getf(g_refs.theta);
  s.roll_deg = getf(g_refs.phi);
  float gear = 0.0f;
  if (g_refs.gear && XPLMGetDatavf(g_refs.gear, &gear, 0, 1) == 1) {
    s.gear = gear;
  }
  s.flaps = getf(g_refs.flaps);
  s.speedbrake = getf(g_refs.speedbrake);
  s.thrust = getf(g_refs.throttle);
  s.lights = (geti(g_refs.nav) ? 1u : 0u) | (geti(g_refs.beacon) ? 2u : 0u) |
             (geti(g_refs.strobe) ? 4u : 0u) | (geti(g_refs.taxi) ? 8u : 0u) |
             (geti(g_refs.landing) ? 16u : 0u);
  if (g_refs.livery) {
    char path[512] = {};
    XPLMGetDatab(g_refs.livery, path, 0, sizeof(path) - 1);
    std::string livery(path);
    while (!livery.empty() && (livery.back() == '/' || livery.back() == '\\')) {
      livery.pop_back();
    }
    const auto slash = livery.find_last_of("/\\");
    s.livery = slash == std::string::npos ? livery : livery.substr(slash + 1);
  }

  // Echo suppression: what we inject shows up on our own TCAS. Reporting it
  // would tell the console its own AI aircraft (and other pilots) are
  // unknown non-participants.
  if (g_renderer && g_renderer->size() > 0) {
    const auto injected = g_renderer->mode_s_ids();
    auto &targets = s.traffic;
    const auto before = targets.size();
    targets.erase(std::remove_if(targets.begin(), targets.end(),
                                 [&](const telemetry::TrafficTarget &t) {
                                   return t.id != 0 &&
                                          injected.count(static_cast<std::uint32_t>(t.id)) > 0;
                                 }),
                  targets.end());
    s.traffic_census.seen -= static_cast<int>(before - targets.size());
  }
}

} // namespace zoal_atc::xplane::traffic_layer
