#ifndef ZOAL_ATC_MODELS_MODEL_STORE_HPP
#define ZOAL_ATC_MODELS_MODEL_STORE_HPP

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zoal_atc::models {

// ModelFile is one file of a model the plugin runs, as this build pins it:
// where it goes under models/, how big it is, its SHA-256 and where to fetch
// it. scripts/model-manifest.py writes the list from the tree the release
// ships, so a build expects exactly what its own release carries.
//
// An appendable file is one the plugin writes to after it is installed —
// prepare_catalogue gives Kokoro's model.onnx its speaker count — so only its
// first `size` bytes are the pinned ones.
struct ModelFile {
  const char *model;
  const char *path;
  std::uint64_t size;
  const char *sha256;
  const char *url;
  bool appendable;
};

// The two models: whisper's ("stt"), which hears the pilot, and Kokoro's
// voices ("tts"), which are the controllers.
inline constexpr const char *kSttModel = "stt";
inline constexpr const char *kTtsModel = "tts";

const std::vector<ModelFile> &manifest();
std::vector<ModelFile> files_of(const std::vector<ModelFile> &all, const std::string &model);

// model_id names a model's whole content in one checksum. A single file's is
// its SHA-256. A directory's is ensure-models.sh's tree_sum: each file's
// checksum and "./" path under the directory, a line each, in byte order,
// checksummed together — so the id of the voices is the .content-sha256 the
// release already ships inside their folder.
std::string model_id(const std::vector<ModelFile> &files);

// marker_path is where a model's verified id is kept: inside a directory
// model (its .content-sha256), and beside a single-file one.
std::filesystem::path marker_path(const std::filesystem::path &root,
                                  const std::vector<ModelFile> &files);

// quick_ready says a model needs no work without reading its bytes: its marker
// holds this build's id and every file is there at its pinned size. That is
// what startup asks on the main thread, so it only stats.
bool quick_ready(const std::filesystem::path &root, const std::vector<ModelFile> &files);

// file_holds checks one file's pinned bytes by its checksum.
bool file_holds(const std::filesystem::path &file, const ModelFile &pinned,
                const std::atomic_bool *cancel = nullptr);

// stale_files reads every file and returns the ones missing or not holding
// their pinned bytes. Slow — a second or two for both models — so it is never
// asked on the main thread.
std::vector<ModelFile> stale_files(const std::filesystem::path &root,
                                   const std::vector<ModelFile> &files,
                                   const std::atomic_bool *cancel = nullptr);

// partial_path is where a file is downloaded before it is checked: named for
// the content it should hold, so an interrupted download of one version is
// never resumed as another.
std::filesystem::path partial_path(const std::filesystem::path &root, const ModelFile &file);

// install_partial moves a downloaded file into place when it holds the pinned
// bytes, and removes it when it is as long as it should be and does not. A
// shorter one is left for the next attempt to resume. True when installed.
bool install_partial(const std::filesystem::path &root, const ModelFile &file,
                     const std::atomic_bool *cancel = nullptr);

// mark_verified writes the model's id to its marker.
bool mark_verified(const std::filesystem::path &root, const std::vector<ModelFile> &files);

} // namespace zoal_atc::models

#endif // ZOAL_ATC_MODELS_MODEL_STORE_HPP
