#ifndef ZOAL_ATC_GUI_HUD_SESSION_HPP
#define ZOAL_ATC_GUI_HUD_SESSION_HPP

#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/hud_model.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace zoal_atc::gui {

// The HUD's colour theme, chosen in the panel and stored by the plugin.
enum class HudTheme { graphite, daylight, night };

// "daylight" / "night" / anything else (graphite, the default).
HudTheme hud_theme_from_name(const std::string &name);

// HudSession is the HUD's end of the GUI bridge: the same conversation the
// React HUD had with the plugin, held in C++.
//
// It is a view of GuiBridge (GuiView::hud), so it is owed the same messages a
// page was -- the bridge status, the console's events and the answers to its
// own requests -- and it asks for things the same way, with console.request
// envelopes through GuiBridge::submit. Nothing about the bridge changes.
//
// Main thread only, and clock-free: two times are passed in. `steady_ms` is the
// plugin's monotonic clock, which the bridge times requests with; `wall_ms` is
// Unix milliseconds, which transmissions are stamped and timed with.
class HudSession {
public:
  explicit HudSession(GuiBridge &bridge);

  // The HUD is up: attach to the bridge, which replays its cached snapshot.
  // The first pump after asks the plugin which theme to wear.
  void attach(std::uint64_t steady_ms);
  void detach();
  // Showing and hiding gate the bridge's high-volume stream, as for a page.
  void set_visible(bool visible, std::uint64_t steady_ms);

  // Drains what the bridge owes the HUD and lapses timers. Once a frame.
  void pump(std::uint64_t steady_ms, std::int64_t wall_ms);

  // The plugin's settings.changed post: the theme was changed in the panel.
  void on_settings_changed(const std::string &payload_json);

  // The menu's buttons. Each runs its action through the bridge when the menu
  // says it should.
  void toggle_menu() { menu_.toggle(); }
  void press_reload(std::uint64_t steady_ms, std::int64_t wall_ms);
  void press_new_flight(std::uint64_t steady_ms, std::int64_t wall_ms);
  void press_open_panel(std::uint64_t steady_ms);
  void dismiss_error() { menu_.dismiss_error(); }

  const HudFeed &feed() const { return feed_; }
  const HudMenu &menu() const { return menu_; }
  // The console is there: the socket is up and the subscription answered.
  bool reachable() const { return reachable_; }
  const std::string &callsign() const { return callsign_; }
  HudTheme theme() const { return theme_; }

private:
  enum class Request { action, settings };
  struct Pending {
    Request kind = Request::action;
    HudAction action = HudAction::none;
  };

  void handle(const std::string &envelope, std::int64_t wall_ms);
  void handle_status(const std::string &envelope);
  void handle_event(const std::string &envelope, std::int64_t wall_ms);
  void handle_response(const std::string &envelope, std::int64_t wall_ms);
  // Sends one request. Returns false, with the refusal in `error`, when the
  // bridge would not take it.
  bool send(const std::string &action, Pending pending, std::uint64_t steady_ms,
            std::string &error);
  void run(HudAction action, std::uint64_t steady_ms, std::int64_t wall_ms);

  GuiBridge &bridge_;
  bool attached_ = false;
  bool settings_asked_ = false;
  HudFeed feed_;
  HudMenu menu_;
  bool reachable_ = false;
  std::string callsign_;
  HudTheme theme_ = HudTheme::graphite;
  std::unordered_map<std::uint64_t, Pending> pending_;
};

// The console's comm_log payload (a CommLogEntry) as a HUD line; nullopt when
// it carries no text to show.
std::optional<HudEntry> parse_hud_entry(const std::string &payload_json);

} // namespace zoal_atc::gui

#endif
