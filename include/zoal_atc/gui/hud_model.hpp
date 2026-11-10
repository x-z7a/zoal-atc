#ifndef ZOAL_ATC_GUI_HUD_MODEL_HPP
#define ZOAL_ATC_GUI_HUD_MODEL_HPP

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zoal_atc::gui {

// What the radio HUD shows and how its menu behaves, with no X-Plane and no
// ImGui: the window (src/xplane) draws this, and feeds it what the console
// says and what the pilot clicks. Every time is the wall clock in Unix
// milliseconds, passed in, so the rules are testable without waiting.

// One transmission as the console reports it (the panel's CommLogEntry).
struct HudEntry {
  std::string kind;          // "pilot" or "atc"
  std::string text;
  std::string at;            // ISO-8601 when it was said; may be empty
  std::string spoken_by;     // controller position ("tower", ...); may be empty
  std::string addressed_to;  // on an overheard line, whose it was
  bool overheard = false;    // a party-line copy addressed to someone else
};

// The HUD is not a second radio log. It answers "what did ATC just say?" with
// the panel closed: only what arrived while it was up, three lines at most,
// each for a minute.
inline constexpr std::size_t kHudLineLimit = 3;
inline constexpr std::int64_t kHudLifespanMs = 60'000;
// How long before the end a line starts to fade, so it leaves rather than
// vanishing mid-read.
inline constexpr std::int64_t kHudFadeMs = 5'000;
// Slack for the console's clock and this computer's disagreeing. A line whose
// own timestamp is older than its lifespan plus this was said before the HUD
// was looking: the plugin replays its newest cached line on attach, and a
// clearance from the last session is not news.
inline constexpr std::int64_t kHudClockSkewMs = 30'000;

struct HudLine {
  HudEntry entry;
  // When the HUD first showed it. The lifespan runs from here rather than from
  // the console's timestamp, so a skewed clock cannot cut a line short.
  std::int64_t shown_at_ms = 0;
};

// The transmissions the HUD is showing.
class HudFeed {
public:
  // A transmission arrived. Ignored when it is a line already seen (the
  // plugin replays its newest line) or was said long before now.
  void add(const HudEntry &entry, std::int64_t now_ms);
  // A new flight: what the last one was told is not this one's business.
  void clear();
  // Drops what has outlived its minute.
  void expire(std::int64_t now_ms);

  const std::vector<HudLine> &lines() const { return lines_; }
  bool empty() const { return lines_.empty(); }

private:
  bool seen_before(const HudEntry &entry) const;

  std::vector<HudLine> lines_;
  // Stamped lines recently seen, so a replay is recognised even after the
  // line itself has left the HUD.
  std::deque<HudEntry> seen_;
};

bool hud_line_fading(const HudLine &line, std::int64_t now_ms);
// 1 while a line is fresh, falling to 0 over its last kHudFadeMs.
float hud_line_opacity(const HudLine &line, std::int64_t now_ms);

// How a line is labelled and coloured, as the panel's CommBubble does it.
enum class HudTone { pilot, surface, tower, delivery, advisory, radar, atc };

// The speaker label: this cockpit's callsign (or "You") for the pilot's own
// words, the other aircraft for an overheard pilot line, and the controller's
// position for ATC.
std::string hud_speaker(const HudEntry &entry, std::string_view own_callsign);
HudTone hud_tone(const HudEntry &entry);
// "Tower", "Clearance Delivery", ...; "ATC" for no seat or an unknown one.
std::string format_position(std::string_view position);

// Unix milliseconds for an ISO-8601 timestamp ("2026-11-07T12:00:00Z",
// fractional seconds and a +hh:mm offset allowed; no offset reads as UTC).
std::optional<std::int64_t> parse_iso8601_ms(std::string_view text);

// --- the bar's menu ----------------------------------------------------------

// What a press asks the window to do; it runs the action and reports back
// with action_succeeded / action_failed.
enum class HudAction { none, reload_plan, end_flight, open_panel };

// How long "New flight" stays armed: it throws a clearance away, and must not
// be one stray click. The same as the panel's.
inline constexpr std::int64_t kHudArmedMs = 5'000;
// How long a button shows what it just did, in place of its label.
inline constexpr std::int64_t kHudDoneMs = 3'000;

class HudMenu {
public:
  // The handle: opens or closes the menu, forgetting an armed press, a
  // finished one and an error.
  void toggle();

  // The three buttons. Each returns the action to run, or none when the press
  // only changed the menu (arming New flight) or the button is disabled.
  HudAction press_reload(std::int64_t now_ms);
  HudAction press_new_flight(std::int64_t now_ms);
  HudAction press_open_panel();

  void action_succeeded(HudAction action, std::int64_t now_ms);
  void action_failed(HudAction action, const std::string &error);
  void dismiss_error() { error_.clear(); }

  // Lapses an armed New flight and a finished button's label.
  void tick(std::int64_t now_ms);

  // Whether the console is up, connected and subscribed; buttons that reach it
  // are disabled until it is.
  void set_ready(bool ready) { ready_ = ready; }

  bool open() const { return open_; }
  bool busy() const { return busy_; }
  bool armed() const { return armed_; }
  // The action whose button shows its "done" label, or none.
  HudAction done() const { return done_; }
  const std::string &error() const { return error_; }
  bool reload_enabled() const { return ready_ && !busy_; }
  bool new_flight_enabled() const { return ready_ && !busy_; }
  bool open_panel_enabled() const { return ready_; }

private:
  HudAction start(HudAction action);

  bool open_ = false;
  bool ready_ = false;
  bool busy_ = false;
  bool armed_ = false;
  std::int64_t armed_at_ms_ = 0;
  HudAction done_ = HudAction::none;
  std::int64_t done_at_ms_ = 0;
  std::string error_;
};

} // namespace zoal_atc::gui

#endif
