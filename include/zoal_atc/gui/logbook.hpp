#ifndef ZOAL_ATC_GUI_LOGBOOK_HPP
#define ZOAL_ATC_GUI_LOGBOOK_HPP

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>

namespace zoal_atc::gui {

// Logbook keeps every line of a flight's radio log on the pilot's own disk,
// under the plugin folder:
//
//   logbook/<callsign>-<date>.log         in English, always
//   logbook/<language>/<callsign>-<date>.log
//                                          as the pilot heard it, where they
//                                          chose a language other than English
//
// A line is what the console pushes as comm_log: the pilot's own calls and
// every controller transmission on their frequency, the overheard ones marked.
// Its English is the line's "english" where the pilot heard another language,
// and its text where they did not; a line with no English beside it — the
// open lookup tier, a typed call — is written in the English file as said.
//
// Only the newest files are kept in each folder: keep, 20 unless the pilot
// changed it in Settings. Zero keeps every file. A file is a day of one
// callsign's flying, so a second flight the same day continues it.
class Logbook {
public:
  static constexpr std::size_t kDefaultKeep = 20;

  explicit Logbook(std::string root);

  // set_callsign is the flight's callsign as the simulator reports it, which
  // names the file. Until one is known the file is "flight-<date>.log".
  void set_callsign(const std::string &callsign);

  // set_keep is how many files each folder keeps; zero keeps all of them.
  void set_keep(std::size_t keep);
  std::size_t keep() const;

  // set_language is the language the pilot hears, an ISO 639-1 code: the
  // folder their own words are kept in.
  void set_language(const std::string &language);

  // record writes one comm_log payload. A line already written — the console
  // delivers at least once — is not written twice. Returns false when the
  // payload is not a line or could not be written.
  bool record(const std::string &payload_json);

  const std::string &root() const { return root_; }

private:
  bool append(const std::string &dir, const std::string &file, const std::string &line);
  void prune(const std::string &dir) const;

  std::string root_;
  mutable std::mutex mutex_;
  std::string callsign_;
  std::string language_ = "en";
  std::size_t keep_ = kDefaultKeep;
  std::unordered_set<std::string> written_;
  std::deque<std::string> written_order_;
};

// logbook_file_name is "<callsign>-<date>.log" with anything a file system
// would refuse taken out of the callsign; "flight" when there is none.
std::string logbook_file_name(const std::string &callsign, const std::string &date);

}  // namespace zoal_atc::gui

#endif  // ZOAL_ATC_GUI_LOGBOOK_HPP
