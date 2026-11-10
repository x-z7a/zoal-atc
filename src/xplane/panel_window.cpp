#include "panel_window.hpp"

#include "hud_window.hpp"
#include "imgui_window.hpp"
#include "zoal_atc/gui/panel_paths.hpp"
#include "zoal_atc/gui/panel_session.hpp"
#include "../ui/kit.hpp"
#include "../ui/panel_view.hpp"

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include "imgui.h"

namespace zoal_atc::xplane {

namespace {

// The size it opens at: six readouts across and room for a conversation.
constexpr int kOpenWidth = 1120;
constexpr int kOpenHeight = 740;
// Below this the composer and the readouts stop fitting.
constexpr int kMinWidth = 760;
constexpr int kMinHeight = 480;

struct Panel {
  std::unique_ptr<ImguiWindow> window;
  std::unique_ptr<gui::PanelSession> session;
  std::function<std::uint64_t()> steady_ms;
  std::string version;
  ui::Typeface face;
  ui::PanelViewState state;
  // Presses from the last frame, run from the flight loop.
  std::vector<std::function<void()>> later;
  ui::PanelViewResult last;
  // Where the drag started: the mouse and the window, in global boxels.
  bool dragging = false;
  int drag_mouse_x = 0;
  int drag_mouse_y = 0;
  int drag_left = 0;
  int drag_top = 0;
  bool placed = false;
  XPLMCommandRef command = nullptr;
  XPLMMenuID menu = nullptr;
  XPLMDataRef ui_scale = nullptr;
};

std::unique_ptr<Panel> g_panel;

void panel_log(const std::string &message) {
  XPLMDebugString(("[zoal-atc panel] " + message + "\n").c_str());
}

std::uint64_t steady_now() {
  return g_panel && g_panel->steady_ms ? g_panel->steady_ms() : 0;
}

std::int64_t wall_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void build() {
  ui::PanelFrame frame{g_panel->version, steady_now(), wall_now(),
                       &g_panel->later};
  g_panel->last = ui::draw_panel(*g_panel->session, g_panel->state,
                                 g_panel->face, frame);
}

// Centred on the main screen the first time it opens; where the pilot left it
// after that.
void place_initially() {
  int left = 0, top = 0, right = 0, bottom = 0;
  XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
  const int screen_w = right - left;
  const int screen_h = top - bottom;
  const int width = std::max(kMinWidth, std::min(kOpenWidth, screen_w - 80));
  const int height = std::max(kMinHeight, std::min(kOpenHeight, screen_h - 120));
  const int x = left + (screen_w - width) / 2;
  const int y = top - (screen_h - height) / 2;
  g_panel->window->set_geometry(x, y, x + width, y - height);
  g_panel->placed = true;
}

void set_shown(bool shown) {
  if (shown && !g_panel->placed) {
    place_initially();
  }
  g_panel->window->set_visible(shown);
  if (shown) {
    g_panel->window->bring_to_front();
  }
}

int toggle_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (phase == xplm_CommandBegin && g_panel) {
    set_shown(!g_panel->window->visible());
  }
  return 1;
}

void install_menu() {
  XPLMMenuID plugins = XPLMFindPluginsMenu();
  if (!plugins) {
    panel_log("no plugins menu; the panel is reachable by command only");
    return;
  }
  const int index = XPLMAppendMenuItem(plugins, "zoal-atc", nullptr, 1);
  if (index < 0) {
    panel_log("could not append the zoal-atc menu");
    return;
  }
  g_panel->menu = XPLMCreateMenu("zoal-atc", plugins, index, nullptr, nullptr);
  if (!g_panel->menu) {
    panel_log("could not create the zoal-atc menu");
    return;
  }
  XPLMAppendMenuItemWithCommand(g_panel->menu, "Toggle Panel", g_panel->command);
  if (XPLMCommandRef hud = XPLMFindCommand(kHudCommandName)) {
    XPLMAppendMenuItemWithCommand(g_panel->menu, "Toggle HUD", hud);
  }
}

std::filesystem::path plugin_dir() {
  char binary[1024] = {};
  XPLMGetPluginInfo(XPLMGetMyID(), nullptr, binary, nullptr, nullptr);
  return std::filesystem::path(gui::plugin_root_from_binary_path(binary));
}

std::filesystem::path xplane_fonts_dir() {
  char system_path[1024] = {};
  XPLMGetSystemPath(system_path);
  return std::filesystem::path(system_path) / "Resources" / "fonts";
}

} // namespace

