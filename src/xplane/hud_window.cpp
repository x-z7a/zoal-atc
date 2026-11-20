#include "hud_window.hpp"

#include "imgui_window.hpp"
#include "zoal_atc/gui/hud_layout.hpp"
#include "zoal_atc/gui/hud_session.hpp"
#include "zoal_atc/gui/panel_paths.hpp"
#include "../ui/hud_view.hpp"
#include "../ui/kit.hpp"

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

namespace zoal_atc::xplane {

namespace {

using gui::HudAction;
using gui::HudLine;
using gui::HudSession;
using gui::HudTheme;
using gui::HudTone;

// --- state -------------------------------------------------------------------

struct Hud {
  std::unique_ptr<ImguiWindow> window;
  std::unique_ptr<HudSession> session;
  std::function<std::uint64_t()> steady_ms;
  // Inter, or Roboto without it; the panel is set in the same.
  ui::Typeface face;
  XPLMCommandRef command = nullptr;
  XPLMDataRef ui_scale = nullptr;
  // What the drawing remembers between frames and the presses it recorded.
  ui::HudViewState view;
  HudCockpit cockpit;
};

std::unique_ptr<Hud> g_hud;


void hud_log(const std::string &message) {
  XPLMDebugString(("[zoal-atc hud] " + message + "\n").c_str());
}

std::uint64_t steady_now() { return g_hud->steady_ms ? g_hud->steady_ms() : 0; }

std::int64_t wall_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// --- window ------------------------------------------------------------------

void build() {
  ui::draw_hud(*g_hud->session, g_hud->view, g_hud->face,
               ui::HudFrame{wall_now()});
}

void place() {
  gui::ScreenRect screen;
  XPLMGetScreenBoundsGlobal(&screen.left, &screen.top, &screen.right,
                            &screen.bottom);
  gui::HudPlacement placement;
  placement.width = static_cast<int>(std::ceil(
      std::clamp(g_hud->view.content_width, 1.0f, static_cast<float>(placement.width))));
  const gui::ScreenRect rect = gui::hud_rect(
      screen, static_cast<int>(std::ceil(g_hud->view.content_height)), placement);
  g_hud->window->set_geometry(rect.left, rect.top, rect.right, rect.bottom);
}

void set_shown(bool shown) {
  if (shown) {
    place();
  }
  g_hud->window->set_visible(shown);
  g_hud->session->set_visible(shown, steady_now());
}

int toggle_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (phase == xplm_CommandBegin && g_hud) {
    set_shown(!g_hud->window->visible());
  }
  return 1;
}

std::filesystem::path plugin_fonts_dir() {
  char binary[1024] = {};
  XPLMGetPluginInfo(XPLMGetMyID(), nullptr, binary, nullptr, nullptr);
  return std::filesystem::path(gui::plugin_root_from_binary_path(binary)) /
         "fonts";
}

// Inter, shipped in the plugin's fonts/ folder; without it, X-Plane's own UI
// face, Roboto, which every install has.
void load_fonts() {
  char system_path[1024] = {};
  XPLMGetSystemPath(system_path);
  g_hud->face = ui::load_typeface(
      plugin_fonts_dir(),
      std::filesystem::path(system_path) / "Resources" / "fonts", hud_log);
}

} // namespace

void start_hud(gui::GuiBridge &bridge,
               std::function<std::uint64_t()> steady_ms, HudCockpit cockpit) {
  if (g_hud) {
    return;
  }
  g_hud = std::make_unique<Hud>();
  g_hud->steady_ms = std::move(steady_ms);
  g_hud->cockpit = std::move(cockpit);
  g_hud->window = std::make_unique<ImguiWindow>(0, 40, 440, 0, build);
  if (!g_hud->window->valid()) {
    hud_log("X-Plane did not create the HUD window (it needs X-Plane 12.4.4 "
            "or newer); the panel still works");
    g_hud.reset();
    return;
  }
  {
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_hud->window->context());
    load_fonts();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ItemSpacing = ImVec2(0.0f, 0.0f);
    ImGui::SetCurrentContext(previous);
  }
  g_hud->ui_scale = XPLMFindDataRef("sim/graphics/misc/user_interface_scale");
  g_hud->session = std::make_unique<HudSession>(bridge);
  if (g_hud->cockpit.speak_own_call) {
    g_hud->session->set_own_call_speaker(g_hud->cockpit.speak_own_call);
  }
  g_hud->session->attach(steady_now());

  g_hud->command =
      XPLMCreateCommand(kHudCommandName, "zoal-atc: Toggle the radio HUD");
  XPLMRegisterCommandHandler(g_hud->command, toggle_handler, 1, nullptr);
  // Up from the start: the HUD is how ATC is heard with the panel closed,
  // which is most of a flight.
  set_shown(true);
  hud_log("up; toggle with Plugins > zoal-atc > Toggle HUD or " +
          std::string(kHudCommandName));
}

void stop_hud() {
  if (!g_hud) {
    return;
  }
  if (g_hud->command) {
    XPLMUnregisterCommandHandler(g_hud->command, toggle_handler, 1, nullptr);
  }
  g_hud->session->detach();
  g_hud->window.reset();
  g_hud.reset();
}

void tick_hud() {
  if (!g_hud) {
    return;
  }
  const std::uint64_t steady = steady_now();
  const std::int64_t wall = wall_now();
  HudSession &session = *g_hud->session;
  switch (std::exchange(g_hud->view.pressed, HudAction::none)) {
  case HudAction::reload_plan:
    session.press_reload(steady, wall);
    break;
  case HudAction::end_flight:
    session.press_new_flight(steady, wall);
    break;
  case HudAction::open_panel:
    session.press_open_panel(steady);
    break;
  case HudAction::none:
    break;
  }
  if (g_hud->cockpit.radio) {
    const auto [com, active_hz] = g_hud->cockpit.radio();
    session.set_radio(com, active_hz, steady);
  }
  if (const std::string key = std::exchange(g_hud->view.pressed_call, std::string()); !key.empty()) {
    session.press_call(key, steady, wall);
  }
  if (std::exchange(g_hud->view.pressed_next, false)) {
    session.press_next_frequency(steady);
  }
  if (std::exchange(g_hud->view.pressed_dismiss_call, false)) {
    session.dismiss_call_error();
  }
  session.pump(steady, wall);
  // Text is rasterized for the screen's real density: X-Plane reports its UI
  // scale but not the display's (a Retina screen draws two pixels a boxel and
  // says 1:1), so twice the UI scale keeps it sharp on both kinds of screen.
  float scale = 1.0f;
  if (g_hud->ui_scale) {
    scale = std::max(1.0f, XPLMGetDataf(g_hud->ui_scale));
  }
  g_hud->window->set_framebuffer_scale(2.0f * scale);
  if (g_hud->window->visible()) {
    place();
  }
}

void hud_settings_changed(const std::string &payload_json) {
  if (g_hud) {
    g_hud->session->on_settings_changed(payload_json);
  }
}

void hud_reply_heard(std::uint64_t frequency_hz) {
  if (g_hud) {
    g_hud->session->reply_heard(frequency_hz, steady_now(), wall_now());
  }
}

} // namespace zoal_atc::xplane
