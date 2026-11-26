#include "traffic_layer.hpp"

#include "folder_picker.hpp"
#include "traffic_labels.hpp"
#include "traffic_renderer.hpp"

#include "zoal_atc/gui/traffic_label.hpp"
#include "zoal_atc/traffic/clock_sync.hpp"
#include "zoal_atc/traffic/csl_locator.hpp"
#include "zoal_atc/traffic/traffic_frame.hpp"
#include "zoal_atc/traffic/traffic_picture.hpp"
#include "zoal_atc/traffic/traffic_registry.hpp"
#include "zoal_atc/transport/bounded_queue.hpp"

#include "XPLMCamera.h"
#include "XPLMDataAccess.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"
#include "XPMPMultiplayer.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cmath>
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
// start() ran: the folders are known, so a CSL folder can still be chosen.
bool g_armed = false;
// XPluginEnable said fly, even if traffic was not running yet.
bool g_want_enabled = false;
bool g_xpmp_ready = false;

// Where the models are, and the pilot's choosing (the panel's Traffic tab).
fs::path g_plugin_root;
fs::path g_plugins_dir;
std::string g_csl_folder;
bool g_csl_configured = false;
std::vector<zoal_atc::traffic::FoundCsl> g_csl_found;
std::string g_csl_error;
std::string g_csl_notice;
Hooks g_hooks;
std::unique_ptr<FolderPicker> g_picker;
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
  XPLMDataRef latitude, longitude, elevation;
};
RenderRefs g_refs{};

// Why there is nothing to draw, for the panel; empty once started.
std::string g_unavailable = "Traffic has not started.";

// The panel's choices. Held here, not in the panel, because the renderer
// colours the tracked label and the camera callback chases the followed one.
std::string g_tracked;
std::string g_followed;
// Follow was pressed: the view is switched first and the camera taken on the
// next tick, so the view change does not take it straight back.
bool g_follow_pending = false;
zoal_atc::traffic::ChaseOffset g_chase;

constexpr double kFeetPerMetre = 3.28084;

// Our own tags drew (traffic_labels); false leaves XPMP2's on.
bool g_own_labels = false;

zoal_atc::traffic::OwnPosition own_position() {
  zoal_atc::traffic::OwnPosition own;
  if (g_refs.latitude && g_refs.longitude && g_refs.elevation) {
    own.lat_deg = XPLMGetDatad(g_refs.latitude);
    own.lon_deg = XPLMGetDatad(g_refs.longitude);
    own.alt_ft_msl = XPLMGetDatad(g_refs.elevation) * kFeetPerMetre;
    own.valid = true;
  }
  return own;
}

// Every aircraft XPMP2 is drawing, with what its tag says. The registry's
// state for the words, XPMP2's drawn position for the place, so the tag and
// the model move as one.
std::vector<traffic_labels::Target> label_targets() {
  std::vector<traffic_labels::Target> out;
  if (!g_registry || !g_renderer) {
    return out;
  }
  const auto anchors = g_renderer->anchors();
  if (anchors.empty()) {
    return out;
  }
  const auto picture = zoal_atc::traffic::nearby(
      *g_registry, zoal_atc::traffic::monotonic_seconds(), own_position());
  const gui::LabelRange range;
  for (const auto &anchor : anchors) {
    const auto found =
        std::find_if(picture.begin(), picture.end(),
                     [&](const zoal_atc::traffic::Nearby &n) { return n.identity.id == anchor.id; });
    if (found == picture.end()) {
      continue;
    }
    const bool tracked = anchor.id == g_tracked;
    if (!gui::label_shown(anchor.camera_distance_m, range, tracked)) {
      continue;
    }
    out.push_back(traffic_labels::Target{
        gui::traffic_label(*found, anchor.camera_distance_m, range, g_tracked, g_followed),
        anchor.x, anchor.y, anchor.z});
  }
  return out;
}

void log(const std::string &line) {
  XPLMDebugString(("zoal-atc: traffic: " + line + "\n").c_str());
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

void release_camera() {
  g_follow_pending = false;
  if (!g_followed.empty() && XPLMIsCameraBeingControlled(nullptr)) {
    XPLMDontControlCamera();
  }
  g_followed.clear();
}

// X-Plane asks where the camera is, every frame while we hold it. The state is
// the one XPMP2 draws, so the camera and the model move as one.
int chase_camera(XPLMCameraPosition_t *position, int losing_control, void *) {
  if (losing_control || position == nullptr || !g_registry || g_followed.empty()) {
    g_followed.clear();
    return 0;
  }
  const auto state = g_registry->state(g_followed, zoal_atc::traffic::monotonic_seconds());
  if (!state) {
    g_followed.clear();
    return 0;
  }
  double x = 0, y = 0, z = 0;
  XPLMWorldToLocal(state->lat_deg, state->lon_deg, state->alt_ft_msl / kFeetPerMetre, &x, &y,
                   &z);
  const auto pose = zoal_atc::traffic::chase_pose(x, y, z, state->heading_deg, g_chase);
  position->x = static_cast<float>(pose.x);
  position->y = static_cast<float>(pose.y);
  position->z = static_cast<float>(pose.z);
  position->heading = static_cast<float>(pose.heading_deg);
  position->pitch = static_cast<float>(pose.pitch_deg);
  position->roll = 0.0f;
  position->zoom = 1.0f;
  return 1;
}

} // namespace

