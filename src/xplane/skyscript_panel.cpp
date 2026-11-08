#include "zoal_atc/gui/skyscript_panel.hpp"

#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/hud_layout.hpp"
#include "zoal_atc/gui/panel_paths.hpp"

#include <XPLMDisplay.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#include "skyscript_c.h"

namespace zoal_atc::gui {

namespace {

constexpr const char *kLogPrefix = "[zoal-atc gui] ";

void gui_log(const std::string &message) {
  XPLMDebugString((std::string(kLogPrefix) + message + "\n").c_str());
}

constexpr const char *kGuiCommandName = "zoal_atc/gui_toggle";
constexpr const char *kHudCommandName = "zoal_atc/hud_toggle";
constexpr const char *kRequestChannel = "console.request";
constexpr const char *kReadyChannel = "console.ready";
// The HUD page reports how tall it is drawing, so the window can be exactly
// that tall: see hud_layout.hpp for why that matters.
constexpr const char *kHudLayoutChannel = "hud.layout";
// The HUD's feed window reports the height it wants on its own channel: 1 when
// it has nothing to show, its full height while it has.
constexpr const char *kHudFeedLayoutChannel = "hud.feed_layout";
// The HUD asks for this after every click in it. X-Plane gives a clicked
// browser window the keyboard and keeps it there; the HUD has nothing to type
// into, so holding it only swallows the pilot's sim shortcuts.
constexpr const char *kHudFocusChannel = "hud.release_focus";
// The panel says whether one of its text fields has the keyboard.
constexpr const char *kPanelEditingChannel = "panel.editing";
// How long after a HUD update the keyboard is watched. X-Plane hands a browser
// window the keyboard when that window is updated -- resized, or just sent a
// new transmission -- not only when it is clicked, and the pilot's shortcuts go
// dead mid-approach. Long enough to cover the page's own repaint.
constexpr float kKeyboardGuardSeconds = 2.0F;
// A frame's worth of posts. The browser is a display, not a consumer the sim
// waits on: capping the drain keeps a backlog from stretching a flight frame.
constexpr std::size_t kMaxPostsPerFrame = 16;

// dup_c_string hands Skyscript a reply it will free. It must come from
// skyscript_strdup: on Windows the library has its own C runtime and heap, and
// a buffer from this plugin's malloc freed there corrupts it and crashes the sim.
char *dup_c_string(const std::string &value) {
  return skyscript_strdup(value.c_str());
}

std::string current_plugin_root() {
  char plugin_path[1024] = {};
  XPLMGetPluginInfo(XPLMGetMyID(), nullptr, plugin_path, nullptr, nullptr);
  return plugin_root_from_binary_path(plugin_path);
}

SkyScriptApp g_panel = nullptr;
SkyScriptApp g_hud = nullptr;
SkyScriptApp g_hud_feed = nullptr;
XPLMCommandRef g_gui_command = nullptr;
XPLMCommandRef g_hud_command = nullptr;
XPLMMenuID g_gui_menu = nullptr;
bool g_skyscript_initialized = false;
GuiBridge *g_bridge = nullptr;
std::function<std::uint64_t()> g_now_ms;
bool g_view_visible[kGuiViewCount] = {false, false, false};

// What the HUD was last placed for, so it is moved only when something changed
// rather than every frame.
int g_hud_content_height = 0;
int g_hud_feed_height = 1;
ScreenRect g_hud_screen;
bool g_hud_placed = false;

// The keyboard guard (guard_keyboard). Who had the keyboard last frame, until
// when the guard is up, and whether the pilot is typing in the panel.
bool g_sim_had_keyboard = true;
float g_keyboard_guard_until = 0.0F;
bool g_panel_editing = false;

std::uint64_t panel_now_ms() { return g_now_ms ? g_now_ms() : 0U; }

// The view travels as the handler's user data, so one pair of handlers serves
// both windows and each request is attributed to the page that sent it.
void *view_tag(GuiView view) {
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(view));
}

GuiView view_from(void *tag) {
  return static_cast<GuiView>(reinterpret_cast<std::uintptr_t>(tag));
}

SkyScriptApp app_for(GuiView view) {
  switch (view) {
  case GuiView::hud:
    return g_hud;
  case GuiView::hud_feed:
    return g_hud_feed;
  case GuiView::panel:
    break;
  }
  return g_panel;
}

// Both handlers run synchronously on the X-Plane main thread, inside the
// browser-function call that carries the page's request. Neither may wait on
// the console: the request returns a receipt, and the answer is posted later on
// console.event.
void console_request_handler(const char *, const char *payload,
                             char **out_response, char **out_error,
                             void *user_data) {
  if (g_bridge == nullptr) {
    *out_error = dup_c_string("gui bridge unavailable");
    gui_log("console.request failed: gui bridge unavailable");
    return;
  }
  const auto result = g_bridge->submit(payload != nullptr ? payload : "",
                                       panel_now_ms(), view_from(user_data));
  *out_response = dup_c_string(result.ack_json);
}

void console_ready_handler(const char *, const char *, char **out_response,
                           char **out_error, void *user_data) {
  if (g_bridge == nullptr) {
    *out_error = dup_c_string("gui bridge unavailable");
    gui_log("console.ready failed: gui bridge unavailable");
    return;
  }
  // Skyscript reports no page unload, so a reload simply says ready again:
  // attaching is what clears the previous page's backlog and re-subscribes.
  *out_response = dup_c_string(
      g_bridge->attach_panel(panel_now_ms(), view_from(user_data)));
}

ScreenRect screen_bounds() {
  ScreenRect screen;
  XPLMGetScreenBoundsGlobal(&screen.left, &screen.top, &screen.right,
                            &screen.bottom);
  return screen;
}

// arm_keyboard_guard starts (or extends) the watch on the keyboard; called on
// everything X-Plane may answer by giving a HUD window the keyboard.
void arm_keyboard_guard() {
  g_keyboard_guard_until = XPLMGetElapsedTime() + kKeyboardGuardSeconds;
}

// guard_keyboard runs once a frame. If the sim had the keyboard last frame and
// lost it while the guard was up, a HUD update took it, and it goes straight
// back -- unless the pilot has just clicked into a text field in the panel,
// which the panel reports.
//
// Without window IDs from Skyscript there is no asking which window holds it,
// so a click into another plugin's text box in the same two seconds would be
// undone too; that box only needs clicking again, where a stolen keyboard
// costs the pilot every shortcut until they notice.
void guard_keyboard() {
  const bool sim_has_keyboard = XPLMHasKeyboardFocus(nullptr) != 0;
  if (!sim_has_keyboard && g_sim_had_keyboard && !g_panel_editing &&
      XPLMGetElapsedTime() < g_keyboard_guard_until) {
    XPLMTakeKeyboardFocus(nullptr);
    g_sim_had_keyboard = XPLMHasKeyboardFocus(nullptr) != 0;
    return;
  }
  g_sim_had_keyboard = sim_has_keyboard;
}

void panel_editing_handler(const char *, const char *payload,
                           char **out_response, char **, void *) {
  const std::string body = payload != nullptr ? payload : "";
  g_panel_editing = body.find("\"editing\":true") != std::string::npos;
  *out_response = dup_c_string("{\"ok\":true}");
}

void place_hud() {
  if (!g_hud) {
    return;
  }
  arm_keyboard_guard();
  const ScreenRect screen = screen_bounds();
  const ScreenRect rect = hud_rect(screen, g_hud_content_height);
  skyscript_app_set_geometry(g_hud, rect.left, rect.top, rect.right,
                             rect.bottom);
  if (g_hud_feed) {
    const ScreenRect feed = hud_feed_rect(screen, rect, g_hud_feed_height);
    skyscript_app_set_geometry(g_hud_feed, feed.left, feed.top, feed.right,
                               feed.bottom);
  }
  g_hud_screen = screen;
  g_hud_placed = true;
}

void hud_layout_handler(const char *, const char *payload, char **out_response,
                        char **, void *) {
  const auto height = parse_hud_layout(payload != nullptr ? payload : "");
  if (height.has_value() && *height != g_hud_content_height) {
    g_hud_content_height = *height;
    place_hud();
  }
  *out_response = dup_c_string("{\"ok\":true}");
}

// Only the feed's window moves here. The page asks for a new height only while
// it is drawing nothing (hud.ts), so a resize never has visible content to
// stretch -- and the bar, in a window of its own, is not touched at all.
void hud_feed_layout_handler(const char *, const char *payload,
                             char **out_response, char **, void *) {
  const auto height = parse_hud_layout(payload != nullptr ? payload : "");
  if (height.has_value() && *height != g_hud_feed_height) {
    g_hud_feed_height = *height;
    place_hud();
  }
  *out_response = dup_c_string("{\"ok\":true}");
}

void hud_focus_handler(const char *, const char *, char **out_response,
                       char **, void *) {
  // Nullptr hands the keyboard back to X-Plane itself. Only the HUD posts
  // this, straight after a click in it, so the focus it takes is the HUD's own.
  XPLMTakeKeyboardFocus(nullptr);
  *out_response = dup_c_string("{\"ok\":true}");
}

void register_view(SkyScriptApp app, GuiView view) {
  skyscript_app_on_message(app, kRequestChannel, console_request_handler,
                           view_tag(view));
  skyscript_app_on_message(app, kReadyChannel, console_ready_handler,
                           view_tag(view));
}

void show_panel() {
  if (!g_panel) {
    return;
  }
  skyscript_app_show(g_panel, "");
}

void hide_panel() {
  if (!g_panel) {
    return;
  }
  skyscript_app_hide(g_panel);
}

int gui_command_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (phase != xplm_CommandBegin || !g_panel) {
    return 0;
  }
  if (skyscript_app_is_visible(g_panel)) {
    hide_panel();
  } else {
    show_panel();
  }
  return 1;
}

