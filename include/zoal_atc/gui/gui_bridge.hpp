#ifndef ZOAL_ATC_GUI_GUI_BRIDGE_HPP
#define ZOAL_ATC_GUI_GUI_BRIDGE_HPP

#include "zoal_atc/gui/gui_frames.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zoal_atc::gui {

// The channel every plugin->browser message travels on. Responses, pushed
// events, and bridge status share it, discriminated by the envelope's "kind".
inline constexpr const char *kConsoleEventChannel = "console.event";

// GuiView names a page the bridge serves. Each is its own Skyscript window
// with its own JavaScript, so each is attached, shown and owed messages on its
// own -- but they share one console subscription and one event cache.
//
//   - panel:    the full radio panel, opened and closed by the pilot;
//   - hud:      the HUD's bar and menu, a fixed-size overlay that stays up
//               while the panel is closed;
//   - hud_feed: the HUD's last few transmissions, in a window of their own
//               under the bar, so the lines coming and going never resize
//               the bar's window.
enum class GuiView : std::size_t { panel = 0, hud = 1, hud_feed = 2 };
inline constexpr std::size_t kGuiViewCount = 3;

// JsMessage is one Skyscript post the main thread still owes the browser.
struct JsMessage {
  std::string channel;
  std::string payload;
};

// LocalAnswer is a response the plugin produced by itself, without the console.
struct LocalAnswer {
  bool ok = true;
  // Raw JSON value, as a console answer's payload would be.
  std::string payload_json;
  std::string error;
};

struct GuiBridgeConfig {
  // How long a panel request may wait for the console before the panel is told
  // it will not be answered. A promise that never settles is worse than a
  // refusal: the panel would show a spinner for the rest of the flight.
  std::uint64_t request_timeout_ms = 10000;
  // How long after a failed subscribe the bridge asks again. The socket being
  // up is not the console listening: the subscribe is answered by a worker
  // behind a stream, and one answer that is lost or late must not leave the
  // panel showing an unreachable console for the rest of the flight.
  std::uint64_t subscribe_retry_ms = 5000;
  // Caps that make a wedged console or a runaway panel cost bounded memory.
  std::size_t max_pending = 32;
  std::size_t max_js_queue = 256;
  std::vector<std::string> topics{"flight", "facility"};

  // Actions the plugin answers itself. Consulted before the connected check,
  // and nullopt means "not mine" so the request proxies to the console as usual.
  //
  // This exists for exactly one reason: the connection settings are how a pilot
  // repairs a console they cannot reach. Gated on the socket like every other
  // action, the one control that fixes a bad token would be disabled precisely
  // when it is needed, and the only way out would be hand-editing zoal_atc.cfg -
  // which is the thing the panel was built to replace.
  //
  // Called on the X-Plane main thread and outside the bridge's lock, so a
  // handler may touch the filesystem without blocking the socket thread. It
  // must still be quick: it runs inside a Skyscript handler.
  std::function<std::optional<LocalAnswer>(const std::string &action,
                                           const std::string &payload_json)>
      local_action;
};

// SubmitResult is what a console.request call resolves to in the browser.
struct SubmitResult {
  bool accepted = false;
  std::uint64_t request_id = 0;
  std::string ack_json;
};

// GuiBridge is the plugin's whole GUI proxy: it turns browser requests into
// console frames, matches the answers back, and holds what the panel should see
// when it is next looked at.
//
// Three threads touch it and the split is the point:
//
//   - the websocket receiver thread only *feeds* it (on_socket_frame,
//     set_connected). It never reaches Skyscript, because CEF is pumped from
//     X-Plane's main thread and calling into it from a socket thread is how a
//     sim crashes;
//   - the X-Plane main thread runs the Skyscript handlers (submit, attach) and
//     the flight loop (tick, take_*), which is the only place messages are
//     handed to the browser and frames are handed to the socket;
//   - nothing here blocks on either. Every queue is bounded and every drop is
//     counted, so a panel that is hidden, never opened, or wedged cannot stall
//     the radio.
//
// It is deliberately SDK-free and clock-free: `now_ms` is passed in, so the
// timeout, disconnect, and reconnect behaviour is table-testable without a sim.
class GuiBridge {
public:
  explicit GuiBridge(GuiBridgeConfig config = {});

  GuiBridge(const GuiBridge &) = delete;
  GuiBridge &operator=(const GuiBridge &) = delete;

  // --- X-Plane main thread: Skyscript message handlers ----------------------

  // submit turns one console.request envelope into a pending console request.
  // The answer goes back to the view that asked.
  SubmitResult submit(const std::string &panel_request_json,
                      std::uint64_t now_ms, GuiView view = GuiView::panel);

  // attach_panel records that a page loaded and is listening. It replays the
  // cached snapshot so a reloaded page is current before the console pushes
  // anything new. Returns the ack JSON for the console.ready call.
  std::string attach_panel(std::uint64_t now_ms, GuiView view = GuiView::panel);

