#include "zoal_atc/models/model_provisioner.hpp"

#include <cstdio>
#include <system_error>
#include <utility>

namespace zoal_atc::models {
namespace {

std::uint64_t size_or_zero(const std::filesystem::path &file) {
  std::error_code error;
  const auto size = std::filesystem::file_size(file, error);
  return error ? 0 : static_cast<std::uint64_t>(size);
}

std::uint64_t total_size(const std::vector<ModelFile> &files) {
  std::uint64_t total = 0;
  for (const ModelFile &file : files) {
    total += file.size;
  }
  return total;
}

std::string megabytes(std::uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.0f MB", static_cast<double>(bytes) / 1e6);
  return text;
}

} // namespace

std::string progress_text(const ModelStatus &status) {
  switch (status.state) {
  case ModelStatus::State::Waiting:
    return "waiting";
  case ModelStatus::State::Checking:
    return "checking";
  case ModelStatus::State::Downloading: {
    const double fraction =
        status.need_bytes == 0 ? 0.0
                               : static_cast<double>(status.have_bytes) / static_cast<double>(status.need_bytes);
    char text[64];
    std::snprintf(text, sizeof(text), "downloading %d%% of ", static_cast<int>(fraction * 100.0));
    return text + megabytes(status.need_bytes);
  }
  case ModelStatus::State::Ready:
    return "ready";
  case ModelStatus::State::Failed:
    return "failed: " + status.detail;
  }
  return {};
}

ModelProvisioner::ModelProvisioner(std::filesystem::path root, std::vector<ModelFile> manifest,
                                   std::vector<std::string> models, std::unique_ptr<Fetcher> fetcher,
                                   Options options)
    : root_(std::move(root)), manifest_(std::move(manifest)), models_(std::move(models)),
      fetcher_(std::move(fetcher)), options_(options) {
  for (const std::string &model : models_) {
    status_[model] = ModelStatus{};
  }
}

ModelProvisioner::ModelProvisioner(std::filesystem::path root, std::vector<ModelFile> manifest,
                                   std::vector<std::string> models, std::unique_ptr<Fetcher> fetcher)
    : ModelProvisioner(std::move(root), std::move(manifest), std::move(models), std::move(fetcher),
                       Options{}) {}

ModelProvisioner::~ModelProvisioner() {
  cancel_ = true;
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ModelProvisioner::start() {
  if (!thread_.joinable()) {
    thread_ = std::thread([this] { run(); });
  }
}

ModelStatus ModelProvisioner::status(const std::string &model) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = status_.find(model);
  return found == status_.end() ? ModelStatus{} : found->second;
}

std::vector<std::string> ModelProvisioner::take_ready() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(ready_, {});
}

std::vector<std::string> ModelProvisioner::take_messages() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(messages_, {});
}

bool ModelProvisioner::done() const { return done_.load(); }

void ModelProvisioner::set(const std::string &model, ModelStatus status) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status.state == ModelStatus::State::Ready) {
    ready_.push_back(model);
  }
  status_[model] = std::move(status);
}

void ModelProvisioner::say(std::string message) {
  std::lock_guard<std::mutex> lock(mutex_);
  messages_.push_back(std::move(message));
}

bool ModelProvisioner::sleep_unless_cancelled(std::chrono::milliseconds delay) {
  std::unique_lock<std::mutex> lock(mutex_);
  wake_.wait_for(lock, delay, [this] { return cancel_.load(); });
  return !cancel_.load();
}

void ModelProvisioner::run() {
  for (const std::string &model : models_) {
    if (cancel_) {
      break;
    }
    provision(model);
  }
  done_ = true;
}

bool ModelProvisioner::provision(const std::string &model) {
  const std::vector<ModelFile> files = files_of(manifest_, model);
  if (files.empty()) {
    set(model, {ModelStatus::State::Failed, 0, 0, "this build pins no files for it"});
    return false;
  }
  if (quick_ready(root_, files)) {
    set(model, {ModelStatus::State::Ready, 0, 0, {}});
    return true;
  }

  set(model, {ModelStatus::State::Checking, 0, 0, {}});
  std::vector<ModelFile> stale = stale_files(root_, files, &cancel_);
  if (cancel_) {
    return false;
  }
  const std::uint64_t need = total_size(stale);
  if (!stale.empty()) {
    say(model + ": " + std::to_string(stale.size()) + " file(s) missing or out of date, downloading " +
        megabytes(need));
  }

  std::string why;
  for (int attempt = 1; !stale.empty() && attempt <= options_.attempts; ++attempt) {
    // A download an earlier session finished but did not move into place.
    std::vector<ModelFile> wanted;
    for (const ModelFile &file : stale) {
      if (!install_partial(root_, file, &cancel_)) {
        wanted.push_back(file);
      }
    }
    if (cancel_) {
      return false;
    }
    if (!wanted.empty()) {
      std::vector<Transfer> transfers;
      for (const ModelFile &file : wanted) {
        transfers.push_back({file.url, partial_path(root_, file)});
      }
      const std::uint64_t already = need - total_size(wanted);
      const auto tick = [&] {
        std::uint64_t have = already;
        for (const ModelFile &file : wanted) {
          have += std::min(size_or_zero(partial_path(root_, file)), file.size);
        }
        set(model, {ModelStatus::State::Downloading, have, need, {}});
      };
      tick();
      why.clear();
      fetcher_->fetch(transfers, root_, cancel_, tick, &why);
      if (cancel_) {
        return false;
      }
      std::vector<ModelFile> still;
      for (const ModelFile &file : wanted) {
        if (!install_partial(root_, file, &cancel_)) {
          still.push_back(file);
        }
      }
      stale = std::move(still);
    } else {
      stale.clear();
    }
    if (!stale.empty()) {
      if (why.empty()) {
        why = std::string(stale.front().path) + " did not download whole and right";
      }
      say(model + ": attempt " + std::to_string(attempt) + " left " + std::to_string(stale.size()) +
          " file(s): " + why);
      if (attempt < options_.attempts && !sleep_unless_cancelled(options_.retry_delay)) {
        return false;
      }
    }
  }

  if (!stale.empty()) {
    set(model, {ModelStatus::State::Failed, 0, need, why});
    return false;
  }
  if (!mark_verified(root_, files)) {
    say(model + ": cannot write " + marker_path(root_, files).string() +
        "; it is read again at the next start");
  }
  if (need > 0) {
    say(model + ": downloaded and checked");
  }
  set(model, {ModelStatus::State::Ready, need, need, {}});
  return true;
}

} // namespace zoal_atc::models
