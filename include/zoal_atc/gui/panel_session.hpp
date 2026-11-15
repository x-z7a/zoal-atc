#ifndef ZOAL_ATC_GUI_PANEL_SESSION_HPP
#define ZOAL_ATC_GUI_PANEL_SESSION_HPP

#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/hud_model.hpp"
#include "zoal_atc/gui/hud_session.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zoal_atc::gui {

// What the radio panel shows and does, with no X-Plane and no ImGui: the
// window (src/xplane/panel_window.cpp) draws this and feeds it what the pilot
// clicks and types.
//
// It is the panel's end of the GUI bridge (GuiView::panel), the conversation
// the React panel had with the plugin over Skyscript, held in C++: the same
// envelopes in, the same console.request envelopes out, the same actions by
// the same names. Nothing about the bridge or the console changes.
//
// Main thread only, and clock-free: `steady_ms` is the plugin's monotonic
// clock, which the bridge times requests with; `wall_ms` is Unix milliseconds,
// which arming and telemetry freshness are timed with.

// What the console says about own-ship (the console's PanelTelemetry view).
struct PanelTelemetry {
  double frequency_mhz = 0.0;  // 0 when the console sent none
  double standby_mhz = 0.0;
  double altitude_ft_msl = 0.0;
  double groundspeed_kts = 0.0;
  bool on_ground = false;
  bool paused = false;
  std::string airport;
  // "current", or the word for why traffic is not known.
  std::string surveillance;
  std::int64_t traffic_count = 0;
};

struct PanelFlightPlan {
  bool has_plan = false;
  std::string origin;
  std::string destination;
  std::string aircraft_icao;
  std::string route;
  std::string first_fix;
  double cruise_alt_ft = 0.0;
};

// The bridge's own status envelope.
struct PanelLink {
  bool connected = false;
  bool subscribed = false;
  std::uint64_t pending = 0;
  std::uint64_t dropped_events = 0;
};

// Which silence this is, if any. An unreachable console outranks stale
// telemetry: when nobody is listening to the radio, "no telemetry for 12s"
// describes a symptom and hides the cause.
enum class PanelFreshness { fresh, paused, stale, unreachable };

// One set of buttons' requests: whether any is in flight, and the console's
// words for the last one it refused. An action that silently does nothing is
// indistinguishable from one that worked.
struct PanelRunner {
  int in_flight = 0;
  std::string error;
  bool busy() const { return in_flight > 0; }
};

// The log is capped here as well as in the console: a panel left open for a
// long flight must not grow without bound inside the sim's process.
inline constexpr std::size_t kPanelCommLimit = 100;
// How long "New flight" stays armed before it forgets it was pressed.
inline constexpr std::int64_t kPanelArmedMs = 5'000;
// Telemetry is published about once a second, so silence well past that means
// this aircraft's picture has stopped arriving.
inline constexpr std::int64_t kTelemetryStaleMs = 4'000;

// The placeholder for "no value yet", not "the value is nothing".
inline constexpr const char *kUnknown = "\xE2\x80\x94"; // an em dash

// --- the words on the panel (format.ts) --------------------------------------

std::string format_thousands(double value);
std::string format_frequency(double mhz);
std::string format_standby(double mhz);
std::string format_altitude(const PanelTelemetry &telemetry);
std::string format_groundspeed(double knots);
std::string format_traffic(const PanelTelemetry &telemetry);
std::string format_facility_count(std::int64_t count);
std::string format_plan_route(const std::optional<PanelFlightPlan> &plan);
std::string format_plan_detail(const std::optional<PanelFlightPlan> &plan);
std::string format_dropped_events(std::uint64_t dropped);
// "connected", "subscribing", "disconnected", "connected (2 in flight)".
std::string describe_link(const PanelLink &link);
// "1.234" -> 1.234; nullopt for anything that is not a number at all. Whether
// it is in the COM band is the plugin's answer to give, in its own words.
std::optional<double> parse_frequency(const std::string &text);

std::optional<PanelTelemetry> parse_panel_telemetry(const std::string &json);
std::optional<PanelFlightPlan> parse_panel_flight_plan(const std::string &json);

const char *hud_theme_name(HudTheme theme);

class PanelSession {
public:
  explicit PanelSession(GuiBridge &bridge);

  // The panel exists: attach to the bridge, which replays its cached snapshot.
  // The window stays attached and visible to the bridge while it is closed --
  // drawing nothing costs nothing natively, and a panel that hears every line
  // never has to ask the console what it missed.
  void attach(std::uint64_t steady_ms);
  void detach();

  // Drains what the bridge owes the panel and lapses timers. Once a frame.
  void pump(std::uint64_t steady_ms, std::int64_t wall_ms);

  // The plugin's settings.changed notice (a theme saved).
  void on_settings_changed(const std::string &payload_json);

  // --- what is shown -----------------------------------------------------