void show_hud() {
  if (!g_hud) {
    return;
  }
  skyscript_app_show(g_hud, "");
  if (g_hud_feed) {
    skyscript_app_show(g_hud_feed, "");
  }
  place_hud();
}

void hide_hud() {
  if (g_hud) {
    skyscript_app_hide(g_hud);
  }
  if (g_hud_feed) {
    skyscript_app_hide(g_hud_feed);
  }
}

int hud_command_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (phase != xplm_CommandBegin || !g_hud) {
    return 0;
  }
  if (skyscript_app_is_visible(g_hud)) {
    hide_hud();
  } else {
    show_hud();
  }
  return 1;
}

void install_gui_menu() {
  XPLMMenuID plugins_menu = XPLMFindPluginsMenu();
  if (!plugins_menu || !g_gui_command) {
    gui_log("could not install panel menu; plugin menu or command missing");
    return;
  }

  const int menu_index = XPLMAppendMenuItem(plugins_menu, "zoal-atc", nullptr, 1);
  if (menu_index < 0) {
    gui_log("could not append zoal-atc plugin menu");
    return;
  }

  g_gui_menu = XPLMCreateMenu("zoal-atc", plugins_menu, menu_index, nullptr,
                              nullptr);
  if (!g_gui_menu) {
    gui_log("could not create zoal-atc plugin menu");
    return;
  }

  if (XPLMAppendMenuItemWithCommand(g_gui_menu, "Toggle In-Sim Panel",
                                    g_gui_command) < 0) {
    gui_log("could not append panel toggle menu item");
  }
  if (g_hud_command &&
      XPLMAppendMenuItemWithCommand(g_gui_menu, "Toggle HUD", g_hud_command) <
          0) {
    gui_log("could not append HUD toggle menu item");
  }
}