  // detach_panel drops everything owed to a page that is gone. The console
  // subscription is dropped only with the last page.
  void detach_panel(GuiView view = GuiView::panel);

  // Installs the local action handler after construction. The bridge is a
  // static and the plugin only learns its config path at XPluginStart, so the
  // handler cannot be supplied to the constructor.
  void set_local_action(
      std::function<std::optional<LocalAnswer>(const std::string &action,
                                               const std::string &payload_json)>
          handler);

  // set_panel_visible gates the high-volume event stream. A hidden page keeps
  // only the latest of each event; showing it again replays that.
  void set_panel_visible(bool visible, std::uint64_t now_ms,
                         GuiView view = GuiView::panel);

  // --- websocket receiver thread -------------------------------------------

  // on_socket_frame consumes gui_* frames. It returns true when the frame was
  // one, so the caller stops trying other parsers.
  bool on_socket_frame(const std::string &frame, std::uint64_t now_ms);

  // set_connected reports the socket's state. Losing it settles every pending
  // request as failed rather than leaving the panel waiting on a dead link.
  void set_connected(bool connected, std::uint64_t now_ms);

  // --- X-Plane main thread: flight loop ------------------------------------

  void tick(std::uint64_t now_ms);

  // take_socket_frames returns the frames the flight loop should publish.
  std::vector<std::string> take_socket_frames();

  // take_js_messages returns at most `max` posts for one view, oldest first.
  std::vector<JsMessage> take_js_messages(std::size_t max,
                                          GuiView view = GuiView::panel);

  [[nodiscard]] std::size_t pending_count() const;
  // Updates this view never saw: replaced in the cache while it was hidden, or
  // pushed out of its full queue. Per view, because the HUD seeing an event
  // says nothing about whether the closed panel did.
  [[nodiscard]] std::size_t dropped_events(GuiView view = GuiView::panel) const;
  [[nodiscard]] bool subscribed() const;
  [[nodiscard]] bool connected() const;

  // take_log_lines returns what the bridge has to say about its own
  // subscription since the last call. The bridge is SDK-free and is fed from
  // the socket thread, so it cannot log itself; the flight loop drains this.
  std::vector<std::string> take_log_lines();

private:
  struct Pending {
    std::uint64_t deadline_ms = 0;
    // internal marks the bridge's own subscribe: it settles `subscribed_` and
    // is never forwarded to a browser that did not ask for it.
    bool internal = false;
    // The page that asked, and so the one its answer is owed to.
    GuiView view = GuiView::panel;
  };

  struct ViewState {
    bool attached = false;
    bool visible = false;
    std::deque<JsMessage> queue;
    std::size_t dropped_events = 0;
  };

  void settle_locked(const Pending &pending, const GuiResponse &response,
                     std::uint64_t now_ms);
  void settle_subscribe_locked(const GuiResponse &response,
                               std::uint64_t now_ms);
  void fail_all_locked(const std::string &reason, std::uint64_t now_ms);
  void enqueue_js_locked(GuiView view, std::string payload);
  void enqueue_status_locked();
  void enqueue_status_locked(GuiView view);
  void replay_locked(GuiView view);
  void subscribe_locked(std::uint64_t now_ms);
  void retire_subscribe_locked(std::uint64_t request_id);
  [[nodiscard]] ViewState &view_locked(GuiView view);
  [[nodiscard]] const ViewState &view_locked(GuiView view) const;
  [[nodiscard]] bool any_attached_locked() const;

  GuiBridgeConfig config_;
  mutable std::mutex mutex_;

  std::uint64_t next_request_id_ = 1;
  std::unordered_map<std::uint64_t, Pending> pending_;
  // The subscribe still awaiting an answer, so re-subscribing can retire it
  // instead of stacking a new one behind it. Zero means none outstanding.
  std::uint64_t subscribe_request_id_ = 0;
  // Subscribes this connection gave up waiting on. The console still answers
  // them, just late, and a late "yes" is as good as a prompt one -- throwing it
  // away is how a healthy console came to read as unreachable.
  std::deque<std::uint64_t> retired_subscribes_;
  // When a failed subscribe may be asked again; see subscribe_retry_ms.
  std::uint64_t next_subscribe_ms_ = 0;
  std::deque<std::string> log_lines_;

  bool connected_ = false;
  bool subscribed_ = false;
  ViewState views_[kGuiViewCount];

  std::deque<std::string> socket_frames_;

  // The latest payload of each event, in first-seen order. This is the snapshot
  // a panel gets on load, on unhide, and after a reconnect - the plugin caching
  // what the console last said, not modelling what it meant.
  std::vector<GuiEvent> cache_;
};

} // namespace zoal_atc::gui

#endif // ZOAL_ATC_GUI_GUI_BRIDGE_HPP
