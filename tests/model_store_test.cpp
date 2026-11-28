#include "zoal_atc/models/model_provisioner.hpp"
#include "zoal_atc/models/model_store.hpp"
#include "zoal_atc/models/sha256.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;
using namespace zoal_atc::models;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void write(const fs::path &path, const std::string &bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << bytes;
}

std::string slurp(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

// The pins ensure-models.sh provisions the release with: the manifest the
// plugin downloads by has to name the same bytes.
std::string pin(const std::string &name) {
  const std::string script = slurp(fs::path(ZOAL_ATC_SOURCE_DIR) / "scripts" / "ensure-models.sh");
  std::smatch match;
  const std::regex line(name + "=([0-9a-f]{64})");
  require(std::regex_search(script, match, line), "ensure-models.sh pins " + name);
  return match[1];
}

// A fake server: what each URL serves, and how much of it before the
// connection drops.
class FakeFetcher final : public Fetcher {
public:
  std::map<std::string, std::string> served;
  std::size_t cut_after = std::string::npos;
  int calls = 0;

  bool fetch(const std::vector<Transfer> &transfers, const fs::path &, const std::atomic_bool &cancel,
             const std::function<void()> &tick, std::string *why) override {
    ++calls;
    bool ok = true;
    for (const Transfer &transfer : transfers) {
      if (cancel) {
        return false;
      }
      const auto found = served.find(transfer.url);
      if (found == served.end()) {
        *why = "404 " + transfer.url;
        ok = false;
        continue;
      }
      // Resumed: what is already there is kept, as curl --continue-at does.
      const std::string have = slurp(transfer.destination);
      std::string body = found->second.substr(std::min(have.size(), found->second.size()));
      if (cut_after != std::string::npos && body.size() > cut_after) {
        body.resize(cut_after);
        *why = "connection reset";
        ok = false;
      }
      fs::create_directories(transfer.destination.parent_path());
      std::ofstream out(transfer.destination, std::ios::binary | std::ios::app);
      out << body;
      if (tick) {
        tick();
      }
    }
    return ok;
  }
};

struct Fixture {
  fs::path root;
  std::vector<std::string> sums;
  std::vector<ModelFile> files;
  std::map<std::string, std::string> bytes;
};

// Two models: a single file, and a directory of two, one of them appendable.
Fixture fixture(const std::string &name) {
  Fixture f;
  f.root = fs::temp_directory_path() / ("zoal-atc-model-store-" + name);
  fs::remove_all(f.root);
  f.bytes = {{"https://h/one.bin", std::string(5000, 'a')},
             {"https://h/voices/model.onnx", "onnx-bytes"},
             {"https://h/voices/data/table.txt", "table"}};
  f.sums = {sha256_hex(f.bytes["https://h/one.bin"]), sha256_hex(f.bytes["https://h/voices/model.onnx"]),
            sha256_hex(f.bytes["https://h/voices/data/table.txt"])};
  f.files = {
      {"stt", "one.bin", 5000, f.sums[0].c_str(), "https://h/one.bin", false},
      {"tts", "voices/model.onnx", 10, f.sums[1].c_str(), "https://h/voices/model.onnx", true},
      {"tts", "voices/data/table.txt", 5, f.sums[2].c_str(), "https://h/voices/data/table.txt", false},
  };
  return f;
}

void wait_done(ModelProvisioner &provisioner) {
  for (int i = 0; i < 500 && !provisioner.done(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  require(provisioner.done(), "the provisioner finishes");
}

void test_sha256() {
  require(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of nothing");
  require(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 of abc");
  require(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256 across two blocks");
  require(sha256_hex(std::string(1000000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "sha256 of a million a");
}

void test_manifest_matches_the_release() {
  const auto stt = files_of(manifest(), kSttModel);
  const auto tts = files_of(manifest(), kTtsModel);
  require(stt.size() == 1, "whisper is one file");
  require(tts.size() > 300, "the voices are a tree");
  require(model_id(stt) == pin("stt_sum"), "whisper's id is the pin ensure-models.sh ships");
  require(model_id(tts) == pin("tts_sum"),
          "the voices' id is ensure-models.sh's tree_sum: regenerate with scripts/model-manifest.py");
  for (const ModelFile &file : manifest()) {
    const std::string url = file.url;
    require(url.rfind("https://huggingface.co/", 0) == 0, "https only: " + url);
    require(url.find_first_of(" \"\\") == std::string::npos, "a URL is percent-encoded: " + url);
    require(std::string(file.path).find("..") == std::string::npos, std::string("stays in models/: ") + file.path);
  }
  require(marker_path("/m", tts) == fs::path("/m") / "kokoro-multi-lang-v1_0" / ".content-sha256",
          "the voices' marker is the one ensure-models.sh writes");
}

// x-z7a/zoal-atc#66: a pilot may choose a larger whisper. Each size is one
// file in the manifest, and a size the plugin does not know is small, so a
// typo in the config costs the choice, never the recogniser.
void test_stt_sizes() {
  require(stt_size_or_default("medium") == "medium", "medium is a size");
  require(stt_size_or_default("large") == "large", "large is a size");
  require(stt_size_or_default("") == "small", "unset is small");
  require(stt_size_or_default("Large") == "small", "sizes are written in lower case");
  require(stt_model_for("small") == kSttModel, "small is the shipped model");
  require(stt_model_for("tiny") == kSttModel, "an unknown size is small");
  for (const std::string size : {"small", "medium", "large"}) {
    const auto files = files_of(manifest(), stt_model_for(size));
    require(files.size() == 1, "whisper " + size + " is one file");
    require(std::string(files[0].path).rfind("ggml-", 0) == 0, "and a ggml model: " + size);
    require(std::string(files[0].path).find('/') == std::string::npos,
            "beside the small one in models/: " + size);
    require(!files[0].appendable, "nothing writes to it: " + size);
  }
  require(files_of(manifest(), kSttMediumModel)[0].size > files_of(manifest(), kSttModel)[0].size,
          "medium is the larger download");
}

void test_store() {
  Fixture f = fixture("store");
  const auto stt = files_of(f.files, "stt");
  const auto tts = files_of(f.files, "tts");
  require(!quick_ready(f.root, stt), "nothing installed is not ready");
  require(stale_files(f.root, f.files).size() == 3, "every file is stale on an empty disk");

  write(f.root / "one.bin", f.bytes["https://h/one.bin"]);
  require(stale_files(f.root, stt).empty(), "the right bytes are not stale");
  require(!quick_ready(f.root, stt), "without a marker it is read, not assumed");
  require(mark_verified(f.root, stt), "marked");
  require(slurp(f.root / "one.bin.content-sha256") == f.sums[0] + "\n", "a file's marker is its checksum");
  require(quick_ready(f.root, stt), "a marked model at its sizes is ready at once");

  write(f.root / "one.bin", std::string(5000, 'b'));
  require(stale_files(f.root, stt).size() == 1, "other bytes are stale");
  write(f.root / "one.bin", std::string(4999, 'a'));
  require(!quick_ready(f.root, stt), "a short file is not ready, marker or not");

  // model.onnx after prepare_catalogue: the pinned bytes and a suffix.
  write(f.root / "voices/model.onnx", "onnx-bytes+speakers");
  write(f.root / "voices/data/table.txt", "table");
  require(stale_files(f.root, tts).empty(), "an appendable file is checked by its pinned prefix");
  write(f.root / "voices/data/table.txt", "table+");
  require(stale_files(f.root, tts).size() == 1, "an ordinary file may not grow");

  // A partial: short is kept for resuming, wrong is removed, right is moved in.
  const ModelFile &one = f.files[0];
  write(partial_path(f.root, one), "aaa");
  require(!install_partial(f.root, one) && fs::exists(partial_path(f.root, one)), "a short partial waits");
  write(partial_path(f.root, one), std::string(5000, 'c'));
  require(!install_partial(f.root, one) && !fs::exists(partial_path(f.root, one)), "a wrong partial goes");
  write(partial_path(f.root, one), f.bytes["https://h/one.bin"]);
  require(install_partial(f.root, one), "a right partial is installed");
  require(slurp(f.root / "one.bin") == f.bytes["https://h/one.bin"], "over the old file");
  require(partial_path(f.root, one).filename().string().find(f.sums[0].substr(0, 12)) != std::string::npos,
          "a partial is named for the content it should hold");
}

void test_provisioner_downloads_what_is_missing() {
  Fixture f = fixture("download");
  write(f.root / "voices/data/table.txt", "table"); // already right
  auto fetcher = std::make_unique<FakeFetcher>();
  fetcher->served = f.bytes;
  ModelProvisioner provisioner(f.root, f.files, {"stt", "tts"}, std::move(fetcher), {3, std::chrono::milliseconds(1)});
  provisioner.start();
  wait_done(provisioner);
  const auto ready = provisioner.take_ready();
  require(ready == std::vector<std::string>({"stt", "tts"}), "both models ready, in order");
  require(slurp(f.root / "one.bin") == f.bytes["https://h/one.bin"], "whisper downloaded");
  require(slurp(f.root / "voices/model.onnx") == "onnx-bytes", "the voices downloaded");
  require(quick_ready(f.root, files_of(f.files, "tts")), "and marked, so the next start is quick");
  require(provisioner.status("tts").state == ModelStatus::State::Ready, "status ready");
  require(!provisioner.take_messages().empty(), "what it did is said");
  require(provisioner.take_ready().empty(), "a model is handed over once");
}

void test_provisioner_resumes_after_a_drop() {
  Fixture f = fixture("resume");
  auto fetcher = std::make_unique<FakeFetcher>();
  FakeFetcher *server = fetcher.get();
  fetcher->served = f.bytes;
  fetcher->cut_after = 2000;
  ModelProvisioner provisioner(f.root, f.files, {"stt"}, std::move(fetcher), {3, std::chrono::milliseconds(1)});
  provisioner.start();
  wait_done(provisioner);
  require(provisioner.status("stt").state == ModelStatus::State::Ready, "5000 bytes in three drops of 2000");
  require(server->calls == 3, "one attempt per drop");
}

void test_provisioner_gives_up_with_a_reason() {
  Fixture f = fixture("fail");
  auto fetcher = std::make_unique<FakeFetcher>(); // serves nothing
  FakeFetcher *server = fetcher.get();
  ModelProvisioner provisioner(f.root, f.files, {"stt", "tts"}, std::move(fetcher), {2, std::chrono::milliseconds(1)});
  provisioner.start();
  wait_done(provisioner);
  const ModelStatus stt = provisioner.status("stt");
  require(stt.state == ModelStatus::State::Failed, "a model that will not come fails");
  require(stt.detail.find("404") != std::string::npos, "with the reason: " + stt.detail);
  require(server->calls == 4, "two attempts each");
  require(provisioner.take_ready().empty(), "nothing to load");
  require(progress_text(stt).rfind("failed: ", 0) == 0, "said as failed");
}

void test_provisioner_quick_path_downloads_nothing() {
  Fixture f = fixture("quick");
  for (const ModelFile &file : f.files) {
    write(f.root / file.path, f.bytes[file.url]);
  }
  mark_verified(f.root, files_of(f.files, "stt"));
  mark_verified(f.root, files_of(f.files, "tts"));
  auto fetcher = std::make_unique<FakeFetcher>();
  FakeFetcher *server = fetcher.get();
  ModelProvisioner provisioner(f.root, f.files, {"stt", "tts"}, std::move(fetcher));
  provisioner.start();
  wait_done(provisioner);
  require(provisioner.take_ready().size() == 2, "installed models are ready");
  require(server->calls == 0, "and nothing is fetched");
}

void test_progress_text() {
  ModelStatus status{ModelStatus::State::Downloading, 163000000, 326000000, {}};
  require(progress_text(status) == "downloading 50% of 326 MB", progress_text(status));
}

} // namespace

int main() {
  test_sha256();
  test_manifest_matches_the_release();
  test_stt_sizes();
  test_store();
  test_provisioner_downloads_what_is_missing();
  test_provisioner_resumes_after_a_drop();
  test_provisioner_gives_up_with_a_reason();
  test_provisioner_quick_path_downloads_nothing();
  test_progress_text();
  std::cout << "model store tests passed\n";
  return 0;
}