// create_hud_feed makes the HUD's third window, for its transmissions: the
// same kind of chromeless window as the bar's, hung under it. Failing to make
// it costs the HUD its lines and nothing else.
void create_hud_feed(const std::string &home) {
  const std::string url = home + "&view=hud-feed";
  SkyScriptAppConfig feed_config = skyscript_default_config();
  feed_config.homepage = url.c_str();
  feed_config.hide_addressbar = 1;
  feed_config.audio_muted = 1;
  feed_config.console_logging = 0;
  feed_config.framerate = 15;
  feed_config.window_chromeless = 1;
  const HudPlacement placement;
  feed_config.width = static_cast<unsigned short>(placement.width);
  feed_config.height = 1;

  g_hud_feed = skyscript_create_app_window("zoal-atc HUD feed",
                                           "zoal-atc-hud-feed", &feed_config);
  if (!g_hud_feed) {
    gui_log("Skyscript did not create the HUD feed; the HUD bar still works");
    return;
  }
  if (g_bridge != nullptr) {
    register_view(g_hud_feed, GuiView::hud_feed);
  }
  skyscript_app_on_message(g_hud_feed, kHudFeedLayoutChannel,
                           hud_feed_layout_handler, nullptr);
  skyscript_app_on_message(g_hud_feed, kHudFocusChannel, hud_focus_handler,
                           nullptr);
}

