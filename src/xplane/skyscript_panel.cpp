#include "zoal_atc/gui/skyscript_panel.hpp"

#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/notification.hpp"
#include "zoal_atc/gui/panel_paths.hpp"

#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <cstdio>
#include <cstddef>
#include <cstdlib>
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
constexpr const char *kRequestChannel = "console.request";
constexpr const char *kReadyChannel = "console.ready";
// A frame's worth of posts. The browser is a display, not a consumer the sim
// waits on: capping the drain keeps a backlog from stretching a flight frame.
constexpr std::size_t kMaxPostsPerFrame = 16;

// Only the newest transmission is worth a toast. Showing a backlog one at a
// time would replace each with the next anyway, and a pilot returning from ten
// minutes away wants to know ATC called, not to watch ten toasts.
constexpr std::size_t kMaxNotificationsPerFrame = 8;

NotificationController g_notifications;

SkyScriptNotificationCorner corner_from(const std::string &corner) {
  if (corner == "top_left") {
    return SKYSCRIPT_NOTIFICATION_TOP_LEFT;
  }
  if (corner == "bottom_left") {
    return SKYSCRIPT_NOTIFICATION_BOTTOM_LEFT;
  }
  if (corner == "bottom_right") {
    return SKYSCRIPT_NOTIFICATION_BOTTOM_RIGHT;
  }
  return SKYSCRIPT_NOTIFICATION_TOP_RIGHT;
}

// dup_c_string hands Skyscript a buffer it will release with free(), which is
// what its C ABI documents. new[] here would be a heap mismatch.
char *dup_c_string(const std::string &value) {
  auto *buffer = static_cast<char *>(std::malloc(value.size() + 1U));
  if (buffer == nullptr) {
    return nullptr;
  }
  std::memcpy(buffer, value.c_str(), value.size() + 1U);
  return buffer;
}

std::string current_plugin_root() {
  char plugin_path[1024] = {};
  XPLMGetPluginInfo(XPLMGetMyID(), nullptr, plugin_path, nullptr, nullptr);
  return plugin_root_from_binary_path(plugin_path);
}

SkyScriptApp g_panel = nullptr;
XPLMCommandRef g_gui_command = nullptr;
XPLMMenuID g_gui_menu = nullptr;
bool g_skyscript_initialized = false;
GuiBridge *g_bridge = nullptr;
std::function<std::uint64_t()> g_now_ms;
bool g_panel_visible = false;

std::uint64_t panel_now_ms() { return g_now_ms ? g_now_ms() : 0U; }

// raise_due_notifications turns the events nobody saw into at most one toast.
//
// Showing a new notification replaces the current one, so a backlog would only
// ever leave the last of them on screen. Draining to the newest and raising it
// once says the same thing without flickering through the rest, and the panel
// still holds the whole log for when it is opened.
void raise_due_notifications(bool panel_visible) {
  const auto events = g_bridge->take_notification_events(kMaxNotificationsPerFrame);
  if (events.empty()) {
    return;
  }

  const auto prefs = g_notifications.preferences();

  // Walk the whole batch so the queue drains, but keep only the last one that
  // would have raised: Skyscript replaces the current toast with each new one,
  // so raising them in turn would flicker through to the same result.
  NotificationRequest newest;
  for (const auto &event : events) {
    const auto request = notification_for(event, prefs, panel_visible);
    if (request.raise) {
      newest = request;
    }
  }
  if (!newest.raise) {
    return;
  }

  SkyScriptNotificationOptions options = skyscript_default_notification_options();
  options.title = newest.title.c_str();
  options.body = newest.body.c_str();
  options.corner = corner_from(prefs.corner);
  options.timeout_seconds = static_cast<float>(prefs.timeout_secs);
  options.play_sound = prefs.sound ? 1 : 0;
  // Over a hidden browser Skyscript forces this off anyway, because nothing on
  // such a toast can be clicked. Saying so here keeps the two ends agreeing.
  options.dismissible = 0;

  skyscript_app_show_notification(g_panel, &options);
}

