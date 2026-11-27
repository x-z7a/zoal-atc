#ifndef ZOAL_ATC_GUI_HUD_SESSION_HPP
#define ZOAL_ATC_GUI_HUD_SESSION_HPP

#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/hud_model.hpp"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

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
  // A choice the pilot opened (the level, the runway): its options are shown
  // under the strip until one is pressed or it is pressed again. Null when
  // none is open, or the menu no longer offers it.
  const HudCallItem *open_choice() const;
  void toggle_choice(const std::string &key);
  // The pilot typed a call in the HUD: sent as a typed turn, and spoken in
  // their own voice once the console takes it, like a pressed one.
  void submit_text(const std::string &text, std::uint64_t steady_ms);

  // Tunes the selected COM to the channel the position sent the pilot to.
  void press_next_frequency(std::uint64_t steady_ms);
  // A controller's transmission on this channel finished playing in the
  // cockpit. With auto readback on, a readback the menu offers is pressed.
  void reply_heard(std::uint64_t frequency_hz, std::uint64_t steady_ms,
                   std::int64_t wall_ms);
  bool auto_readback() const { return auto_readback_; }
  // Whether the HUD shows its line to type a call in: on unless the pilot
  // turned it off in the panel's settings.
  bool shows_typing() const { return shows_typing_; }
  // Whether the HUD shows the last few transmissions: on unless the pilot
  // turned it off. The radio log in the panel keeps them either way.
  bool shows_transcript() const { return shows_transcript_; }
  // Whether the transcript includes what other aircraft and their controllers
  // said: on unless the pilot turned it off, when it shows only their own
  // exchanges. The radio log in the panel keeps everyone's either way.
  bool shows_overheard() const { return shows_overheard_; }
  // What the last press could not do; empty when it went.
  const std::string &call_error() const { return call_error_; }
  void dismiss_call_error() { call_error_.clear(); }
  // The push-to-talk button was pressed while the cockpit was still saying
  // something on the channel (x-z7a/atc#1138): busy, the HUD says so; clear,
  // that notice goes, and nothing else the pilot was told.
  void set_frequency_busy(bool busy);
  // A key pressed and not yet answered, so a button is not sent twice.
  bool call_in_flight(const std::string &key) const {
    return in_flight_.count(key) > 0;
  }
  // A controller's line is shown when its voice starts, not when its text
  // arrives: the reply is queued behind the pilot's own call, and words on the
  // HUD before anybody has said them read as the answer arriving before the
  // question. On only where the cockpit speaks replies; a line whose voice
  // never starts is shown after kHudHoldMs anyway.
  void set_holds_until_spoken(bool holds) { holds_until_spoken_ = holds; }
  // A controller's transmission started playing, or will not play at all: the
  // line with these words is shown now.
  void reply_spoken(const std::string &text, std::int64_t wall_ms);

  // Speaks a call the console took, in the pilot's voice, on this channel.
  void set_own_call_speaker(
      std::function<void(const std::string &, std::uint64_t, const std::string &)> speak) {
    speak_own_ = std::move(speak);
  }
  bool awaiting_own_call() const;

  // Whether a controller's transmission on this channel is still waiting to
  // play or playing. Auto readback waits for it: a readback is of what the
  // pilot has heard, and a readback pressed while the controller is still
  // queued answers words nobody has heard yet. Out of Ottawa three Center
  // transmissions queued behind one another, and "proceed direct TUKIR" was
  // read back twenty seconds before it played, because the one before it had
  // just finished.
  void set_controller_speech_pending(std::function<bool(std::uint64_t)> pending) {
    controller_speech_pending_ = std::move(pending);
  }

  // Whether the pilot may key up now: not while the cockpit is still saying
  // something on the channel they transmit on (tts::may_key_up). A pressed or
  // typed call is keying up, so the call buttons are shut, a press or a typed
  // line is refused with the reason, and an automatic readback waits, as
  // push-to-talk does (x-z7a/atc#1143). Unset, the pilot always may.
  void set_may_key_up(std::function<bool()> may) { may_key_up_ = std::move(may); }
  bool may_key_up() const { return !may_key_up_ || may_key_up_(); }

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
    // language is the language sentence is said in: empty for English.
    std::string language;
  };

  void handle(const std::string &envelope, std::uint64_t steady_ms,
              std::int64_t wall_ms);
  void handle_status(const std::string &envelope);
  void handle_event(const std::string &envelope, std::uint64_t steady_ms,
                    std::int64_t wall_ms);
  void handle_response(const std::string &envelope, std::int64_t wall_ms);
  // Sends one request. Returns false, with the refusal in `error`, when the
  // bridge would not take it.
  bool send(const std::string &action, Pending pending, std::uint64_t steady_ms,
            std::string &error);
  bool send_json(const std::string &request, Pending pending,
                 std::uint64_t steady_ms, std::string &error);
  void ask_for_menu(std::uint64_t steady_ms);
  std::string radio_fields() const;
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
  std::uint64_t next_menu_retry_ms_ = 0;
  int menu_retries_left_ = 0;
  bool auto_readback_ = false;
  bool shows_typing_ = true;
  bool shows_transcript_ = true;
  bool shows_overheard_ = true;
  std::set<std::string> in_flight_;
  // Readbacks already pressed for the pilot, so one transmission is read
  // back once.
  std::set<std::string> auto_pressed_;
  // When a transmission last finished playing, by channel.
  std::unordered_map<std::uint64_t, std::int64_t> heard_at_;
  std::string call_error_;
  std::string open_choice_;
  bool holds_until_spoken_ = false;
  struct HeldLine {
    HudEntry entry;
    std::int64_t held_at_ms = 0;
  };
  std::vector<HeldLine> held_;
  // Words whose voice already started, for a line that arrives after it: the
  // reply and its line reach the cockpit as two frames, in either order.
  std::vector<HeldLine> spoken_;
  std::function<void(const std::string &, std::uint64_t, const std::string &)> speak_own_;
  std::function<bool(std::uint64_t)> controller_speech_pending_;
  std::function<bool()> may_key_up_;
};

// The console's comm_log payload (a CommLogEntry) as a HUD line; nullopt when
// it carries no text to show.
std::optional<HudEntry> parse_hud_entry(const std::string &payload_json);

// The console's pilot_menu payload; nullopt when it names no frequency.
std::optional<HudCallMenu> parse_call_menu(const std::string &payload_json);

// How long after a transmission finished playing auto readback still reads it
// back when its menu arrives late.
inline constexpr std::int64_t kAutoReadbackWindowMs = 20'000;

// The longest a controller's line waits for its voice before it is shown.
inline constexpr std::int64_t kHudHoldMs = 15'000;

} // namespace zoal_atc::gui

#endif