// create_hud makes the second window: chromeless, transparent wherever the page
// paints nothing, pinned to the top right and as tall as its bar. Failing to
// make it costs the HUD and nothing else -- the panel is already up.
void create_hud(const std::string &home) {
  const std::string url = home + "&view=hud";
  SkyScriptAppConfig hud_config = skyscript_default_config();
  hud_config.homepage = url.c_str();
  hud_config.hide_addressbar = 1;
  hud_config.audio_muted = 1;
  hud_config.console_logging = 0;
  hud_config.framerate = 15;
  hud_config.window_chromeless = 1;
  const HudPlacement placement;
  hud_config.width = static_cast<unsigned short>(placement.width);
  hud_config.height = static_cast<unsigned short>(placement.min_height);

  g_hud = skyscript_create_app_window("zoal-atc HUD", "zoal-atc-hud",
                                      &hud_config);
  if (!g_hud) {
    gui_log("Skyscript did not create the zoal-atc HUD; the panel still works");
    return;
  }
  if (g_bridge != nullptr) {
    register_view(g_hud, GuiView::hud);
  }
  skyscript_app_on_message(g_hud, kHudLayoutChannel, hud_layout_handler,
                           nullptr);
  skyscript_app_on_message(g_hud, kHudFocusChannel, hud_focus_handler,
                           nullptr);
  create_hud_feed(home);
  g_hud_command =
      XPLMCreateCommand(kHudCommandName, "zoal-atc: Toggle the radio HUD");
  XPLMRegisterCommandHandler(g_hud_command, hud_command_handler, 1, nullptr);
  // Up from the start: the HUD is how ATC is heard with the panel closed,
  // which is most of a flight.
  show_hud();
}

} // namespace

void start_skyscript_panel(const SkyscriptPanelConfig &config) {
  if (g_panel) {
    return;
  }

  const std::string plugin_root = current_plugin_root();
  const std::string assets = assets_dir(plugin_root);
  const std::string app_index = shell_index_path(plugin_root);
  if (!std::filesystem::exists(app_index)) {
    gui_log("panel app not found at " + app_index +
            "; build/package atc/gui/zoal-atc first");
  }

  skyscript_set_log_prefix("[zoal-atc skyscript]");
  skyscript_initialize();
  g_skyscript_initialized = true;
  skyscript_set_plugin_path(plugin_root.c_str());
  skyscript_set_assets_path(assets.c_str());

  const std::string home = shell_home_url(plugin_root, config.plugin_version);
  SkyScriptAppConfig panel_config = skyscript_default_config();
  panel_config.homepage = home.c_str();
  panel_config.hide_addressbar = 1;
  panel_config.audio_muted = 1;
  panel_config.console_logging = 0;
  panel_config.framerate = 15;
  panel_config.minimum_width = 760;
  // Wide enough for the six readouts on one row at the panel's type size
  // (six 170px tiles, their gaps and the padding come to about 1075px), so the
  // first thing a pilot sees is not a lone tile wrapped onto a second line.
  panel_config.width = 1200;
  panel_config.height = 800;
  // Window chrome, chosen rather than inherited. Skyscript v0.9.0 made
  // self-decorated windows the default and v0.9.1 put the field on the C ABI so
  // a consumer can say which it wants; stating it here means the panel's
  // appearance does not move when a later Skyscript changes its mind.
  panel_config.window_titleless = 1;
  panel_config.window_opacity = 0.96f;

  g_panel =
      skyscript_create_app_window("zoal-atc", "zoal-atc", &panel_config);
  if (!g_panel) {
    gui_log("Skyscript did not create the zoal-atc panel");
    skyscript_shutdown();
    g_skyscript_initialized = false;
    return;
  }

  g_bridge = config.bridge;
  g_now_ms = config.now_ms;
  g_view_visible[static_cast<std::size_t>(GuiView::panel)] =
      skyscript_app_is_visible(g_panel) != 0;
  if (g_bridge != nullptr) {
    register_view(g_panel, GuiView::panel);
    skyscript_app_on_message(g_panel, kPanelEditingChannel,
                             panel_editing_handler, nullptr);
  } else {
    gui_log("no GUI bridge configured; Skyscript handlers were not registered");
  }

  create_hud(home);

  g_gui_command =
      XPLMCreateCommand(kGuiCommandName, "zoal-atc: Toggle in-sim panel");
  XPLMRegisterCommandHandler(g_gui_command, gui_command_handler, 1, nullptr);
  install_gui_menu();

  gui_log("Skyscript panel ready; use Plugins > zoal-atc > Toggle In-Sim Panel "
          "or bind command zoal_atc/gui_toggle");
}