  const PanelLink &link() const { return link_; }
  // The console is there: the socket is up and the subscription answered.
  bool reachable() const { return link_.connected && link_.subscribed; }
  const std::optional<PanelTelemetry> &telemetry() const { return telemetry_; }
  bool has_flight() const { return has_flight_; }
  const std::string &callsign() const { return callsign_; }
  const std::string &lifecycle() const { return lifecycle_; }
  std::optional<std::int64_t> facility_count() const { return facility_count_; }
  const std::optional<PanelFlightPlan> &plan() const { return plan_; }

  // The radio log, oldest first. `comm_revision` changes whenever it does, so
  // the view can follow new lines without comparing them.
  const std::vector<HudEntry> &comm() const { return comm_; }
  std::uint64_t comm_revision() const { return comm_revision_; }

  PanelFreshness freshness(std::int64_t wall_ms) const;
  // The banner's words for it; empty when everything is current.
  std::string freshness_message(std::int64_t wall_ms) const;

  HudTheme theme() const { return theme_; }

  // The settings as the plugin stores them. The revisions change when an
  // answer replaces them, so a field showing one knows to reload.
  bool settings_loaded() const { return settings_loaded_; }
  const std::string &simbrief_pilot_id() const { return simbrief_pilot_id_; }
  std::uint64_t settings_revision() const { return settings_revision_; }
  bool connection_loaded() const { return connection_loaded_; }
  const std::string &token() const { return token_; }
  const std::string &console_url() const { return console_url_; }
  std::uint64_t connection_revision() const { return connection_revision_; }

  // "New flight" has been pressed once and is waiting for the confirm.
  bool armed() const { return armed_; }

  // Home's buttons; Settings' console-backed import; and the settings the
  // plugin answers itself, which never share a failure with the console.
  const PanelRunner &home() const { return home_; }
  const PanelRunner &settings() const { return settings_; }
  const PanelRunner &local() const { return local_; }

  // --- what the pilot does -------------------------------------------------

  void tune(const std::string &text, std::uint64_t steady_ms);
  void transmit(const std::string &text, std::uint64_t steady_ms);
  // Home's one-press import: the plugin's own SimBrief ID is used.
  void import_plan(std::uint64_t steady_ms);
  // Two presses, because this one throws a clearance away: the first arms,
  // the second (within kPanelArmedMs) ends the flight.
  void press_new_flight(std::uint64_t steady_ms, std::int64_t wall_ms);
  void save_token(const std::string &token, std::uint64_t steady_ms);
  void save_pilot_id(const std::string &pilot_id, std::uint64_t steady_ms);
  // Shown at once, saved behind it; a refused save puts the old one back.
  void pick_theme(HudTheme theme, std::uint64_t steady_ms);
  // Settings' import, naming the pilot ID stored here.
  void import_now(std::uint64_t steady_ms);

  void clear_home_error() { home_.error.clear(); }

private:
  enum class Request {
    settings_read,
    connection_read,
    comm_sync,
    plan_read,
    tune,
    transmit,
    import_plan,
    end_flight,
    save_token,
    save_settings,
    save_theme,
  };
  struct Pending {
    Request kind = Request::settings_read;
    PanelRunner *runner = nullptr;
    // The theme to put back if a theme save is refused.
    HudTheme previous_theme = HudTheme::graphite;
  };

  void send(const std::string &action, const std::string &payload_json,
            Pending pending, std::uint64_t steady_ms);
  void handle(const std::string &envelope, std::int64_t wall_ms,
              std::uint64_t steady_ms);
  void handle_status(const std::string &envelope, std::uint64_t steady_ms);
  void handle_event(const std::string &envelope, std::int64_t wall_ms);
  void handle_response(const std::string &envelope);
  void apply_settings(const std::string &payload_json);
  void apply_connection(const std::string &payload_json);
  void append_comm(const HudEntry &entry);
  void finish_comm_sync(const std::string &payload_json);
  void set_comm(std::vector<HudEntry> entries);

  GuiBridge &bridge_;
  bool attached_ = false;
  bool settings_asked_ = false;

  PanelLink link_;
  std::optional<PanelTelemetry> telemetry_;
  std::optional<std::int64_t> last_telemetry_wall_ms_;
  bool has_flight_ = false;
  std::string callsign_;
  std::string lifecycle_;
  std::optional<std::int64_t> facility_count_;
  std::optional<PanelFlightPlan> plan_;

  std::vector<HudEntry> comm_;
  std::uint64_t comm_revision_ = 0;
  // Lines pushed while a resync is in flight. The console read its record at
  // some moment between the request and the answer, so a line pushed in that
  // window may or may not be in what comes back; it is kept here and added to
  // the answer unless the answer already has it.
  std::optional<std::vector<HudEntry>> arrived_during_sync_;

  HudTheme theme_ = HudTheme::graphite;
  bool settings_loaded_ = false;
  std::string simbrief_pilot_id_;
  std::uint64_t settings_revision_ = 0;
  bool connection_loaded_ = false;
  std::string token_;
  std::string console_url_;
  std::uint64_t connection_revision_ = 0;

  bool armed_ = false;
  std::int64_t armed_at_ms_ = 0;

  PanelRunner home_;
  PanelRunner settings_;
  PanelRunner local_;
  std::unordered_map<std::uint64_t, Pending> pending_;
};

} // namespace zoal_atc::gui

#endif
