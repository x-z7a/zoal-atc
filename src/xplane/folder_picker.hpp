#ifndef ZOAL_ATC_XPLANE_FOLDER_PICKER_HPP
#define ZOAL_ATC_XPLANE_FOLDER_PICKER_HPP

#include <memory>
#include <optional>
#include <string>

namespace zoal_atc::xplane {

// The system's own "choose a folder" dialog, which X-Plane's SDK does not
// offer: osascript on a Mac, IFileOpenDialog on Windows, zenity or kdialog on
// Linux. It runs on a thread of its own, so the sim keeps flying while the
// pilot browses; the flight loop collects the answer with take().
class FolderPicker {
public:
  struct Result {
    std::string path;  // empty: cancelled, or see error
    std::string error; // no dialog could be shown
  };

  FolderPicker();
  ~FolderPicker();

  // Opens the dialog. False when one is already open.
  bool open(const std::string &prompt);
  bool busy() const;
  // The answer, once, after the dialog closes.
  std::optional<Result> take();

private:
  struct State;
  // Shared with the dialog's thread, which may outlive this picker (the
  // plugin unloaded with the dialog still up).
  std::shared_ptr<State> state_;
};

} // namespace zoal_atc::xplane

#endif
