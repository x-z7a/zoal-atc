#include "zoal_atc/gui/gui_bridge.hpp"

#include <algorithm>
#include <utility>

namespace zoal_atc::gui {

namespace {

constexpr GuiView kAllViews[kGuiViewCount] = {GuiView::panel, GuiView::hud};

} // namespace

GuiBridge::GuiBridge(GuiBridgeConfig config) : config_(std::move(config)) {}

SubmitResult GuiBridge::submit(const std::string &panel_request_json,
                               std::uint64_t now_ms, GuiView view) {
  const auto request = parse_panel_request(panel_request_json);
  if (!request.has_value()) {
    return {false, 0, build_request_ack(false, 0, "malformed request")};
  }

  // Local actions are offered the request first, and deliberately called
  // outside the lock: a handler may write a file, and the socket thread should
  // not wait on that. They also run before the connected check, because a
  // setting that repairs the connection cannot require one.
  //
  // The handler is copied under the lock rather than read through config_,
  // since set_local_action can assign it. Copying a std::function per submit is
  // nothing next to the request it is about to answer.
  std::function<std::optional<LocalAnswer>(const std::string &,
                                           const std::string &)>
      local_action;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    local_action = config_.local_action;
  }
  if (local_action) {
    if (auto answer = local_action(request->action, request->payload_json)) {
      std::lock_guard<std::mutex> lock(mutex_);
      GuiResponse response;
      response.request_id = next_request_id_++;
      response.ok = answer->ok;
      response.payload_json = answer->payload_json;
      response.error = answer->error;
      // Receipt now, answer on console.event - the same two steps a proxied
      // request takes, so the page needs no second code path for these.
      enqueue_js_locked(view, build_js_response(response));
      return {true, response.request_id,
              build_request_ack(true, response.request_id, "")};
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) {
    // Refuse now rather than let the page wait out a timeout to learn what the
    // status line already says.
    return {false, 0, build_request_ack(false, 0, "console disconnected")};
  }
  if (pending_.size() >= config_.max_pending) {
    return {false, 0,
            build_request_ack(false, 0, "too many requests in flight")};
  }

  const std::uint64_t id = next_request_id_++;
  pending_[id] = Pending{now_ms + config_.request_timeout_ms, false, view};

  GuiRequest frame;
  frame.request_id = id;
  frame.action = request->action;
  frame.payload_json = request->payload_json;
  socket_frames_.push_back(build_gui_request(frame));

  return {true, id, build_request_ack(true, id, "")};
}

std::string GuiBridge::attach_panel(std::uint64_t now_ms, GuiView view) {
  std::lock_guard<std::mutex> lock(mutex_);
  ViewState &state = view_locked(view);
  state.attached = true;
  state.visible = true;
  // A page that just loaded holds no history, and anything queued for the
  // previous one is about a screen that no longer exists.
  state.queue.clear();
  if (connected_) {
    subscribe_locked(now_ms);
  }
  replay_locked(view);
  return R"({"ok":true,"replayed":)" + std::to_string(cache_.size()) + "}";
}

void GuiBridge::set_local_action(
    std::function<std::optional<LocalAnswer>(const std::string &,
                                             const std::string &)>
        handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.local_action = std::move(handler);
}

void GuiBridge::detach_panel(GuiView view) {
  std::lock_guard<std::mutex> lock(mutex_);
  ViewState &state = view_locked(view);
  state.attached = false;
  state.visible = false;
  state.queue.clear();
  // A page's requests outlive it only as leaked pending slots; the answers have
  // nowhere to go.
  for (auto entry = pending_.begin(); entry != pending_.end();) {
    if (!entry->second.internal && entry->second.view == view) {
      entry = pending_.erase(entry);
    } else {
      ++entry;
    }
  }
  if (any_attached_locked()) {
    return;
  }
  // The last page is gone, so nobody is listening. Our own subscribe goes with
  // it, and the next attach re-asserts it.
  pending_.clear();
  subscribe_request_id_ = 0;
  retired_subscribes_.clear();
  subscribed_ = false;
}

void GuiBridge::set_panel_visible(bool visible, std::uint64_t now_ms,
                                  GuiView view) {
  (void)now_ms;
  std::lock_guard<std::mutex> lock(mutex_);
  ViewState &state = view_locked(view);
  if (state.visible == visible) {
    return;
  }
  state.visible = visible;
  if (visible && state.attached) {
    // Show what is true now, not the backlog of what was true while hidden.
    replay_locked(view);
  }
}

bool GuiBridge::on_socket_frame(const std::string &frame,
                                std::uint64_t now_ms) {
  if (!is_gui_frame(frame)) {
    return false;
  }

  if (const auto response = parse_gui_response(frame); response.has_value()) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto entry = pending_.find(response->request_id);
    if (entry == pending_.end()) {
      const auto retired =
          std::find(retired_subscribes_.begin(), retired_subscribes_.end(),
                    response->request_id);
      if (retired != retired_subscribes_.end()) {
        // Our own subscribe, answered after we stopped waiting. A late yes
        // still means the console is listening; a late no changes nothing,
        // because a newer subscribe is already in flight or scheduled.
        retired_subscribes_.erase(retired);
        if (response->ok && !subscribed_) {
          // The retry still in flight is now redundant, and must not be left
          // to time out and undo this.
          if (subscribe_request_id_ != 0) {
            pending_.erase(subscribe_request_id_);
            subscribe_request_id_ = 0;
          }
          settle_subscribe_locked(*response, now_ms);
        }
      }
      // Otherwise a timed-out page request, or an id from a connection that
      // has since been replaced. There is no promise left to settle.
      return true;
    }
    const Pending pending = entry->second;
    pending_.erase(entry);
    if (pending.internal && response->request_id == subscribe_request_id_) {
      subscribe_request_id_ = 0;
    }
    settle_locked(pending, *response, now_ms);
    return true;
  }

