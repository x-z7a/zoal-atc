#ifndef ZOAL_ATC_GUI_GUI_BRIDGE_HPP
#define ZOAL_ATC_GUI_GUI_BRIDGE_HPP

#include "zoal_atc/gui/gui_frames.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace zoal_atc::gui {

// The channel every plugin->browser message travels on. Responses, pushed
// events, and bridge status share it, discriminated by the envelope's "kind".
inline constexpr const char *kConsoleEventChannel = "console.event";

// JsMessage is one Skyscript post the main thread still owes the browser.
struct JsMessage {
  std::string channel;
  std::string payload;
};

struct GuiBridgeConfig {
  // How long a panel request may wait for the console before the panel is told
  // it will not be answered. A promise that never settles is worse than a
  // refusal: the panel would show a spinner for the rest of the flight.
  std::uint64_t request_timeout_ms = 10000;
  // Caps that make a wedged console or a runaway panel cost bounded memory.
  std::size_t max_pending = 32;
  std::size_t max_js_queue = 256;
  std::vector<std::string> topics{"flight", "facility"};
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
  SubmitResult submit(const std::string &panel_request_json,
                      std::uint64_t now_ms);

  // attach_panel records that the page loaded and is listening. It replays the
  // cached snapshot so a reloaded panel is current before the console pushes
  // anything new. Returns the ack JSON for the console.ready call.
  std::string attach_panel(std::uint64_t now_ms);

  // detach_panel drops everything owed to a page that is gone.
  void detach_panel();

  // set_panel_visible gates the high-volume event stream. A hidden panel keeps
  // only the latest of each event; showing it again replays that.
  void set_panel_visible(bool visible, std::uint64_t now_ms);

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

  // take_js_messages returns at most `max` posts for Skyscript, oldest first.
  std::vector<JsMessage> take_js_messages(std::size_t max);

  // take_notification_events returns console events that arrived since the last
  // call, oldest first, whether or not the panel was there to see them.
  //
  // Deliberately a second queue rather than a read of the js one. The js queue
  // is what the *browser* is owed, and a hidden panel is owed nothing -- which
  // is exactly the case a notification exists for. Bounded and drop-oldest like
  // everything else here: an unattended sim must not accumulate toasts.
  std::vector<GuiEvent> take_notification_events(std::size_t max);

  [[nodiscard]] std::size_t pending_count() const;
  [[nodiscard]] std::size_t dropped_events() const;
  [[nodiscard]] bool subscribed() const;
  [[nodiscard]] bool connected() const;

private:
  struct Pending {
    std::uint64_t deadline_ms = 0;
    // internal marks the bridge's own subscribe: it settles `subscribed_` and
    // is never forwarded to a browser that did not ask for it.
    bool internal = false;
  };

  void settle_locked(std::uint64_t request_id, const GuiResponse &response,
                     bool internal);
  void fail_all_locked(const std::string &reason);
  void enqueue_js_locked(std::string payload);
  void enqueue_status_locked();
  void replay_locked();
  void subscribe_locked(std::uint64_t now_ms);
  [[nodiscard]] bool can_see_events_locked() const;

  GuiBridgeConfig config_;
  mutable std::mutex mutex_;

  std::uint64_t next_request_id_ = 1;
  std::unordered_map<std::uint64_t, Pending> pending_;
  // The subscribe still awaiting an answer, so re-subscribing can retire it
  // instead of stacking a new one behind it. Zero means none outstanding.
  std::uint64_t subscribe_request_id_ = 0;

  bool connected_ = false;
  bool subscribed_ = false;
  bool panel_attached_ = false;
  bool panel_visible_ = false;

  std::deque<std::string> socket_frames_;
  std::deque<JsMessage> js_queue_;
  std::deque<GuiEvent> notify_queue_;

  // The latest payload of each event, in first-seen order. This is the snapshot
  // a panel gets on load, on unhide, and after a reconnect - the plugin caching
  // what the console last said, not modelling what it meant.
  std::vector<GuiEvent> cache_;
  std::size_t dropped_events_ = 0;
};

} // namespace zoal_atc::gui

#endif // ZOAL_ATC_GUI_GUI_BRIDGE_HPP
