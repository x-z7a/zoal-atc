#include "zoal_atc/gui/gui_bridge.hpp"

#include <algorithm>
#include <utility>

namespace zoal_atc::gui {

GuiBridge::GuiBridge(GuiBridgeConfig config) : config_(std::move(config)) {}

SubmitResult GuiBridge::submit(const std::string &panel_request_json,
                               std::uint64_t now_ms) {
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
      // request takes, so the panel needs no second code path for these.
      if (panel_attached_) {
        enqueue_js_locked(build_js_response(response));
      }
      return {true, response.request_id,
              build_request_ack(true, response.request_id, "")};
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) {
    // Refuse now rather than let the panel wait out a timeout to learn what the
    // status line already says.
    return {false, 0, build_request_ack(false, 0, "console disconnected")};
  }
  if (pending_.size() >= config_.max_pending) {
    return {false, 0,
            build_request_ack(false, 0, "too many requests in flight")};
  }

  const std::uint64_t id = next_request_id_++;
  pending_[id] = Pending{now_ms + config_.request_timeout_ms, false};

  GuiRequest frame;
  frame.request_id = id;
  frame.action = request->action;
  frame.payload_json = request->payload_json;
  socket_frames_.push_back(build_gui_request(frame));

  return {true, id, build_request_ack(true, id, "")};
}

std::string GuiBridge::attach_panel(std::uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  panel_attached_ = true;
  panel_visible_ = true;
  // A page that just loaded holds no history, and anything queued for the
  // previous one is about a screen that no longer exists.
  js_queue_.clear();
  if (connected_) {
    subscribe_locked(now_ms);
  }
  replay_locked();
  return R"({"ok":true,"replayed":)" + std::to_string(cache_.size()) + "}";
}

void GuiBridge::set_local_action(
    std::function<std::optional<LocalAnswer>(const std::string &,
                                             const std::string &)>
        handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.local_action = std::move(handler);
}

void GuiBridge::detach_panel() {
  std::lock_guard<std::mutex> lock(mutex_);
  panel_attached_ = false;
  panel_visible_ = false;
  js_queue_.clear();
  // Panel requests outlive their page only as leaked pending slots; the answers
  // have nowhere to go. Our own subscribe is dropped with them, so the next
  // attach re-asserts it.
  pending_.clear();
  subscribe_request_id_ = 0;
  subscribed_ = false;
}

void GuiBridge::set_panel_visible(bool visible, std::uint64_t now_ms) {
  (void)now_ms;
  std::lock_guard<std::mutex> lock(mutex_);
  if (panel_visible_ == visible) {
    return;
  }
  panel_visible_ = visible;
  if (visible && panel_attached_) {
    // Show what is true now, not the backlog of what was true while hidden.
    replay_locked();
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
      // A timed-out request, or an id from a connection that has since been
      // replaced. There is no promise left to settle.
      return true;
    }
    const bool internal = entry->second.internal;
    pending_.erase(entry);
    settle_locked(response->request_id, *response, internal);
    return true;
  }

  if (const auto event = parse_gui_event(frame); event.has_value()) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cached =
        std::find_if(cache_.begin(), cache_.end(),
                     [&](const GuiEvent &held) {
                       return held.event == event->event;
                     });
    const bool superseded_unseen =
        cached != cache_.end() && !can_see_events_locked();
    if (cached == cache_.end()) {
      cache_.push_back(*event);
    } else {
      cached->payload_json = event->payload_json;
    }
    // Every event, once, regardless of whether a panel was watching. This is
    // the notification path: what it exists for is precisely the case where
    // nobody saw the event because the window was closed.
    notify_queue_.push_back(*event);
    while (notify_queue_.size() > config_.max_js_queue) {
      notify_queue_.pop_front();
    }

    if (can_see_events_locked()) {
      enqueue_js_locked(build_js_event(*event));
    } else if (superseded_unseen) {
      // An update nobody saw and that the cache no longer holds is genuinely
      // lost, so say so rather than let the panel believe it saw everything.
      ++dropped_events_;
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
  if (!connected) {
    subscribed_ = false;
    fail_all_locked("console disconnected");
  } else if (panel_attached_) {
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
  // Stable order so a burst of timeouts reaches the panel oldest first.
  std::sort(expired.begin(), expired.end());
  for (const std::uint64_t id : expired) {
    const auto entry = pending_.find(id);
    const bool internal = entry->second.internal;
    pending_.erase(entry);
    GuiResponse response;
    response.request_id = id;
    response.ok = false;
    response.error = "request timed out";
    settle_locked(id, response, internal);
  }
}

std::vector<std::string> GuiBridge::take_socket_frames() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> frames(socket_frames_.begin(), socket_frames_.end());
  socket_frames_.clear();
  return frames;
}