void stop_skyscript_panel() {
  if (g_bridge != nullptr) {
    g_bridge->detach_panel(GuiView::panel);
    g_bridge->detach_panel(GuiView::hud);
    g_bridge->detach_panel(GuiView::hud_feed);
    g_bridge = nullptr;
  }
  g_now_ms = nullptr;
  for (bool &visible : g_view_visible) {
    visible = false;
  }
  g_hud_placed = false;
  g_hud_content_height = 0;
  g_hud_feed_height = 1;
  if (g_gui_menu) {
    XPLMDestroyMenu(g_gui_menu);
    g_gui_menu = nullptr;
  }
  if (g_gui_command) {
    XPLMUnregisterCommandHandler(g_gui_command, gui_command_handler, 1,
                                 nullptr);
    g_gui_command = nullptr;
  }
  if (g_hud_command) {
    XPLMUnregisterCommandHandler(g_hud_command, hud_command_handler, 1,
                                 nullptr);
    g_hud_command = nullptr;
  }
  if (g_hud_feed) {
    skyscript_destroy_app_window(g_hud_feed);
    g_hud_feed = nullptr;
  }
  if (g_hud) {
    skyscript_destroy_app_window(g_hud);
    g_hud = nullptr;
  }
  if (g_panel) {
    skyscript_destroy_app_window(g_panel);
    g_panel = nullptr;
  }
  if (g_skyscript_initialized) {
    skyscript_shutdown();
    g_skyscript_initialized = false;
  }
}

void tick_skyscript_panel() {
  if (g_bridge == nullptr) {
    return;
  }

  for (const GuiView view : {GuiView::panel, GuiView::hud, GuiView::hud_feed}) {
    SkyScriptApp app = app_for(view);
    if (app == nullptr) {
      continue;
    }
    const auto index = static_cast<std::size_t>(view);
    const bool visible = skyscript_app_is_visible(app) != 0;
    if (visible != g_view_visible[index]) {
      g_view_visible[index] = visible;
      g_bridge->set_panel_visible(visible, panel_now_ms(), view);
      // A hidden panel types nothing, and its page may never see the blur.
      if (view == GuiView::panel && !visible) {
        g_panel_editing = false;
      }
    }
    for (const auto &message : g_bridge->take_js_messages(kMaxPostsPerFrame, view)) {
      skyscript_app_post_message(app, message.channel.c_str(),
                                 message.payload.c_str());
      if (view != GuiView::panel && visible) {
        arm_keyboard_guard();
      }
    }
  }

  // The screen changes size when X-Plane's window does; the HUD follows its
  // corner rather than staying where the old corner was.
  if (g_hud && g_hud_placed &&
      skyscript_app_is_visible(g_hud) != 0) {
    const ScreenRect screen = screen_bounds();
    if (screen.left != g_hud_screen.left || screen.top != g_hud_screen.top ||
        screen.right != g_hud_screen.right ||
        screen.bottom != g_hud_screen.bottom) {
      place_hud();
    }
  }

  guard_keyboard();
}

void show_skyscript_panel() { show_panel(); }

void post_to_skyscript_windows(const char *channel,
                               const std::string &payload) {
  for (SkyScriptApp app : {g_panel, g_hud, g_hud_feed}) {
    if (app) {
      skyscript_app_post_message(app, channel, payload.c_str());
    }
  }
}

} // namespace zoal_atc::gui