// Both handlers run synchronously on the X-Plane main thread, inside
// Skyscript's own flight loop. Neither may wait on the console: the request
// returns a receipt, and the answer is posted later on console.event.
void console_request_handler(const char *, const char *payload,
                             char **out_response, char **out_error,
                             void *) {
  if (g_bridge == nullptr) {
    *out_error = dup_c_string("gui bridge unavailable");
    gui_log("console.request failed: gui bridge unavailable");
    return;
  }
  const auto result =
      g_bridge->submit(payload != nullptr ? payload : "", panel_now_ms());
  *out_response = dup_c_string(result.ack_json);
}

void console_ready_handler(const char *, const char *, char **out_response,
                           char **out_error, void *) {
  if (g_bridge == nullptr) {
    *out_error = dup_c_string("gui bridge unavailable");
    gui_log("console.ready failed: gui bridge unavailable");
    return;
  }
  // Skyscript reports no page unload, so a reload simply says ready again:
  // attaching is what clears the previous page's backlog and re-subscribes.
  *out_response = dup_c_string(g_bridge->attach_panel(panel_now_ms()));
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
  panel_config.width = 920;
  panel_config.height = 620;
  // Window chrome, chosen rather than inherited. Skyscript v0.9.0 made
  // self-decorated windows the default and v0.9.1 put the field on the C ABI so
  // a consumer can say which it wants; stating it here means the panel's
  // appearance does not move when a later Skyscript changes its mind.
  //
  // It is also a requirement rather than a preference: a notification can only
  // bring its own window when the app draws its own chrome, because decoration
  // is fixed at window creation and a titled window would put an empty X-Plane
  // frame on screen around the toast. Notifications with the panel closed are
  // the whole point of the bump.
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
  g_panel_visible = skyscript_app_is_visible(g_panel) != 0;
  if (g_bridge != nullptr) {
    skyscript_app_on_message(g_panel, kRequestChannel, console_request_handler,
                             nullptr);
    skyscript_app_on_message(g_panel, kReadyChannel, console_ready_handler,
                             nullptr);
  } else {
    gui_log("no GUI bridge configured; Skyscript handlers were not registered");
  }

  g_gui_command =
      XPLMCreateCommand(kGuiCommandName, "zoal-atc: Toggle in-sim panel");
  XPLMRegisterCommandHandler(g_gui_command, gui_command_handler, 1, nullptr);
  install_gui_menu();

  gui_log("Skyscript panel ready; use Plugins > zoal-atc > Toggle In-Sim Panel "
          "or bind command zoal_atc/gui_toggle");
}

void stop_skyscript_panel() {
  if (g_bridge != nullptr) {
    g_bridge->detach_panel();
    g_bridge = nullptr;
  }
  g_now_ms = nullptr;
  g_panel_visible = false;
  if (g_gui_menu) {
    XPLMDestroyMenu(g_gui_menu);
    g_gui_menu = nullptr;
  }
  if (g_gui_command) {
    XPLMUnregisterCommandHandler(g_gui_command, gui_command_handler, 1,
                                 nullptr);
    g_gui_command = nullptr;
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
  if (g_panel == nullptr || g_bridge == nullptr) {
    return;
  }

  const bool visible = skyscript_app_is_visible(g_panel) != 0;
  if (visible != g_panel_visible) {
    g_panel_visible = visible;
    g_bridge->set_panel_visible(visible, panel_now_ms());
  }

  const auto messages = g_bridge->take_js_messages(kMaxPostsPerFrame);
  for (const auto &message : messages) {
    skyscript_app_post_message(g_panel, message.channel.c_str(),
                               message.payload.c_str());
  }

  raise_due_notifications(visible);
}

bool apply_notification_control(const std::string &frame) {
  const auto control = parse_notification_control(frame);
  if (!control.valid) {
    return false;
  }
  g_notifications.apply(control);
  return true;
}

void clear_notification_control() { g_notifications.reset(); }

} // namespace zoal_atc::gui