void start_panel(const PanelConfig &config) {
  if (g_panel || config.bridge == nullptr) {
    return;
  }
  g_panel = std::make_unique<Panel>();
  g_panel->steady_ms = config.steady_ms;
  g_panel->version = config.plugin_version;

  ImguiWindow::Options options;
  options.keyboard = true;
  options.resizable = true;
  options.min_width = kMinWidth;
  options.min_height = kMinHeight;
  g_panel->window = std::make_unique<ImguiWindow>(
      0, kOpenHeight, kOpenWidth, 0, build, options);
  if (!g_panel->window->valid()) {
    panel_log("X-Plane did not create the panel window (it needs X-Plane "
              "12.4.4 or newer)");
    g_panel.reset();
    return;
  }
  {
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_panel->window->context());
    g_panel->face = ui::load_typeface(plugin_dir() / "fonts", xplane_fonts_dir(),
                                      panel_log);
    ui::setup_panel_style();
    ImGui::SetCurrentContext(previous);
  }
  g_panel->ui_scale = XPLMFindDataRef("sim/graphics/misc/user_interface_scale");

  g_panel->session = std::make_unique<gui::PanelSession>(*config.bridge);
  g_panel->session->attach(steady_now());

  g_panel->command =
      XPLMCreateCommand(kPanelCommandName, "zoal-atc: Toggle the radio panel");
  XPLMRegisterCommandHandler(g_panel->command, toggle_handler, 1, nullptr);
  install_menu();
  panel_log("ready; open it with Plugins > zoal-atc > Toggle Panel or bind " +
            std::string(kPanelCommandName));
}

void stop_panel() {
  if (!g_panel) {
    return;
  }
  if (g_panel->menu) {
    XPLMDestroyMenu(g_panel->menu);
  }
  if (g_panel->command) {
    XPLMUnregisterCommandHandler(g_panel->command, toggle_handler, 1, nullptr);
  }
  g_panel->later.clear();
  g_panel->session->detach();
  g_panel->window.reset();
  g_panel.reset();
}

void tick_panel() {
  if (!g_panel) {
    return;
  }
  // What was pressed, outside the draw callback that saw it.
  std::vector<std::function<void()>> later;
  later.swap(g_panel->later);
  for (const auto &action : later) {
    action();
  }

  const ui::PanelViewResult last = std::exchange(g_panel->last, {});
  if (last.close) {
    set_shown(false);
  }
  // Anywhere off the controls is the window's grip; it follows the mouse in
  // global boxels,
  // since the window moving under it would make window coordinates useless.
  if (g_panel->window->visible() && (last.drag_started || last.dragging)) {
    int mx = 0, my = 0;
    XPLMGetMouseLocationGlobal(&mx, &my);
    int left = 0, top = 0, right = 0, bottom = 0;
    g_panel->window->geometry(&left, &top, &right, &bottom);
    if (last.drag_started || !g_panel->dragging) {
      g_panel->dragging = true;
      g_panel->drag_mouse_x = mx;
      g_panel->drag_mouse_y = my;
      g_panel->drag_left = left;
      g_panel->drag_top = top;
    } else {
      const int x = g_panel->drag_left + (mx - g_panel->drag_mouse_x);
      const int y = g_panel->drag_top + (my - g_panel->drag_mouse_y);
      g_panel->window->set_geometry(x, y, x + (right - left), y - (top - bottom));
    }
  } else {
    g_panel->dragging = false;
  }

  g_panel->session->pump(steady_now(), wall_now());
  // Text is rasterized for the screen's real density: X-Plane reports its UI
  // scale but not the display's, so twice the UI scale keeps it sharp on a
  // Retina screen and a scaled one alike (as the HUD does).
  float scale = 1.0f;
  if (g_panel->ui_scale) {
    scale = std::max(1.0f, XPLMGetDataf(g_panel->ui_scale));
  }
  g_panel->window->set_framebuffer_scale(2.0f * scale);
}

void show_panel() {
  if (g_panel) {
    set_shown(true);
  }
}

void panel_settings_changed(const std::string &payload_json) {
  if (g_panel) {
    g_panel->session->on_settings_changed(payload_json);
  }
}

} // namespace zoal_atc::xplane
