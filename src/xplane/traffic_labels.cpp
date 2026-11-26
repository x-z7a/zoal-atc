#include "traffic_labels.hpp"

#include "hud_window.hpp"
#include "imgui_window.hpp"
#include "panel_window.hpp"
#include "zoal_atc/gui/panel_paths.hpp"
#include "zoal_atc/traffic/screen_projection.hpp"
#include "../ui/kit.hpp"
#include "../ui/traffic_labels_view.hpp"

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

#include "imgui.h"

namespace zoal_atc::xplane::traffic_labels {

namespace {

// How far past the screen's edge a tag's point may be and still be drawn: a
// tag is wider than its point, so it slides in rather than popping.
constexpr float kMarginBoxels = 120.0f;

struct Labels {
  std::unique_ptr<ImguiWindow> window;
  std::function<std::vector<Target>()> targets;
  ui::Typeface face;
  bool enabled = false;
  bool shown = true; // the pilot's toggle
  XPLMCommandRef command = nullptr;
  XPLMDataRef world = nullptr;
  XPLMDataRef projection = nullptr;
  XPLMDataRef modern = nullptr;
  XPLMDataRef ui_scale = nullptr;
};

std::unique_ptr<Labels> g_labels;

void log(const std::string &line) {
  XPLMDebugString(("zoal-atc: traffic labels: " + line + "\n").c_str());
}

// The matrices are this frame's: X-Plane sets them for the 3-D view before
// windows draw, which is when XPMP2 read them for its own labels.
void build() {
  if (!g_labels || !g_labels->targets) {
    return;
  }
  float world[16] = {};
  float projection[16] = {};
  if (!g_labels->world || !g_labels->projection ||
      XPLMGetDatavf(g_labels->world, world, 0, 16) != 16 ||
      XPLMGetDatavf(g_labels->projection, projection, 0, 16) != 16) {
    return;
  }
  // Vulkan and Metal keep depth in [0, 1]; OpenGL in [-1, 1].
  const bool zero_to_one = !g_labels->modern || XPLMGetDatai(g_labels->modern) != 0;
  const ImVec2 display = ImGui::GetIO().DisplaySize;

  std::vector<ui::PlacedLabel> placed;
  for (Target &target : g_labels->targets()) {
    const auto at = traffic::project_to_screen(world, projection, target.x, target.y,
                                               target.z, zero_to_one);
    if (!at) {
      continue;
    }
    const ImVec2 anchor(static_cast<float>(at->x) * display.x,
                        static_cast<float>(1.0 - at->y) * display.y);
    if (anchor.x < -kMarginBoxels || anchor.x > display.x + kMarginBoxels ||
        anchor.y < -kMarginBoxels || anchor.y > display.y + kMarginBoxels) {
      continue;
    }
    placed.push_back(ui::PlacedLabel{std::move(target.label), anchor, at->depth});
  }
  ui::draw_traffic_labels(std::move(placed), hud_theme(), g_labels->face);
}

void apply_visibility() {
  if (g_labels && g_labels->window) {
    g_labels->window->set_visible(g_labels->enabled && g_labels->shown);
  }
}

int toggle_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (phase == xplm_CommandBegin && g_labels) {
    g_labels->shown = !g_labels->shown;
    apply_visibility();
  }
  return 1;
}

std::filesystem::path plugin_fonts_dir() {
  char binary[1024] = {};
  XPLMGetPluginInfo(XPLMGetMyID(), nullptr, binary, nullptr, nullptr);
  return std::filesystem::path(gui::plugin_root_from_binary_path(binary)) / "fonts";
}

void fit_to_screen() {
  int left = 0, top = 0, right = 0, bottom = 0;
  XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
  int l = 0, t = 0, r = 0, b = 0;
  g_labels->window->geometry(&l, &t, &r, &b);
  if (l != left || t != top || r != right || b != bottom) {
    g_labels->window->set_geometry(left, top, right, bottom);
  }
}

} // namespace

bool start(std::function<std::vector<Target>()> targets) {
  if (g_labels) {
    return true;
  }
  g_labels = std::make_unique<Labels>();
  g_labels->targets = std::move(targets);
  ImguiWindow::Options options;
  options.overlay = true;
  g_labels->window = std::make_unique<ImguiWindow>(0, 100, 100, 0, build, options);
  if (!g_labels->window->valid()) {
    log("X-Plane did not create the overlay (it needs X-Plane 12.4.4 or newer); "
        "XPMP2's labels are used instead");
    g_labels.reset();
    return false;
  }
  {
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_labels->window->context());
    char system_path[1024] = {};
    XPLMGetSystemPath(system_path);
    g_labels->face = ui::load_typeface(
        plugin_fonts_dir(), std::filesystem::path(system_path) / "Resources" / "fonts", log);
    ImGui::SetCurrentContext(previous);
  }
  g_labels->world = XPLMFindDataRef("sim/graphics/view/world_matrix");
  g_labels->projection = XPLMFindDataRef("sim/graphics/view/projection_matrix_3d");
  g_labels->modern = XPLMFindDataRef("sim/graphics/view/using_modern_driver");
  g_labels->ui_scale = XPLMFindDataRef("sim/graphics/misc/user_interface_scale");
  fit_to_screen();
  g_labels->command = XPLMCreateCommand(kCommandName, "zoal-atc: Show or hide the traffic tags");
  XPLMRegisterCommandHandler(g_labels->command, toggle_handler, 1, nullptr);
  add_to_menu("Toggle Traffic Tags", kCommandName);
  return true;
}

void stop() {
  if (!g_labels) {
    return;
  }
  if (g_labels->command) {
    XPLMUnregisterCommandHandler(g_labels->command, toggle_handler, 1, nullptr);
  }
  g_labels->window.reset();
  g_labels.reset();
}

void set_enabled(bool enabled) {
  if (g_labels) {
    g_labels->enabled = enabled;
    apply_visibility();
  }
}

void tick() {
  if (!g_labels || !g_labels->window->visible()) {
    return;
  }
  fit_to_screen();
  // Sharp on a Retina screen and a scaled UI alike, as the HUD does.
  float scale = 1.0f;
  if (g_labels->ui_scale) {
    scale = std::max(1.0f, XPLMGetDataf(g_labels->ui_scale));
  }
  g_labels->window->set_framebuffer_scale(2.0f * scale);
}

} // namespace zoal_atc::xplane::traffic_labels
