#ifndef ZOAL_ATC_MODELS_MODEL_PROVISIONER_HPP
#define ZOAL_ATC_MODELS_MODEL_PROVISIONER_HPP

#include "zoal_atc/models/fetcher.hpp"
#include "zoal_atc/models/model_store.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zoal_atc::models {

struct ModelStatus {
  enum class State { Waiting, Checking, Downloading, Ready, Failed };
  State state = State::Waiting;
  std::uint64_t have_bytes = 0;
  std::uint64_t need_bytes = 0;
  std::string detail;
};

// ModelProvisioner makes the models this build pins present under models/, off
// the main thread, so X-Plane goes on loading while it works. Each model is
// read in full and every file that is missing or holds other bytes than this
// build's is downloaded beside it, checked, and moved into place; a model is
// ready once all of its files are, and is handed to the main thread to load
// then. A model with a file that will not come is left failed with the
// reason, after a few attempts, and the rest of the plugin runs without it —
// exactly as an install without the model always has.
//
// It never deletes a file the manifest does not name: a model the plugin no
// longer uses stays on the pilot's disk until they remove it.
class ModelProvisioner {
public:
  struct Options {
    int attempts = 3;
    std::chrono::milliseconds retry_delay{5000};
  };

  // models are provisioned in the order given.
  ModelProvisioner(std::filesystem::path root, std::vector<ModelFile> manifest,
                   std::vector<std::string> models, std::unique_ptr<Fetcher> fetcher,
                   Options options);
  ModelProvisioner(std::filesystem::path root, std::vector<ModelFile> manifest,
                   std::vector<std::string> models, std::unique_ptr<Fetcher> fetcher);
  ~ModelProvisioner(); // cancels a download in progress and joins

  ModelProvisioner(const ModelProvisioner &) = delete;
  ModelProvisioner &operator=(const ModelProvisioner &) = delete;

  void start();

  ModelStatus status(const std::string &model) const;

  // take_ready returns the models that have become ready since it was last
  // called, for the main thread to load.
  std::vector<std::string> take_ready();
  // take_messages returns what happened, for the main thread to log: the
  // plugin's log is not written from other threads.
  std::vector<std::string> take_messages();

  // done is true once every model is ready or failed.
  bool done() const;

private:
  void run();
  bool provision(const std::string &model);
  void set(const std::string &model, ModelStatus status);
  void say(std::string message);
  bool sleep_unless_cancelled(std::chrono::milliseconds delay);

  std::filesystem::path root_;
  std::vector<ModelFile> manifest_;
  std::vector<std::string> models_;
  std::unique_ptr<Fetcher> fetcher_;
  Options options_;

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::map<std::string, ModelStatus> status_;
  std::vector<std::string> ready_;
  std::vector<std::string> messages_;
  std::atomic_bool cancel_{false};
  std::atomic_bool done_{false};
  std::thread thread_;
};

// progress_text is a status as the pilot reads it: "downloading 42% of 326 MB".
std::string progress_text(const ModelStatus &status);

} // namespace zoal_atc::models

#endif // ZOAL_ATC_MODELS_MODEL_PROVISIONER_HPP