namespace {

void find_refs() {
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
      find("sim/flightmodel/position/latitude"),
      find("sim/flightmodel/position/longitude"),
      find("sim/flightmodel/position/elevation"),
  };
}

// XPMP2 up, the models from `csl` loaded, and everything that draws them.
// False (with g_unavailable saying why) when XPMP2 will not start.
bool bring_up(const fs::path &csl) {
  const fs::path resources = resource_dir(g_plugin_root, csl);
  if (!g_xpmp_ready) {
    if (const char *err = XPMPMultiplayerInit("zoal-atc", resources.string().c_str(),
                                              xpmp_prefs, "A320", "ZATC");
        err && *err) {
      log(std::string("XPMP2 init failed: ") + err);
      g_unavailable = std::string("Traffic could not start: ") + err;
      return false;
    }
    g_xpmp_ready = true;
  }
  if (const char *err = XPMPLoadCSLPackage(csl.string().c_str()); err && *err) {
    log(std::string("CSL load reported: ") + err);
  }
  log("CSL models from " + csl.string() + ", " + std::to_string(XPMPGetNumberOfInstalledModels()) +
      " models; resources " + resources.string());

  g_registry = std::make_unique<zoal_atc::traffic::TrafficRegistry>();
  g_renderer = std::make_unique<TrafficRenderer>(*g_registry);
  g_renderer->set_tracked(g_tracked);
  find_refs();
  g_unavailable.clear();
  g_csl_folder = csl.string();
  g_own_labels = traffic_labels::start(label_targets);
  g_started = true;
  return true;
}

std::string home_dir() {
  for (const char *name : {"HOME", "USERPROFILE"}) {
    if (const char *value = std::getenv(name); value != nullptr && *value != '\0') {
      return value;
    }
  }
  return {};
}

} // namespace

bool start(const std::string &plugin_root, const std::string &system_path,
           const std::string &configured_csl, Hooks hooks) {
  g_plugin_root = fs::path(plugin_root);
  g_plugins_dir = fs::path(system_path) / "Resources" / "plugins";
  g_hooks = std::move(hooks);
  g_picker = std::make_unique<FolderPicker>();
  g_armed = true;

  const auto choice = zoal_atc::traffic::choose_csl(
      configured_csl, zoal_atc::traffic::default_csl_folders(g_plugin_root, g_plugins_dir));
  if (!choice.skipped.empty()) {
    log("csl_path " + choice.skipped + "; looking in the usual places");
  }
  if (choice.path.empty()) {
    // Offered in the panel, so the pilot picks rather than hunts.
    g_csl_found = zoal_atc::traffic::discover_csl_folders(g_plugins_dir);
    log("no CSL models found (looked in " + (g_plugin_root / "Resources" / "CSL").string() +
        " and other traffic plugins' folders); traffic is off until one is chosen in the "
        "panel's Traffic tab (" + std::to_string(g_csl_found.size()) + " found to offer)");
    g_unavailable = "No CSL models found, so traffic is off. Choose the folder that holds "
                    "your CSL models below.";
    return false;
  }
  g_csl_configured = choice.configured;
  return bring_up(choice.path);
}

void use_csl(const std::string &typed) {
  g_csl_error.clear();
  g_csl_notice.clear();
  if (!g_armed) {
    return;
  }
  const std::string text = zoal_atc::traffic::normalise_user_path(typed, home_dir());
  if (text.empty()) {
    g_csl_error = "Type or choose a folder first.";
    return;
  }
  const fs::path folder(text);
  if (!zoal_atc::traffic::holds_csl(folder)) {
    g_csl_error = "That folder has no CSL models: there is no xsb_aircraft.txt in it or below it.";
    return;
  }
  std::string why;
  if (g_hooks.save_csl && !g_hooks.save_csl(folder.string(), &why)) {
    // Used anyway: only remembering it failed.
    log("csl_path not saved: " + why);
    g_csl_notice = "Could not save it for next time: " + why;
  }
  if (g_started) {
    if (folder.string() == g_csl_folder) {
      g_csl_notice = "These models are already in use.";
      return;
    }
    // XPMP2 adds to what it has; the old folder's models go at the next start.
    if (const char *err = XPMPLoadCSLPackage(folder.string().c_str()); err && *err) {
      log(std::string("CSL load reported: ") + err);
    }
    log("CSL models added from " + folder.string() + ", now " +
        std::to_string(XPMPGetNumberOfInstalledModels()));
    g_csl_folder = folder.string();
    g_csl_configured = true;
    if (g_csl_notice.empty()) {
      g_csl_notice = "Loaded. The previous folder's models stay until X-Plane restarts.";
    }
    return;
  }
  if (!bring_up(folder)) {
    g_csl_error = g_unavailable;
    return;
  }
  g_csl_configured = true;
  if (g_want_enabled) {
    enable();
  }
  // The console sends traffic only to an aircraft that asked for it on the
  // hello, which this one did not: tell it now.
  if (g_hooks.started) {
    g_hooks.started();
  }
}

