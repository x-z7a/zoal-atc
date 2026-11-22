#include "zoal_atc/gui/logbook.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace zoal_atc::gui {

namespace {

// kRemembered is how many lines are remembered to refuse a second copy of: a
// redelivery comes within seconds, never hundreds of lines later.
constexpr std::size_t kRemembered = 512;

std::string field(const std::string &json, const char *key) {
  return transport::json_string_field(json, key).value_or("");
}

// The time a line was said, "HH:MM:SSZ", from its RFC 3339 stamp.
std::string clock_of(const std::string &at) {
  const auto t = at.find('T');
  if (t == std::string::npos || at.size() < t + 9) {
    return at;
  }
  return at.substr(t + 1, 8) + "Z";
}

// The day a line was said, "YYYY-MM-DD", from its RFC 3339 stamp.
std::string date_of(const std::string &at) {
  if (at.size() >= 10 && at[4] == '-' && at[7] == '-') {
    return at.substr(0, 10);
  }
  return "undated";
}

} // namespace

std::string logbook_file_name(const std::string &callsign, const std::string &date) {
  std::string safe;
  for (const char c : callsign) {
    const auto u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '-' || c == '_') {
      safe.push_back(static_cast<char>(std::toupper(u)));
    }
  }
  if (safe.empty()) {
    safe = "flight";
  }
  return safe + "-" + date + ".log";
}

Logbook::Logbook(std::string root) : root_(std::move(root)) {}

void Logbook::set_callsign(const std::string &callsign) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!callsign.empty()) {
    callsign_ = callsign;
  }
}

void Logbook::set_keep(std::size_t keep) {
  std::lock_guard<std::mutex> lock(mutex_);
  keep_ = keep;
  // A smaller number takes effect now, not at the next flight.
  std::error_code error;
  if (!std::filesystem::is_directory(root_, error)) {
    return;
  }
  prune(root_);
  for (const auto &entry : std::filesystem::directory_iterator(root_, error)) {
    if (entry.is_directory(error)) {
      prune(entry.path().string());
    }
  }
}

std::size_t Logbook::keep() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return keep_;
}

void Logbook::set_language(const std::string &language) {
  std::lock_guard<std::mutex> lock(mutex_);
  language_ = language.empty() ? "en" : language;
}

bool Logbook::record(const std::string &payload_json) {
  const std::string text = field(payload_json, "text");
  if (text.empty()) {
    return false;
  }
  const std::string kind = field(payload_json, "kind");
  const std::string at = field(payload_json, "at");
  const std::string english = field(payload_json, "english");
  const std::string spoken_by = field(payload_json, "spokenBy");
  const std::string station = field(payload_json, "station");
  const std::string addressed_to = field(payload_json, "addressedTo");
  const bool overheard =
      transport::json_bool_field(payload_json, "overheard").value_or(false);

  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = at + "|" + kind + "|" + spoken_by + "|" + text;
  if (written_.count(key) > 0) {
    return true;
  }
  written_.insert(key);
  written_order_.push_back(key);
  if (written_order_.size() > kRemembered) {
    written_.erase(written_order_.front());
    written_order_.pop_front();
  }

  // Who said it: the pilot by their callsign, a controller by its position.
  std::string speaker;
  if (kind == "pilot") {
    speaker = !spoken_by.empty() ? spoken_by : (!callsign_.empty() ? callsign_ : "PILOT");
  } else {
    speaker = !station.empty() ? station : (!spoken_by.empty() ? spoken_by : "ATC");
    if (overheard && !addressed_to.empty()) {
      speaker += " \xE2\x86\x92 " + addressed_to;  // an arrow: who it was for
    }
  }
  const std::string prefix =
      "[" + clock_of(at) + "] " + (overheard ? "(overheard) " : "") + speaker + ": ";
  const std::string file = logbook_file_name(callsign_, date_of(at));

  bool ok = append(root_, file, prefix + (english.empty() ? text : english));
  if (language_ != "en") {
    ok = append(root_ + "/" + language_, file, prefix + text) && ok;
  }
  return ok;
}

bool Logbook::append(const std::string &dir, const std::string &file,
                     const std::string &line) {
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  const std::filesystem::path path = std::filesystem::path(dir) / file;
  const bool fresh = !std::filesystem::exists(path, error);
  {
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out) {
      return false;
    }
    out << line << '\n';
  }
  if (fresh) {
    prune(dir);
  }
  return true;
}

// prune keeps the newest keep_ logs in a folder: newest by when they were last
// written, so the flight being flown is never the one removed.
void Logbook::prune(const std::string &dir) const {
  if (keep_ == 0) {
    return;
  }
  std::error_code error;
  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> logs;
  for (const auto &entry : std::filesystem::directory_iterator(dir, error)) {
    if (entry.is_regular_file(error) && entry.path().extension() == ".log") {
      logs.emplace_back(entry.last_write_time(error), entry.path());
    }
  }
  if (logs.size() <= keep_) {
    return;
  }
  std::sort(logs.begin(), logs.end(),
            [](const auto &a, const auto &b) { return a.first > b.first; });
  for (std::size_t i = keep_; i < logs.size(); ++i) {
    std::filesystem::remove(logs[i].second, error);
  }
}

} // namespace zoal_atc::gui
