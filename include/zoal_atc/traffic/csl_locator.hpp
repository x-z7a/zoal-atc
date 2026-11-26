#ifndef ZOAL_ATC_TRAFFIC_CSL_LOCATOR_HPP
#define ZOAL_ATC_TRAFFIC_CSL_LOCATOR_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace zoal_atc::traffic {

// Where the CSL models the traffic is drawn with are: the folder the pilot
// chose, else the ones other traffic plugins installed, else none -- in which
// case the panel's Traffic tab asks. SDK-free; std::filesystem only.

// A folder holds CSL models when an xsb_aircraft.txt is in it or in a folder
// below it, at most `max_depth` levels down (0: only in the folder itself).
// False for a path that does not exist or is not a folder. Never throws.
bool holds_csl(const std::filesystem::path &folder, int max_depth = 6);

// The folders tried without asking, in order: ours, then the packages that
// IVAO, LiveTraffic, xPilot and X-Ivap install.
std::vector<std::filesystem::path>
default_csl_folders(const std::filesystem::path &plugin_root,
                    const std::filesystem::path &plugins_dir);

// A CSL folder found on this computer, for the panel to offer.
struct FoundCsl {
  std::filesystem::path path;
  // The path below plugins_dir with '/' separators, e.g.
  // "LiveTraffic/Resources/CSL".
  std::string label;
};

// Every folder named "CSL" (any case) at most three levels below plugins_dir
// (plugins/<X>/CSL or plugins/<X>/<Y>/CSL) that holds_csl. Sorted by label.
// A CSL folder is not searched inside for more. Empty when plugins_dir is
// missing. Never throws.
std::vector<FoundCsl> discover_csl_folders(const std::filesystem::path &plugins_dir);

// A path as a pilot types or pastes it: surrounding spaces, tabs, newlines
// and one pair of matching quotes ('...' or "...") removed; a leading "~" or
// "~/" replaced by `home` (left as it is when home is empty); "\ " (a shell-
// escaped space, as a Mac terminal pastes it) turned into " ".
std::string normalise_user_path(std::string text, const std::string &home);

// Which folder to load: `configured` when it is non-empty and holds_csl,
// else the first of `defaults` that holds_csl, else none.
struct CslChoice {
  std::filesystem::path path; // empty: none
  bool configured = false;    // the pilot's own choice
  // Set when `configured` was non-empty but held no models, so the log can
  // say why it was passed over: "<path> holds no xsb_aircraft.txt".
  std::string skipped;
};
CslChoice choose_csl(const std::string &configured,
                     const std::vector<std::filesystem::path> &defaults);

} // namespace zoal_atc::traffic

#endif // ZOAL_ATC_TRAFFIC_CSL_LOCATOR_HPP
