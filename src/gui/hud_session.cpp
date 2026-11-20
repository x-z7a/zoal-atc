#include "zoal_atc/gui/hud_session.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace zoal_atc::gui {

namespace {

using transport::json_raw_field;
using transport::json_unquote;

// Messages drained per frame. The bridge caps the queue; this caps the work.
constexpr std::size_t kMaxMessagesPerPump = 64;

// Top-level fields only: an envelope wraps a console payload with keys of its
// own ("kind" in a comm_log entry), which must not answer for the envelope.
std::optional<std::string> top_string(const std::string &json,
                                      std::string_view key) {
  const auto raw = json_raw_field(json, key);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  return json_unquote(*raw);
}

bool top_bool(const std::string &json, std::string_view key) {
  const auto raw = json_raw_field(json, key);
  return raw.has_value() && *raw == "true";
}

std::optional<std::uint64_t> top_uint(const std::string &json,
                                      std::string_view key) {
  const auto raw = json_raw_field(json, key);
  if (!raw.has_value() || raw->empty()) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : *raw) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return value;
}

const char *action_name(HudAction action) {
  switch (action) {
  case HudAction::reload_plan:
    return "refresh_flight_plan";
  case HudAction::end_flight:
    return "end_flight_session";
  case HudAction::open_panel:
    return "open_panel";
  case HudAction::none:
    break;
  }
  return "";
}

std::string request_json(const std::string &action) {
  // The reload names no pilot ID: the plugin's own SimBrief ID is used, as the
  // React HUD's empty pilotId asked for.
  if (action == "refresh_flight_plan") {
    return R"({"action":"refresh_flight_plan","payload":{"pilotId":""}})";
  }
  return R"({"action":")" + transport::json_escape(action) + R"("})";
}

} // namespace

HudTheme hud_theme_from_name(const std::string &name) {
  if (name == "daylight") {
    return HudTheme::daylight;
  }
  if (name == "night") {
    return HudTheme::night;
  }
  return HudTheme::graphite;
}

// One button: a call, or a choice whose options are calls.
static std::optional<HudCallItem> parse_call_item(const std::string &element, bool top) {
  HudCallItem item;
  item.key = top_string(element, "key").value_or("");
  item.label = top_string(element, "label").value_or("");
  item.sentence = top_string(element, "sentence").value_or("");
  item.readback = top_bool(element, "readback");
  if (top) {
    if (const auto raw = json_raw_field(element, "options")) {
      if (const auto options = transport::json_array_elements(*raw)) {
        for (const std::string &option : *options) {
          if (auto parsed = parse_call_item(option, false)) {
            item.options.push_back(std::move(*parsed));
          }
        }
      }
    }
  }
  if (item.key.empty()) {
    return std::nullopt;
  }
  if (!item.options.empty()) {
    if (item.label.empty()) {
      item.label = item.key;
    }
    return item;
  }
  // A button with nothing to say is not one a pilot can press.
  if (item.sentence.empty()) {
    return std::nullopt;
  }
  if (item.label.empty()) {
    item.label = item.sentence;
  }
  return item;
}

std::optional<HudCallMenu> parse_call_menu(const std::string &payload_json) {
  HudCallMenu menu;
  menu.frequency_hz = top_uint(payload_json, "frequencyHz").value_or(0);
  if (menu.frequency_hz == 0) {
    return std::nullopt;
  }
  menu.role = top_string(payload_json, "role").value_or("");
  menu.station = top_string(payload_json, "station").value_or("");
  menu.station_name = top_string(payload_json, "stationName").value_or("");
  menu.revision = top_uint(payload_json, "revision").value_or(0);
  menu.next_frequency_hz = top_uint(payload_json, "nextFrequencyHz").value_or(0);
  if (const auto raw = json_raw_field(payload_json, "items")) {
    if (const auto elements = transport::json_array_elements(*raw)) {
      for (const std::string &element : *elements) {
        if (auto item = parse_call_item(element, true)) {
          menu.items.push_back(std::move(*item));
        }
      }
    }
  }
  return menu;
}

std::optional<HudEntry> parse_hud_entry(const std::string &payload_json) {
  HudEntry entry;
  entry.text = top_string(payload_json, "text").value_or("");
  if (entry.text.empty()) {
    return std::nullopt;
  }
  entry.kind = top_string(payload_json, "kind").value_or("");
  entry.at = top_string(payload_json, "at").value_or("");
  entry.spoken_by = top_string(payload_json, "spokenBy").value_or("");
  entry.addressed_to = top_string(payload_json, "addressedTo").value_or("");
  entry.overheard = top_bool(payload_json, "overheard");
  return entry;
}

