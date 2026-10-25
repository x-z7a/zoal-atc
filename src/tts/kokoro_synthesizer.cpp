#include "zoal_atc/tts/kokoro_synthesizer.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace zoal_atc::tts {

namespace {

// Four threads: measured at 2.9 s for 15.5 s of speech on six, and X-Plane is
// using the rest of the machine.
constexpr int kThreads = 4;
// Controllers speak briskly, and the pauses between phrases are short.
constexpr float kSpeed = 1.1f;
constexpr float kSilenceScale = 0.2f;

bool file(const std::filesystem::path &path) {
  std::error_code missing;
  return std::filesystem::exists(path, missing);
}

}  // namespace

KokoroSynthesizer::KokoroSynthesizer(std::string model_dir)
    : model_dir_(std::move(model_dir)) {}

KokoroSynthesizer::~KokoroSynthesizer() {
  if (tts_ != nullptr) {
    SherpaOnnxDestroyOfflineTts(tts_);
  }
}

bool KokoroSynthesizer::installed(const std::string &model_dir) {
  const std::filesystem::path dir(model_dir);
  return file(dir / "model.onnx") && file(dir / "voices.bin") &&
         file(dir / "tokens.txt") && file(dir / "espeak-ng-data");
}

bool KokoroSynthesizer::load_locked() {
  if (tts_ != nullptr) {
    return true;
  }
  if (load_failed_) {
    return false;
  }
  const std::filesystem::path dir(model_dir_);
  const std::string model = (dir / "model.onnx").string();
  const std::string voices = (dir / "voices.bin").string();
  const std::string tokens = (dir / "tokens.txt").string();
  const std::string data = (dir / "espeak-ng-data").string();

  SherpaOnnxOfflineTtsConfig config;
  std::memset(&config, 0, sizeof(config));
  config.model.kokoro.model = model.c_str();
  config.model.kokoro.voices = voices.c_str();
  config.model.kokoro.tokens = tokens.c_str();
  config.model.kokoro.data_dir = data.c_str();
  config.model.kokoro.length_scale = 1.0f;
  config.model.num_threads = kThreads;
  config.model.provider = "cpu";
  config.max_num_sentences = 1;
  tts_ = SherpaOnnxCreateOfflineTts(&config);
  load_failed_ = tts_ == nullptr;
  return tts_ != nullptr;
}

void KokoroSynthesizer::warm() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!load_locked()) {
    return;
  }
  // The first generation pays for graph setup; say one word nobody hears.
  SherpaOnnxGenerationConfig gen;
  std::memset(&gen, 0, sizeof(gen));
  gen.sid = kDefaultSpeaker;
  gen.speed = kSpeed;
  gen.silence_scale = kSilenceScale;
  if (const auto *audio = SherpaOnnxOfflineTtsGenerateWithConfig(
          tts_, "Roger.", &gen, nullptr, nullptr)) {
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
  }
}

Audio KokoroSynthesizer::synthesize(const std::string &text, int speaker) {
  std::lock_guard<std::mutex> lock(mutex_);
  Audio out;
  if (text.empty() || !load_locked()) {
    return out;
  }
  const int count = SherpaOnnxOfflineTtsNumSpeakers(tts_);
  SherpaOnnxGenerationConfig gen;
  std::memset(&gen, 0, sizeof(gen));
  gen.sid = count > 0 ? std::clamp(speaker, 0, count - 1) : 0;
  gen.speed = kSpeed;
  gen.silence_scale = kSilenceScale;
  const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerateWithConfig(
      tts_, text.c_str(), &gen, nullptr, nullptr);
  if (audio == nullptr) {
    return out;
  }
  if (audio->n > 0 && audio->samples != nullptr) {
    out.sample_rate_hz = audio->sample_rate;
    out.pcm.resize(static_cast<std::size_t>(audio->n));
    for (std::size_t i = 0; i < out.pcm.size(); ++i) {
      const float s = std::clamp(audio->samples[i], -1.0f, 1.0f);
      out.pcm[i] = static_cast<std::int16_t>(std::lround(s * 32767.0f));
    }
  }
  SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
  return out;
}

}  // namespace zoal_atc::tts