  if (const auto event = parse_gui_event(frame); event.has_value()) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cached =
        std::find_if(cache_.begin(), cache_.end(),
                     [&](const GuiEvent &held) {
                       return held.event == event->event;
                     });
    const bool superseded = cached != cache_.end();
    if (cached == cache_.end()) {
      cache_.push_back(*event);
    } else {
      cached->payload_json = event->payload_json;
    }
    for (const GuiView view : kAllViews) {
      ViewState &state = view_locked(view);
      if (state.attached && state.visible) {
        enqueue_js_locked(view, build_js_event(*event));
      } else if (superseded) {
        // An update this page never saw and that the cache no longer holds is
        // genuinely lost to it, so say so rather than let it believe it saw
        // everything.
        ++state.dropped_events;
      }
    }
    return true;
  }

  (void)now_ms;
  return true;
}

void GuiBridge::set_connected(bool connected, std::uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (connected_ == connected) {
    return;
  }
  connected_ = connected;
  // Whatever a dead socket was waiting on, the new one will not answer.
  retired_subscribes_.clear();
  if (!connected) {
    subscribed_ = false;
    fail_all_locked("console disconnected", now_ms);
  } else if (any_attached_locked()) {
    // The console holds no memory of a socket that went away, so a reconnect
    // re-asserts the subscription instead of assuming it survived.
    subscribe_locked(now_ms);
  }
  enqueue_status_locked();
}

void GuiBridge::tick(std::uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::uint64_t> expired;
  for (const auto &entry : pending_) {
    if (now_ms >= entry.second.deadline_ms) {
      expired.push_back(entry.first);
    }
  }
  // Stable order so a burst of timeouts reaches the page oldest first.
  std::sort(expired.begin(), expired.end());
  for (const std::uint64_t id : expired) {
    const auto entry = pending_.find(id);
    const Pending pending = entry->second;
    pending_.erase(entry);
    if (pending.internal && id == subscribe_request_id_) {
      subscribe_request_id_ = 0;
      // Not answered yet is not refused: keep listening for it.
      retire_subscribe_locked(id);
    }
    GuiResponse response;
    response.request_id = id;
    response.ok = false;
    response.error = "request timed out";
    settle_locked(pending, response, now_ms);
  }

  // A subscribe that failed is asked again rather than left failed: the socket
  // is up, so the console is there to ask.
  if (connected_ && any_attached_locked() && !subscribed_ &&
      subscribe_request_id_ == 0 && now_ms >= next_subscribe_ms_) {
    subscribe_locked(now_ms);
  }
}

std::vector<std::string> GuiBridge::take_socket_frames() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> frames(socket_frames_.begin(), socket_frames_.end());
  socket_frames_.clear();
  return frames;
}

std::vector<JsMessage> GuiBridge::take_js_messages(std::size_t max,
                                                   GuiView view) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &queue = view_locked(view).queue;
  std::vector<JsMessage> messages;
  const std::size_t count = std::min(max, queue.size());
  messages.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    messages.push_back(std::move(queue.front()));
    queue.pop_front();
  }
  return messages;
}

std::size_t GuiBridge::pending_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.size();
}

std::size_t GuiBridge::dropped_events(GuiView view) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return view_locked(view).dropped_events;
}

bool GuiBridge::subscribed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return subscribed_;
}

bool GuiBridge::connected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