HudSession::HudSession(GuiBridge &bridge) : bridge_(bridge) {}

void HudSession::attach(std::uint64_t steady_ms) {
  attached_ = true;
  menu_.set_ready(true);
  bridge_.attach_panel(steady_ms, GuiView::hud);
  settings_asked_ = false;
}

void HudSession::detach() {
  if (!attached_) {
    return;
  }
  attached_ = false;
  menu_.set_ready(false);
  pending_.clear();
  bridge_.detach_panel(GuiView::hud);
}

void HudSession::set_visible(bool visible, std::uint64_t steady_ms) {
  if (attached_) {
    bridge_.set_panel_visible(visible, steady_ms, GuiView::hud);
  }
}

void HudSession::pump(std::uint64_t steady_ms, std::int64_t wall_ms) {
  if (attached_ && !settings_asked_) {
    // The theme is the plugin's to say; until it answers, the default stands.
    // Asked here rather than on attach: the plugin attaches the HUD while it
    // is still starting, before the handler that answers is installed.
    settings_asked_ = true;
    std::string ignored;
    send("settings", Pending{Request::settings, HudAction::none}, steady_ms,
         ignored);
  }
  if (attached_) {
    for (const JsMessage &message :
         bridge_.take_js_messages(kMaxMessagesPerPump, GuiView::hud)) {
      handle(message.payload, wall_ms);
    }
  }
  if (attached_ && reachable_ && active_hz_ != 0 &&
      (asked_hz_ != active_hz_ || !asked_reachable_)) {
    // A channel the pilot just tuned, or a console that just came back: ask
    // the position on it what it would take.
    ask_for_menu(steady_ms);
  }
  spoken_.erase(std::remove_if(spoken_.begin(), spoken_.end(),
                               [wall_ms](const HeldLine &said) {
                                 return wall_ms - said.held_at_ms > kHudHoldMs;
                               }),
                spoken_.end());
  // A line whose voice never started is not lost: shown once it has waited
  // long enough that nothing is still going to say it.
  for (auto it = held_.begin(); it != held_.end();) {
    if (wall_ms - it->held_at_ms >= kHudHoldMs) {
      feed_.add(it->entry, wall_ms);
      it = held_.erase(it);
    } else {
      ++it;
    }
  }
  menu_.tick(wall_ms);
  feed_.expire(wall_ms);
}

void HudSession::reply_spoken(const std::string &text, std::int64_t wall_ms) {
  for (auto it = held_.begin(); it != held_.end(); ++it) {
    if (it->entry.text == text) {
      feed_.add(it->entry, wall_ms);
      held_.erase(it);
      return;
    }
  }
  // Not here yet: remembered, so it is shown the moment it arrives.
  HudEntry said;
  said.text = text;
  spoken_.push_back(HeldLine{said, wall_ms});
  constexpr std::size_t kSpokenKept = 8;
  if (spoken_.size() > kSpokenKept) {
    spoken_.erase(spoken_.begin());
  }
}

void HudSession::set_radio(int com, std::uint64_t active_hz,
                           std::uint64_t) {
  com_ = (com == 2) ? 2 : 1;
  active_hz_ = active_hz;
}

void HudSession::ask_for_menu(std::uint64_t steady_ms) {
  asked_hz_ = active_hz_;
  asked_reachable_ = true;
  std::string ignored;
  // The radio the console reads is the one the gateway stamps on the
  // request, so the payload names nothing.
  send_json(R"({"action":"pilot_menu","payload":{}})",
            Pending{Request::menu, HudAction::none, "", "", active_hz_},
            steady_ms, ignored);
}

void HudSession::press_call(const std::string &key, std::uint64_t steady_ms,
                            std::int64_t) {
  const HudCallMenu *menu = calls();
  const HudCallItem *item = menu == nullptr ? nullptr : menu->find(key);
  if (item == nullptr || in_flight_.count(key) > 0) {
    return;
  }
  if (!item->options.empty()) {
    // A choice: its options are the calls.
    toggle_choice(key);
    return;
  }
  open_choice_.clear();
  call_error_.clear();
  const std::string request =
      R"({"action":"pilot_call","payload":{"key":")" +
      transport::json_escape(item->key) + R"(","sentence":")" +
      transport::json_escape(item->sentence) + R"(","role":")" +
      transport::json_escape(item->role) + R"("}})";
  std::string error;
  if (!send_json(request,
                 Pending{Request::call, HudAction::none, item->key,
                         item->sentence, active_hz_},
                 steady_ms, error)) {
    call_error_ = error.empty() ? "the call could not be sent" : error;
    return;
  }
  in_flight_.insert(item->key);
}

