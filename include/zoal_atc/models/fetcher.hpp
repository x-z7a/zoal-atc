#ifndef ZOAL_ATC_MODELS_FETCHER_HPP
#define ZOAL_ATC_MODELS_FETCHER_HPP

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zoal_atc::models {

// Transfer is one download: an https URL into a file, resumed where an
// earlier attempt stopped.
struct Transfer {
  std::string url;
  std::filesystem::path destination;
};

// Fetcher downloads files. It decides nothing about whether they are right;
// the store checks every byte afterwards.
class Fetcher {
public:
  virtual ~Fetcher() = default;
  // fetch runs every transfer and returns once they have all been tried, or
  // `cancel` is raised. `tick` is called a few times a second while it waits.
  // False with `why` when something did not complete.
  virtual bool fetch(const std::vector<Transfer> &transfers, const std::filesystem::path &work_dir,
                     const std::atomic_bool &cancel, const std::function<void()> &tick,
                     std::string *why) = 0;
};

// The system's own curl, which every platform X-Plane 12 runs on carries —
// /usr/bin/curl on macOS, System32\curl.exe on Windows 10 and later, and the
// distribution's on Linux — with its own TLS and certificate store, which
// this plugin has neither of. One process for all the transfers of a model,
// so one connection carries them.
std::unique_ptr<Fetcher> make_curl_fetcher();

} // namespace zoal_atc::models

#endif // ZOAL_ATC_MODELS_FETCHER_HPP