std::vector<std::string> GuiBridge::take_log_lines() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> lines(log_lines_.begin(), log_lines_.end());
  log_lines_.clear();
  return lines;
}

void GuiBridge::settle_locked(const Pending &pending,
                              const GuiResponse &response,
                              std::uint64_t now_ms) {
  if (pending.internal) {
    settle_subscribe_locked(response, now_ms);
    return;
  }
  // A response is delivered even to a hidden page: it is a correlated answer
  // to something the page asked for, and its JavaScript is still waiting.
  enqueue_js_locked(pending.view, build_js_response(response));
}

void GuiBridge::settle_subscribe_locked(const GuiResponse &response,
                                        std::uint64_t now_ms) {
  const bool was = subscribed_;
  subscribed_ = response.ok;
  if (subscribed_ && !was) {
    log_lines_.push_back("console subscribed: the panel is live");
  }
  if (!response.ok) {
    next_subscribe_ms_ = now_ms + config_.subscribe_retry_ms;
    // A dropped socket says so itself; this is for the case it does not.
    if (connected_) {
      log_lines_.push_back("console subscribe failed (" + response.error +
                           "); asking again in " +
                           std::to_string(config_.subscribe_retry_ms / 1000) +
                           "s");
    }
  }
  while (log_lines_.size() > 16) {
    log_lines_.pop_front();
  }
  if (was != subscribed_) {
    enqueue_status_locked();
  }
}

void GuiBridge::fail_all_locked(const std::string &reason,
                                std::uint64_t now_ms) {
  std::vector<std::uint64_t> ids;
  ids.reserve(pending_.size());
  for (const auto &entry : pending_) {
    ids.push_back(entry.first);
  }
  std::sort(ids.begin(), ids.end());
  subscribe_request_id_ = 0;
  for (const std::uint64_t id : ids) {
    const auto entry = pending_.find(id);
    const Pending pending = entry->second;
    pending_.erase(entry);
    GuiResponse response;
    response.request_id = id;
    response.ok = false;
    response.error = reason;
    settle_locked(pending, response, now_ms);
  }
}

void GuiBridge::enqueue_js_locked(GuiView view, std::string payload) {
  ViewState &state = view_locked(view);
  if (!state.attached) {
    return;
  }
  if (state.queue.size() >= config_.max_js_queue) {
    // Drop the oldest: on a radio panel, missing state beats stale state.
    state.queue.pop_front();
    ++state.dropped_events;
  }
  state.queue.push_back(JsMessage{kConsoleEventChannel, std::move(payload)});
}

void GuiBridge::enqueue_status_locked() {
  for (const GuiView view : kAllViews) {
    enqueue_status_locked(view);
  }
}

void GuiBridge::enqueue_status_locked(GuiView view) {
  GuiStatus status;
  status.connected = connected_;
  status.subscribed = subscribed_;
  status.pending = pending_.size();
  status.dropped_events = view_locked(view).dropped_events;
  enqueue_js_locked(view, build_js_status(status));
}

void GuiBridge::replay_locked(GuiView view) {
  enqueue_status_locked(view);
  for (const auto &event : cache_) {
    enqueue_js_locked(view, build_js_event(event));
  }
}

void GuiBridge::subscribe_locked(std::uint64_t now_ms) {
  // Only the newest subscribe can still be answered usefully, so retire the
  // previous one rather than leaving it to time out. A page that reloads a few
  // times would otherwise show a pending count that is pure bookkeeping - and
  // `pending` is what the panel's own status line reports to the pilot.
  if (subscribe_request_id_ != 0) {
    pending_.erase(subscribe_request_id_);
    // Superseded, not refused: if the console answers it, that answer counts.
    retire_subscribe_locked(subscribe_request_id_);
  }
  const std::uint64_t id = next_request_id_++;
  subscribe_request_id_ = id;
  pending_[id] = Pending{now_ms + config_.request_timeout_ms, true,
                         GuiView::panel};
  socket_frames_.push_back(build_gui_subscribe(id, config_.topics));
}

void GuiBridge::retire_subscribe_locked(std::uint64_t request_id) {
  retired_subscribes_.push_back(request_id);
  while (retired_subscribes_.size() > 8) {
    retired_subscribes_.pop_front();
  }
}

GuiBridge::ViewState &GuiBridge::view_locked(GuiView view) {
  return views_[static_cast<std::size_t>(view)];
}

const GuiBridge::ViewState &GuiBridge::view_locked(GuiView view) const {
  return views_[static_cast<std::size_t>(view)];
}

bool GuiBridge::any_attached_locked() const {
  for (const auto &state : views_) {
    if (state.attached) {
      return true;
    }
  }
  return false;
}

} // namespace zoal_atc::gui
