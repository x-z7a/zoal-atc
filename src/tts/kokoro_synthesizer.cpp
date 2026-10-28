#include "zoal_atc/tts/kokoro_synthesizer.hpp"

#include "zoal_atc/tts/voice_catalogue.hpp"

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
// The pauses between phrases are short. How fast each voice talks is its own,
// in the catalogue.
constexpr float kSilenceScale = 0.2f;

bool file(const std::filesystem::path &path) {
  std::error_code missing;
  return std::filesystem::exists(path, missing);
}

}  // namespace

KokoroSynthesizer::KokoroSynthesizer(std::string model_dir, bool catalogue)
    : model_dir_(std::move(model_dir)),
      catalogue_(catalogue),
      speakers_(catalogue ? static_cast<int>(voice_catalogue().size()) : kEnglishSpeakers) {}

KokoroSynthesizer::~KokoroSynthesizer() {
  for (Engine *engine : {&american_, &british_}) {
    if (engine->tts != nullptr) {
      SherpaOnnxDestroyOfflineTts(engine->tts);
    }
  }
}

bool KokoroSynthesizer::installed(const std::string &model_dir) {
  const std::filesystem::path dir(model_dir);
  return file(dir / "model.onnx") && file(dir / "voices.bin") &&
         file(dir / "tokens.txt") && file(dir / "espeak-ng-data") &&
         file(dir / "lexicon-us-en.txt") && file(dir / "lexicon-gb-en.txt");
}

const SherpaOnnxOfflineTts *KokoroSynthesizer::load_locked(Engine &engine) {
  if (engine.tts != nullptr || engine.load_failed) {
    return engine.tts;
  }
  const std::filesystem::path dir(model_dir_);
  const std::string model = (dir / "model.onnx").string();
  const std::string voices =
      (dir / (catalogue_ ? kCatalogueVoicesFile : "voices.bin")).string();
  const std::string tokens = (dir / "tokens.txt").string();
  const std::string data = (dir / "espeak-ng-data").string();
  // Kokoro's own lexicon, and the controller's words it lacks where they were
  // written: a word in neither is left out of the speech.
  std::string lexicon = (dir / engine.lexicon).string();
  const std::filesystem::path aviation =
      dir / aviation_lexicon_file(engine.lang == std::string("en") ? Lexicon::British : Lexicon::American);
  if (file(aviation)) {
    lexicon += "," + aviation.string();
  }

  SherpaOnnxOfflineTtsConfig config;
  std::memset(&config, 0, sizeof(config));
  config.model.kokoro.model = model.c_str();
  config.model.kokoro.voices = voices.c_str();
  config.model.kokoro.tokens = tokens.c_str();
  config.model.kokoro.data_dir = data.c_str();
  config.model.kokoro.lexicon = lexicon.c_str();
  config.model.kokoro.lang = engine.lang;
  config.model.kokoro.length_scale = 1.0f;
  config.model.num_threads = kThreads;
  config.model.provider = "cpu";
  config.max_num_sentences = 1;
  engine.tts = SherpaOnnxCreateOfflineTts(&config);
  engine.load_failed = engine.tts == nullptr;
  return engine.tts;
}

void KokoroSynthesizer::warm() {
  std::lock_guard<std::mutex> lock(mutex_);
  const SherpaOnnxOfflineTts *tts = load_locked(american_);
  if (tts == nullptr) {
    return;
  }
  // The first generation pays for graph setup; say one word nobody hears.
  SherpaOnnxGenerationConfig gen;
  std::memset(&gen, 0, sizeof(gen));
  gen.sid = kDefaultSpeaker;
  gen.speed = voice_catalogue()[kDefaultSpeaker].speed;
  gen.silence_scale = kSilenceScale;
  if (const auto *audio = SherpaOnnxOfflineTtsGenerateWithConfig(
          tts, "Roger.", &gen, nullptr, nullptr)) {
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
  }
}

Audio KokoroSynthesizer::synthesize(const std::string &text, int speaker) {
  std::lock_guard<std::mutex> lock(mutex_);
  Audio out;
  if (text.empty()) {
    return out;
  }
  const int sid = std::clamp(speaker, 0, speakers_ - 1);
  const CatalogueVoice &voice = voice_catalogue()[static_cast<std::size_t>(sid)];
  const SherpaOnnxOfflineTts *tts =
      load_locked(voice.lexicon == Lexicon::British ? british_ : american_);
  if (tts == nullptr) {
    return out;
  }
  SherpaOnnxGenerationConfig gen;
  std::memset(&gen, 0, sizeof(gen));
  gen.sid = sid;
  gen.speed = voice.speed;
  gen.silence_scale = kSilenceScale;
  const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerateWithConfig(
      tts, text.c_str(), &gen, nullptr, nullptr);
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