void HudSession::toggle_choice(const std::string &key) {
  open_choice_ = open_choice_ == key ? std::string() : key;
}

const HudCallItem *HudSession::open_choice() const {
  if (open_choice_.empty()) {
    return nullptr;
  }
  const HudCallMenu *menu = calls();
  const HudCallItem *item = menu == nullptr ? nullptr : menu->find(open_choice_);
  return item != nullptr && !item->options.empty() ? item : nullptr;
}

void HudSession::submit_text(const std::string &text, std::uint64_t steady_ms) {
  std::string said = text;
  const auto first = said.find_first_not_of(" \t");
  const auto last = said.find_last_not_of(" \t");
  said = first == std::string::npos ? std::string() : said.substr(first, last - first + 1);
  if (said.empty()) {
    return;
  }
  call_error_.clear();
  const std::string request = R"({"action":"submit_text","payload":{"text":")" +
                              transport::json_escape(said) + R"("}})";
  std::string error;
  if (!send_json(request, Pending{Request::call, HudAction::none, "", said, active_hz_},
                 steady_ms, error)) {
    call_error_ = error.empty() ? "the message could not be sent" : error;
  }
}

void HudSession::press_next_frequency(std::uint64_t steady_ms) {
  const HudCallMenu *menu = calls();
  if (menu == nullptr || menu->next_frequency_hz == 0 ||
      menu->next_frequency_hz == active_hz_) {
    return;
  }
  const std::string request =
      R"({"action":"tune_radio","payload":{"mhz":)" +
      format_mhz(menu->next_frequency_hz) + R"(,"com":)" +
      std::to_string(com_) + R"(,"target":"active"}})";
  std::string error;
  if (!send_json(request,
                 Pending{Request::tune, HudAction::none, "", "",
                         menu->next_frequency_hz},
                 steady_ms, error)) {
    call_error_ = error.empty() ? "the radio could not be tuned" : error;
  }
}

void HudSession::reply_heard(std::uint64_t frequency_hz,
                             std::uint64_t steady_ms, std::int64_t wall_ms) {
  if (frequency_hz == 0) {
    frequency_hz = active_hz_;
  }
  heard_at_[frequency_hz] = wall_ms;
  maybe_read_back(steady_ms, wall_ms);
}

// Auto readback presses the readback the moment there is both a transmission
// the pilot heard and a menu offering its readback, whichever came second.
// Only a transmission that played counts: the readback says the pilot heard
// it, and a pilot whose radio did not play it did not.
void HudSession::maybe_read_back(std::uint64_t steady_ms,
                                 std::int64_t wall_ms) {
  if (!auto_readback_) {
    return;
  }
  const HudCallMenu *menu = calls();
  const HudCallItem *item = menu == nullptr ? nullptr : menu->readback();
  if (item == nullptr || auto_pressed_.count(item->key) > 0) {
    return;
  }
  const auto heard = heard_at_.find(active_hz_);
  if (heard == heard_at_.end() || wall_ms - heard->second > kAutoReadbackWindowMs) {
    return;
  }
  auto_pressed_.insert(item->key);
  press_call(item->key, steady_ms, wall_ms);
}

void HudSession::on_settings_changed(const std::string &payload_json) {
  if (const auto theme = top_string(payload_json, "theme")) {
    theme_ = hud_theme_from_name(*theme);
  }
  if (const auto raw = json_raw_field(payload_json, "autoReadback")) {
    auto_readback_ = *raw == "true";
  }
  if (const auto raw = json_raw_field(payload_json, "hudTyping")) {
    shows_typing_ = *raw != "false";
  }
  if (const auto raw = json_raw_field(payload_json, "hudTranscript")) {
    shows_transcript_ = *raw != "false";
  }
}

void HudSession::press_reload(std::uint64_t steady_ms, std::int64_t wall_ms) {
  run(menu_.press_reload(wall_ms), steady_ms, wall_ms);
}

void HudSession::press_new_flight(std::uint64_t steady_ms,
                                  std::int64_t wall_ms) {
  run(menu_.press_new_flight(wall_ms), steady_ms, wall_ms);
}

void HudSession::press_open_panel(std::uint64_t steady_ms) {
  run(menu_.press_open_panel(), steady_ms, 0);
}

