#include "zoal_atc/models/model_store.hpp"

#include "zoal_atc/models/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace zoal_atc::models {
namespace {

const std::vector<ModelFile> kManifest = {
#include "model_manifest.inc"
};

bool single_file(const std::vector<ModelFile> &files) {
  return files.size() == 1 && std::string(files.front().path).find('/') == std::string::npos;
}

// The directory a directory model's files are under, "kokoro-multi-lang-v1_0".
std::string top_of(const std::vector<ModelFile> &files) {
  const std::string path = files.front().path;
  return path.substr(0, path.find('/'));
}

std::uint64_t size_of(const std::filesystem::path &file) {
  std::error_code error;
  const auto size = std::filesystem::file_size(file, error);
  return error ? 0 : static_cast<std::uint64_t>(size);
}

bool is_file(const std::filesystem::path &file) {
  std::error_code error;
  return std::filesystem::is_regular_file(file, error);
}

bool size_fits(const std::filesystem::path &file, const ModelFile &pinned) {
  if (!is_file(file)) {
    return false;
  }
  const std::uint64_t size = size_of(file);
  return pinned.appendable ? size >= pinned.size : size == pinned.size;
}

std::string trim(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
    text.pop_back();
  }
  return text;
}

} // namespace

const std::vector<ModelFile> &manifest() { return kManifest; }

std::vector<ModelFile> files_of(const std::vector<ModelFile> &all, const std::string &model) {
  std::vector<ModelFile> out;
  std::copy_if(all.begin(), all.end(), std::back_inserter(out),
               [&](const ModelFile &file) { return model == file.model; });
  return out;
}

std::string stt_size_or_default(const std::string &size) {
  if (size == "medium" || size == "large") {
    return size;
  }
  return "small";
}

std::string stt_model_for(const std::string &size) {
  const std::string known = stt_size_or_default(size);
  if (known == "medium") {
    return kSttMediumModel;
  }
  if (known == "large") {
    return kSttLargeModel;
  }
  return kSttModel;
}

std::string model_id(const std::vector<ModelFile> &files) {
  if (files.empty()) {
    return {};
  }
  if (single_file(files)) {
    return files.front().sha256;
  }
  const std::size_t prefix = top_of(files).size() + 1;
  std::vector<std::pair<std::string, std::string>> lines;
  for (const ModelFile &file : files) {
    lines.emplace_back("./" + std::string(file.path).substr(prefix), file.sha256);
  }
  // ensure-models.sh sorts the paths (LC_ALL=C, byte order) before it writes
  // the listing, so the order is the paths'.
  std::sort(lines.begin(), lines.end());
  std::string listing;
  for (const auto &[path, sha] : lines) {
    listing += sha + "  " + path + "\n";
  }
  return sha256_hex(listing);
}

std::filesystem::path marker_path(const std::filesystem::path &root,
                                  const std::vector<ModelFile> &files) {
  if (single_file(files)) {
    return root / (std::string(files.front().path) + ".content-sha256");
  }
  return root / top_of(files) / ".content-sha256";
}

bool quick_ready(const std::filesystem::path &root, const std::vector<ModelFile> &files) {
  if (files.empty()) {
    return false;
  }
  std::ifstream in(marker_path(root, files), std::ios::binary);
  const std::string marker(std::istreambuf_iterator<char>(in), {});
  if (trim(marker) != model_id(files)) {
    return false;
  }
  return std::all_of(files.begin(), files.end(), [&](const ModelFile &file) {
    return size_fits(root / file.path, file);
  });
}

bool file_holds(const std::filesystem::path &file, const ModelFile &pinned,
                const std::atomic_bool *cancel) {
  if (!size_fits(file, pinned)) {
    return false;
  }
  const auto sum = sha256_file_prefix(file, pinned.size, cancel);
  return sum && *sum == pinned.sha256;
}

std::vector<ModelFile> stale_files(const std::filesystem::path &root,
                                   const std::vector<ModelFile> &files,
                                   const std::atomic_bool *cancel) {
  std::vector<ModelFile> stale;
  for (const ModelFile &file : files) {
    if (cancel != nullptr && cancel->load()) {
      break;
    }
    if (!file_holds(root / file.path, file, cancel)) {
      stale.push_back(file);
    }
  }
  return stale;
}

std::filesystem::path partial_path(const std::filesystem::path &root, const ModelFile &file) {
  return root / (std::string(file.path) + "." + std::string(file.sha256).substr(0, 12) + ".partial");
}

bool install_partial(const std::filesystem::path &root, const ModelFile &file,
                     const std::atomic_bool *cancel) {
  const auto partial = partial_path(root, file);
  if (!is_file(partial)) {
    return false;
  }
  const std::uint64_t size = size_of(partial);
  if (size < file.size) {
    return false;
  }
  std::error_code error;
  const auto sum = size == file.size ? sha256_file_prefix(partial, file.size, cancel)
                                     : std::nullopt;
  if (cancel != nullptr && cancel->load()) {
    return false;
  }
  if (!sum || *sum != file.sha256) {
    std::filesystem::remove(partial, error);
    return false;
  }
  const auto target = root / file.path;
  // Removed first: a rename over an existing file is not a replace everywhere.
  std::filesystem::remove(target, error);
  std::filesystem::rename(partial, target, error);
  return !error;
}

bool mark_verified(const std::filesystem::path &root, const std::vector<ModelFile> &files) {
  const auto marker = marker_path(root, files);
  std::error_code error;
  std::filesystem::create_directories(marker.parent_path(), error);
  std::ofstream out(marker, std::ios::binary | std::ios::trunc);
  out << model_id(files) << "\n";
  return static_cast<bool>(out);
}

} // namespace zoal_atc::models
