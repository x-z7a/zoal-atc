#ifndef ZOAL_ATC_GUI_HUD_SESSION_HPP
#define ZOAL_ATC_GUI_HUD_SESSION_HPP

#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/hud_model.hpp"

#include <cstdint>
#include <functional>
#include <set>
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

  // --- calls by button (atc docs/architecture/pilot-menu.md) ---------------

  // The radio the pilot talks on: which COM is selected and what it is tuned
  // to, in Hz. Called every frame; a change asks the position now on the
  // channel for its menu.
  void set_radio(int com, std::uint64_t active_hz, std::uint64_t steady_ms);
  // The menu the position on the tuned channel last sent, or null.
  const HudCallMenu *calls() const { return calls_.on(active_hz_); }
  std::uint64_t active_hz() const { return active_hz_; }
  // Presses the item with this key on the tuned channel's menu: the call goes
  // to the console, and is spoken in the pilot's own voice once it is taken.
  void press_call(const std::string &key, std::uint64_t steady_ms,
                  std::int64_t wall_ms);
  // Tunes the selected COM to the channel the position sent the pilot to.
  void press_next_frequency(std::uint64_t steady_ms);
  // A controller's transmission on this channel finished playing in the
  // cockpit. With auto readback on, a readback the menu offers is pressed.
  void reply_heard(std::uint64_t frequency_hz, std::uint64_t steady_ms,
                   std::int64_t wall_ms);
  bool auto_readback() const { return auto_readback_; }
  // What the last press could not do; empty when it went.
  const std::string &call_error() const { return call_error_; }
  void dismiss_call_error() { call_error_.clear(); }
  // A key pressed and not yet answered, so a button is not sent twice.
  bool call_in_flight(const std::string &key) const {
    return in_flight_.count(key) > 0;
  }
  // Speaks a call the console took, in the pilot's voice, on this channel.
  void set_own_call_speaker(
      std::function<void(const std::string &, std::uint64_t)> speak) {
    speak_own_ = std::move(speak);
  }

  const HudFeed &feed() const { return feed_; }
  const HudMenu &menu() const { return menu_; }
  // The console is there: the socket is up and the subscription answered.
  bool reachable() const { return reachable_; }
  const std::string &callsign() const { return callsign_; }
  HudTheme theme() const { return theme_; }

private:
  enum class Request { action, settings, menu, call, tune };
  struct Pending {
    Request kind = Request::action;
    HudAction action = HudAction::none;
    std::string key;
    std::string sentence;
    std::uint64_t frequency_hz = 0;
  };

  void handle(const std::string &envelope, std::int64_t wall_ms);
  void handle_status(const std::string &envelope);
  void handle_event(const std::string &envelope, std::int64_t wall_ms);
  void handle_response(const std::string &envelope, std::int64_t wall_ms);
  // Sends one request. Returns false, with the refusal in `error`, when the
  // bridge would not take it.
  bool send(const std::string &action, Pending pending, std::uint64_t steady_ms,
            std::string &error);
  bool send_json(const std::string &request, Pending pending,
                 std::uint64_t steady_ms, std::string &error);
  void ask_for_menu(std::uint64_t steady_ms);
  void maybe_read_back(std::uint64_t steady_ms, std::int64_t wall_ms);
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

  HudCalls calls_;
  int com_ = 1;
  std::uint64_t active_hz_ = 0;
  // The channel last asked about, and whether that was while reachable: a
  // console that comes back is asked again.
  std::uint64_t asked_hz_ = 0;
  bool asked_reachable_ = false;
  bool auto_readback_ = false;
  std::set<std::string> in_flight_;
  // Readbacks already pressed for the pilot, so one transmission is read
  // back once.
  std::set<std::string> auto_pressed_;
  // When a transmission last finished playing, by channel.
  std::unordered_map<std::uint64_t, std::int64_t> heard_at_;
  std::string call_error_;
  std::function<void(const std::string &, std::uint64_t)> speak_own_;
};

// The console's comm_log payload (a CommLogEntry) as a HUD line; nullopt when
// it carries no text to show.
std::optional<HudEntry> parse_hud_entry(const std::string &payload_json);

// The console's pilot_menu payload; nullopt when it names no frequency.
std::optional<HudCallMenu> parse_call_menu(const std::string &payload_json);

// How long after a transmission finished playing auto readback still reads it
// back when its menu arrives late.
inline constexpr std::int64_t kAutoReadbackWindowMs = 20'000;

} // namespace zoal_atc::gui

#endif
