#include "zoal_atc/gui/hud_model.hpp"

#include <algorithm>
#include <cctype>

namespace zoal_atc::gui {

namespace {

// How many stamped lines are remembered for recognising a replay. The plugin
// replays only its newest line, so this is generous.
constexpr std::size_t kSeenLimit = 64;

bool said_too_long_ago(const HudEntry &entry, std::int64_t now_ms) {
  if (entry.at.empty()) {
    return false;
  }
  const auto at = parse_iso8601_ms(entry.at);
  return at.has_value() && *at < now_ms - kHudLifespanMs - kHudClockSkewMs;
}

// Only a line stamped with when it was said can be recognised again: two
// unstamped lines with the same words may genuinely be two transmissions.
bool same_line(const HudEntry &a, const HudEntry &b) {
  return a.at == b.at && a.kind == b.kind && a.text == b.text;
}

struct Position {
  std::string_view key;
  std::string_view name;
  HudTone tone;
};

constexpr Position kPositions[] = {
    {"clearance_delivery", "Clearance Delivery", HudTone::delivery},
    // The console names the seat by its role, "delivery"; the line above is
    // the name v1 used. Without this one, Delivery read "ATC" on the HUD.
    {"delivery", "Clearance Delivery", HudTone::delivery},
    {"ground", "Ground", HudTone::surface},
    {"tower", "Tower", HudTone::tower},
    {"departure", "Departure", HudTone::radar},
    {"approach", "Approach", HudTone::radar},
    {"center", "Center", HudTone::radar},
    {"oceanic", "Oceanic", HudTone::radar},
    {"ramp", "Ramp", HudTone::surface},
    {"ramp_control", "Ramp", HudTone::surface},
    {"fbo_or_handler", "FBO", HudTone::surface},
    {"unicom_ctaf", "UNICOM", HudTone::advisory},
};

const Position *find_position(std::string_view key) {
  for (const Position &position : kPositions) {
    if (position.key == key) {
      return &position;
    }
  }
  return nullptr;
}

// Reads exactly `digits` decimal digits at `pos`, advancing it.
bool read_number(std::string_view text, std::size_t &pos, std::size_t digits,
                 int &out) {
  if (pos + digits > text.size()) {
    return false;
  }
  int value = 0;
  for (std::size_t i = 0; i < digits; ++i) {
    const char c = text[pos + i];
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  pos += digits;
  out = value;
  return true;
}

bool expect_char(std::string_view text, std::size_t &pos, char want) {
  if (pos >= text.size() || text[pos] != want) {
    return false;
  }
  ++pos;
  return true;
}

// Days from 1970-01-01 to a proleptic Gregorian date (Howard Hinnant's
// days_from_civil).
std::int64_t days_from_civil(int year, int month, int day) {
  const int y = year - (month <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int mp = (month + 9) % 12;
  const int doy = (153 * mp + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<std::int64_t>(era) * 146097 + doe - 719468;
}

} // namespace

// --- feed ---------------------------------------------------------------------

void HudFeed::add(const HudEntry &entry, std::int64_t now_ms) {
  if (seen_before(entry)) {
    return;
  }
  if (!entry.at.empty()) {
    seen_.push_back(entry);
    if (seen_.size() > kSeenLimit) {
      seen_.pop_front();
    }
  }
  if (said_too_long_ago(entry, now_ms)) {
    return;
  }
  lines_.push_back(HudLine{entry, now_ms});
  expire(now_ms);
}

void HudFeed::clear() {
  lines_.clear();
  seen_.clear();
}

void HudFeed::expire(std::int64_t now_ms) {
  lines_.erase(std::remove_if(lines_.begin(), lines_.end(),
                              [now_ms](const HudLine &line) {
                                return now_ms - line.shown_at_ms >=
                                       kHudLifespanMs;
                              }),
               lines_.end());
  if (lines_.size() > kHudLineLimit) {
    lines_.erase(lines_.begin(),
                 lines_.end() - static_cast<std::ptrdiff_t>(kHudLineLimit));
  }
}

bool HudFeed::seen_before(const HudEntry &entry) const {
  if (entry.at.empty()) {
    return false;
  }
  return std::any_of(seen_.begin(), seen_.end(), [&](const HudEntry &held) {
    return same_line(held, entry);
  });
}

bool hud_line_fading(const HudLine &line, std::int64_t now_ms) {
  return now_ms - line.shown_at_ms >= kHudLifespanMs - kHudFadeMs;
}

float hud_line_opacity(const HudLine &line, std::int64_t now_ms) {
  const std::int64_t left = kHudLifespanMs - (now_ms - line.shown_at_ms);
  if (left >= kHudFadeMs) {
    return 1.0f;
  }
  if (left <= 0) {
    return 0.0f;
  }
  return static_cast<float>(left) / static_cast<float>(kHudFadeMs);
}

// --- labels -------------------------------------------------------------------

std::string format_position(std::string_view position) {
  const Position *found = find_position(position);
  return found ? std::string(found->name) : std::string("ATC");
}

std::string hud_speaker(const HudEntry &entry, std::string_view own_callsign) {
  if (entry.kind == "atc") {
    return format_position(entry.spoken_by);
  }
  // An overheard pilot transmission is another aircraft on the frequency, not
  // this one: labelled as this cockpit it reads as words the pilot just said.
  if (entry.overheard) {
    return entry.spoken_by.empty() ? std::string("Aircraft") : entry.spoken_by;
  }
  return own_callsign.empty() ? std::string("You") : std::string(own_callsign);
}

HudTone hud_tone(const HudEntry &entry) {
  if (entry.kind != "atc") {
    return HudTone::pilot;
  }
  const Position *found = find_position(entry.spoken_by);
  return found ? found->tone : HudTone::atc;
}

std::optional<std::int64_t> parse_iso8601_ms(std::string_view text) {
  std::size_t pos = 0;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (!read_number(text, pos, 4, year) || !expect_char(text, pos, '-') ||
      !read_number(text, pos, 2, month) || !expect_char(text, pos, '-') ||
      !read_number(text, pos, 2, day)) {
    return std::nullopt;
  }
  if (pos >= text.size() || (text[pos] != 'T' && text[pos] != 't' &&
                             text[pos] != ' ')) {
    return std::nullopt;
  }
  ++pos;
  if (!read_number(text, pos, 2, hour) || !expect_char(text, pos, ':') ||
      !read_number(text, pos, 2, minute) || !expect_char(text, pos, ':') ||
      !read_number(text, pos, 2, second)) {
    return std::nullopt;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
      minute > 59 || second > 60) {
    return std::nullopt;
  }

  // Fractional seconds, to the millisecond; further digits are ignored.
  std::int64_t millis = 0;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    int digits = 0;
    while (pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      if (digits < 3) {
        millis = millis * 10 + (text[pos] - '0');
      }
      ++digits;
      ++pos;
    }
    if (digits == 0) {
      return std::nullopt;
    }
    for (int i = digits; i < 3; ++i) {
      millis *= 10;
    }
  }

  std::int64_t offset_minutes = 0;
  if (pos < text.size()) {
    const char zone = text[pos];
    if (zone == 'Z' || zone == 'z') {
      ++pos;
    } else if (zone == '+' || zone == '-') {
      ++pos;
      int oh = 0, om = 0;
      if (!read_number(text, pos, 2, oh)) {
        return std::nullopt;
      }
      if (pos < text.size() && text[pos] == ':') {
        ++pos;
      }
      if (!read_number(text, pos, 2, om) || oh > 23 || om > 59) {
        return std::nullopt;
      }
      offset_minutes = (zone == '+' ? 1 : -1) * (oh * 60 + om);
    } else {
      return std::nullopt;
    }
  }
  if (pos != text.size()) {
    return std::nullopt;
  }

  const std::int64_t days = days_from_civil(year, month, day);
  const std::int64_t seconds = days * 86'400 + hour * 3'600 + minute * 60 +
                               second - offset_minutes * 60;
  return seconds * 1'000 + millis;
}

// --- menu ---------------------------------------------------------------------

void HudMenu::toggle() {
  open_ = !open_;
  armed_ = false;
  done_ = HudAction::none;
  error_.clear();
}

HudAction HudMenu::start(HudAction action) {
  error_.clear();
  done_ = HudAction::none;
  busy_ = true;
  return action;
}

HudAction HudMenu::press_reload(std::int64_t) {
  if (!reload_enabled()) {
    return HudAction::none;
  }
  return start(HudAction::reload_plan);
}

HudAction HudMenu::press_new_flight(std::int64_t now_ms) {
  if (!new_flight_enabled()) {
    return HudAction::none;
  }
  if (!armed_) {
    armed_ = true;
    armed_at_ms_ = now_ms;
    return HudAction::none;
  }
  armed_ = false;
  return start(HudAction::end_flight);
}

HudAction HudMenu::press_open_panel() {
  if (!open_panel_enabled()) {
    return HudAction::none;
  }
  return start(HudAction::open_panel);
}

void HudMenu::action_succeeded(HudAction action, std::int64_t now_ms) {
  busy_ = false;
  if (action == HudAction::reload_plan || action == HudAction::end_flight) {
    done_ = action;
    done_at_ms_ = now_ms;
  }
}

void HudMenu::action_failed(HudAction, const std::string &error) {
  busy_ = false;
  error_ = error.empty() ? std::string("the console refused") : error;
}

void HudMenu::tick(std::int64_t now_ms) {
  if (armed_ && now_ms - armed_at_ms_ >= kHudArmedMs) {
    armed_ = false;
  }
  if (done_ != HudAction::none && now_ms - done_at_ms_ >= kHudDoneMs) {
    done_ = HudAction::none;
  }
}

// --- calls -------------------------------------------------------------------

const HudCallItem *HudCallMenu::readback() const {
  for (const HudCallItem &item : items) {
    if (item.readback) {
      return &item;
    }
  }
  return nullptr;
}

const HudCallItem *HudCallMenu::find(std::string_view key) const {
  for (const HudCallItem &item : items) {
    if (item.key == key) {
      return &item;
    }
    for (const HudCallItem &option : item.options) {
      if (option.key == key) {
        return &option;
      }
    }
  }
  return nullptr;
}

void HudCalls::put(HudCallMenu menu) {
  for (HudCallItem &item : menu.items) {
    item.role = menu.role;
    for (HudCallItem &option : item.options) {
      option.role = menu.role;
    }
  }
  const std::uint64_t frequency_hz = menu.frequency_hz;
  bool replaced = false;
  for (auto it = menus_.begin(); it != menus_.end(); ++it) {
    if (it->frequency_hz != frequency_hz || it->role != menu.role) {
      continue;
    }
    // One position's older state arriving after its newer one is not news.
    // A different position for the role is: the pilot was handed to it.
    if (it->station == menu.station && menu.revision < it->revision) {
      return;
    }
    // Moved to the end: the most recently updated role is the one whose
    // calls come first.
    menus_.erase(it);
    menus_.push_back(std::move(menu));
    replaced = true;
    break;
  }
  if (!replaced) {
    // A handful of channels and roles is a whole flight; the oldest goes.
    constexpr std::size_t kHeld = 12;
    if (menus_.size() >= kHeld) {
      menus_.erase(menus_.begin());
    }
    menus_.push_back(std::move(menu));
  }
  merge(frequency_hz);
}

// merge is what the HUD shows for one channel: the menus of every role on it,
// as one. A channel two roles share (a tower that works ground control) has
// both roles' calls: every readback first, then the calls of the role heard
// from last, then the others, each label once.
void HudCalls::merge(std::uint64_t frequency_hz) {
  HudCallMenu merged;
  merged.frequency_hz = frequency_hz;
  std::vector<const HudCallMenu *> roles;
  for (auto it = menus_.rbegin(); it != menus_.rend(); ++it) {
    if (it->frequency_hz == frequency_hz) {
      roles.push_back(&*it);
    }
  }
  std::vector<std::string> labels;
  auto add = [&](const HudCallItem &item) {
    if (std::find(labels.begin(), labels.end(), item.label) != labels.end()) {
      return;
    }
    labels.push_back(item.label);
    merged.items.push_back(item);
  };
  for (const HudCallMenu *menu : roles) {
    for (const HudCallItem &item : menu->items) {
      if (item.readback) {
        add(item);
      }
    }
  }
  for (const HudCallMenu *menu : roles) {
    if (merged.role.empty()) {
      merged.role = menu->role;
      merged.station = menu->station;
      merged.station_name = menu->station_name;
      merged.revision = menu->revision;
    }
    if (merged.next_frequency_hz == 0) {
      merged.next_frequency_hz = menu->next_frequency_hz;
    }
    for (const HudCallItem &item : menu->items) {
      if (!item.readback) {
        add(item);
      }
    }
  }
  merged_.erase(std::remove_if(merged_.begin(), merged_.end(),
                               [frequency_hz](const HudCallMenu &held) {
                                 return held.frequency_hz == frequency_hz;
                               }),
                merged_.end());
  if (!roles.empty()) {
    merged_.push_back(std::move(merged));
  }
}

const HudCallMenu *HudCalls::on(std::uint64_t frequency_hz) const {
  if (frequency_hz == 0) {
    return nullptr;
  }
  for (const HudCallMenu &held : merged_) {
    if (held.frequency_hz == frequency_hz) {
      return &held;
    }
  }
  return nullptr;
}

std::string format_mhz(std::uint64_t frequency_hz) {
  const std::uint64_t khz = (frequency_hz + 500) / 1000;
  std::string mhz = std::to_string(khz / 1000) + ".";
  const std::string rest = std::to_string(1000 + khz % 1000);
  return mhz + rest.substr(1);
}

} // namespace zoal_atc::gui