void browse_csl() {
  g_csl_error.clear();
  if (g_picker && !g_picker->open("Choose the folder that holds your CSL models")) {
    g_csl_error = "The folder dialog is already open.";
  }
}

void stop() {
  g_armed = false;
  g_picker.reset();
  if (!g_started) {
    if (g_xpmp_ready) {
      XPMPMultiplayerCleanup();
      g_xpmp_ready = false;
    }
    return;
  }
  disable();
  release_camera();
  traffic_labels::stop();
  g_own_labels = false;
  g_renderer.reset();
  g_registry.reset();
  XPMPMultiplayerCleanup();
  g_xpmp_ready = false;
  g_started = false;
}

void enable() {
  g_want_enabled = true;
  if (!g_started || g_enabled) {
    return;
  }
  // Taking the AI/TCAS aircraft is what replaces X-Plane's own AI traffic
  // with the console's: theirs are not drawn while we hold them.
  if (const char *err = XPMPMultiplayerEnable(); err && *err) {
    log(std::string("could not take the AI/TCAS aircraft: ") + err);
  }
  // Our tags replace XPMP2's when the overlay could be made.
  XPMPEnableAircraftLabels(!g_own_labels);
  traffic_labels::set_enabled(true);
  g_enabled = true;
}

void disable() {
  g_want_enabled = false;
  if (!g_enabled) {
    return;
  }
  release_camera();
  traffic_labels::set_enabled(false);
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
  // The folder dialog's answer, whether or not traffic is running yet.
  if (g_picker) {
    if (auto picked = g_picker->take()) {
      if (!picked->error.empty()) {
        g_csl_error = picked->error;
      } else if (!picked->path.empty()) {
        use_csl(picked->path);
      }
    }
  }
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
  traffic_labels::tick();
  if (g_follow_pending) {
    g_follow_pending = false;
    if (!g_followed.empty()) {
      XPLMControlCamera(xplm_ControlCameraUntilViewChanges, chase_camera, nullptr);
    }
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
}

namespace {

gui::CslStatus csl_status() {
  gui::CslStatus status;
  status.missing = g_armed && !g_started;
  status.folder = g_csl_folder;
  status.configured = g_csl_configured;
  status.models = g_started ? static_cast<std::size_t>(XPMPGetNumberOfInstalledModels()) : 0;
  for (const auto &found : g_csl_found) {
    status.found.push_back(gui::CslOffer{found.path.string(), found.label});
  }
  status.browsing = g_picker && g_picker->busy();
  status.error = g_csl_error;
  status.notice = g_csl_notice;
  return status;
}

} // namespace

void rescan_csl() {
  if (g_armed) {
    g_csl_found = zoal_atc::traffic::discover_csl_folders(g_plugins_dir);
  }
}

gui::TrafficPanel panel(gui::TrafficFilter filter) {
  if (!g_started || !g_registry) {
    gui::TrafficPanel off;
    off.unavailable = g_unavailable;
    off.csl = csl_status();
    return off;
  }
  const auto picture = zoal_atc::traffic::nearby(
      *g_registry, zoal_atc::traffic::monotonic_seconds(), own_position());
  // The camera lets go on its own (a view key, a track that left); the tab
  // shows that, not the last press.
  if (!g_followed.empty() && !g_follow_pending && !XPLMIsCameraBeingControlled(nullptr)) {
    g_followed.clear();
  }
  auto out = gui::traffic_panel(picture, filter, g_tracked, g_followed);
  out.chase_distance_m = g_chase.distance_m;
  out.csl = csl_status();
  return out;
}

void track(const std::string &id) {
  g_tracked = id;
  if (g_renderer) {
    g_renderer->set_tracked(id);
  }
}

void follow(const std::string &id) {
  if (id.empty() || !g_started) {
    release_camera();
    return;
  }
  const bool holding = !g_followed.empty() && XPLMIsCameraBeingControlled(nullptr);
  g_followed = id;
  if (holding) {
    return; // the callback reads g_followed: it simply chases the new one
  }
  g_chase.orbit_deg = 0.0;
  // An outside view first; the camera is taken on the next tick.
  if (XPLMCommandRef chase = XPLMFindCommand("sim/view/chase")) {
    XPLMCommandOnce(chase);
  }
  g_follow_pending = true;
}

void chase_zoom(double factor) {
  g_chase.distance_m =
      std::clamp(g_chase.distance_m * factor, zoal_atc::traffic::kChaseMinDistanceM,
                 zoal_atc::traffic::kChaseMaxDistanceM);
}

void chase_orbit(double degrees) {
  g_chase.orbit_deg = std::fmod(g_chase.orbit_deg + degrees + 360.0, 360.0);
}

} // namespace zoal_atc::xplane::traffic_layer