void HudSession::run(HudAction action, std::uint64_t steady_ms,
                     std::int64_t) {
  if (action == HudAction::none) {
    return;
  }
  std::string error;
  if (!send(action_name(action), Pending{Request::action, action}, steady_ms,
            error)) {
    menu_.action_failed(action, error);
  }
}

bool HudSession::send(const std::string &action, Pending pending,
                      std::uint64_t steady_ms, std::string &error) {
  return send_json(request_json(action), std::move(pending), steady_ms, error);
}

bool HudSession::send_json(const std::string &request, Pending pending,
                           std::uint64_t steady_ms, std::string &error) {
  if (!attached_) {
    error = "the HUD is not attached to the console";
    return false;
  }
  const SubmitResult result = bridge_.submit(request, steady_ms, GuiView::hud);
  if (!result.accepted) {
    error = top_string(result.ack_json, "error").value_or("");
    return false;
  }
  // Local actions answer inside submit, so the answer may already be queued;
  // recording the request before the next pump is all that ordering needs.
  pending_[result.request_id] = pending;
  return true;
}

void HudSession::handle(const std::string &envelope, std::int64_t wall_ms) {
  const auto kind = top_string(envelope, "kind");
  if (!kind.has_value()) {
    return;
  }
  if (*kind == "status") {
    handle_status(envelope);
  } else if (*kind == "event") {
    handle_event(envelope, wall_ms);
  } else if (*kind == "response") {
    handle_response(envelope, wall_ms);
  }
}

void HudSession::handle_status(const std::string &envelope) {
  reachable_ = top_bool(envelope, "connected") && top_bool(envelope, "subscribed");
  if (!reachable_) {
    asked_reachable_ = false;
    // Nothing in flight on a socket that went away will be answered.
    in_flight_.clear();
  }
}

void HudSession::handle_event(const std::string &envelope,
                              std::int64_t wall_ms) {
  const auto event = top_string(envelope, "event");
  const auto payload = json_raw_field(envelope, "payload");
  if (!event.has_value() || !payload.has_value()) {
    return;
  }
  if (*event == "comm_log") {
    if (const auto entry = parse_hud_entry(*payload)) {
      const bool already_spoken =
          std::any_of(spoken_.begin(), spoken_.end(),
                      [&](const HeldLine &said) { return said.entry.text == entry->text; });
      if (holds_until_spoken_ && entry->kind == "atc" && !entry->overheard && !already_spoken) {
        held_.push_back(HeldLine{*entry, wall_ms});
      } else {
        feed_.add(*entry, wall_ms);
      }
    }
  } else if (*event == "flight_snapshot") {
    callsign_ = top_string(*payload, "callsign").value_or("");
  } else if (*event == "pilot_menu") {
    if (auto menu = parse_call_menu(*payload)) {
      calls_.put(std::move(*menu));
      // A newer menu settles what was in flight on it: a key it no longer
      // offers was answered, and one it still offers may be pressed again.
      in_flight_.clear();
      maybe_read_back(0, wall_ms);
    }
  }
}

void HudSession::handle_response(const std::string &envelope,
                                 std::int64_t wall_ms) {
  const auto id = top_uint(envelope, "request_id");
  if (!id.has_value()) {
    return;
  }
  const auto found = pending_.find(*id);
  if (found == pending_.end()) {
    return;
  }
  const Pending pending = found->second;
  pending_.erase(found);
  const bool ok = top_bool(envelope, "ok");

  switch (pending.kind) {
  case Request::menu:
    return;
  case Request::tune:
    if (!ok) {
      call_error_ = top_string(envelope, "error").value_or("the radio could not be tuned");
    }
    return;
  case Request::call:
    in_flight_.erase(pending.key);
    if (!ok) {
      call_error_ = top_string(envelope, "error").value_or("the console refused the call");
      return;
    }
    // Taken: the pilot hears themselves make it, as on a real radio.
    if (speak_own_) {
      speak_own_(pending.sentence, pending.frequency_hz);
    }
    return;
  case Request::action:
  case Request::settings:
    break;
  }

  if (pending.kind == Request::settings) {
    if (ok) {
      if (const auto payload = json_raw_field(envelope, "payload")) {
        on_settings_changed(*payload);
      }
    }
    return;
  }
  if (!ok) {
    menu_.action_failed(pending.action,
                        top_string(envelope, "error").value_or(""));
    return;
  }
  menu_.action_succeeded(pending.action, wall_ms);
  if (pending.action == HudAction::end_flight) {
    // A new flight: what the last one was told is not this one's business.
    feed_.clear();
  }
}

} // namespace zoal_atc::gui