std::vector<JsMessage> GuiBridge::take_js_messages(std::size_t max) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<JsMessage> messages;
  const std::size_t count = std::min(max, js_queue_.size());
  messages.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    messages.push_back(std::move(js_queue_.front()));
    js_queue_.pop_front();
  }
  return messages;
}

std::vector<GuiEvent> GuiBridge::take_notification_events(std::size_t max) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<GuiEvent> events;
  const std::size_t count = std::min(max, notify_queue_.size());
  events.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    events.push_back(std::move(notify_queue_.front()));
    notify_queue_.pop_front();
  }
  return events;
}

std::size_t GuiBridge::pending_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.size();
}

std::size_t GuiBridge::dropped_events() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_events_;
}

bool GuiBridge::subscribed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return subscribed_;
}

bool GuiBridge::connected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

void GuiBridge::settle_locked(std::uint64_t request_id,
                              const GuiResponse &response, bool internal) {
  if (internal) {
    if (request_id == subscribe_request_id_) {
      subscribe_request_id_ = 0;
    }
    const bool was = subscribed_;
    subscribed_ = response.ok;
    if (was != subscribed_) {
      enqueue_status_locked();
    }
    return;
  }
  if (!panel_attached_) {
    return;
  }
  // A response is delivered even to a hidden panel: it is a correlated answer
  // to something the page asked for, and its JavaScript is still waiting.
  enqueue_js_locked(build_js_response(response));
}

void GuiBridge::fail_all_locked(const std::string &reason) {
  std::vector<std::uint64_t> ids;
  ids.reserve(pending_.size());
  for (const auto &entry : pending_) {
    ids.push_back(entry.first);
  }
  std::sort(ids.begin(), ids.end());
  for (const std::uint64_t id : ids) {
    const auto entry = pending_.find(id);
    const bool internal = entry->second.internal;
    pending_.erase(entry);
    GuiResponse response;
    response.request_id = id;
    response.ok = false;
    response.error = reason;
    settle_locked(id, response, internal);
  }
}

void GuiBridge::enqueue_js_locked(std::string payload) {
  if (!panel_attached_) {
    return;
  }
  if (js_queue_.size() >= config_.max_js_queue) {
    // Drop the oldest: on a radio panel, missing state beats stale state.
    js_queue_.pop_front();
    ++dropped_events_;
  }
  js_queue_.push_back(JsMessage{kConsoleEventChannel, std::move(payload)});
}

void GuiBridge::enqueue_status_locked() {
  GuiStatus status;
  status.connected = connected_;
  status.subscribed = subscribed_;
  status.pending = pending_.size();
  status.dropped_events = dropped_events_;
  enqueue_js_locked(build_js_status(status));
}

void GuiBridge::replay_locked() {
  enqueue_status_locked();
  for (const auto &event : cache_) {
    enqueue_js_locked(build_js_event(event));
  }
}

void GuiBridge::subscribe_locked(std::uint64_t now_ms) {
  // Only the newest subscribe can still be answered usefully, so retire the
  // previous one rather than leaving it to time out. A page that reloads a few
  // times would otherwise show a pending count that is pure bookkeeping - and
  // `pending` is what the panel's own status line reports to the pilot.
  if (subscribe_request_id_ != 0) {
    pending_.erase(subscribe_request_id_);
  }
  const std::uint64_t id = next_request_id_++;
  subscribe_request_id_ = id;
  pending_[id] = Pending{now_ms + config_.request_timeout_ms, true};
  socket_frames_.push_back(build_gui_subscribe(id, config_.topics));
}

bool GuiBridge::can_see_events_locked() const {
  return panel_attached_ && panel_visible_;
}

} // namespace zoal_atc::gui
