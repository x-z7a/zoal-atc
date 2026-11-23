#include "zoal_atc/tts/kokoro_synthesizer.hpp"

#include "zoal_atc/tts/voice_catalogue.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

bool separator(char c) {
  return c == ' ' || c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?';
}

// spoken_part is a reading without the spaces and punctuation around it, which
// are the pause at the seam rather than something to say.
std::string spoken_part(const std::string &text) {
  std::size_t first = 0;
  while (first < text.size() && separator(text[first])) {
    ++first;
  }
  std::size_t last = text.size();
  while (last > first && separator(text[last - 1])) {
    --last;
  }
  return text.substr(first, last - first);
}

std::string leading_separator(const std::string &text) {
  std::size_t end = 0;
  while (end < text.size() && separator(text[end])) {
    ++end;
  }
  return text.substr(0, end);
}

std::string trailing_separator(const std::string &text) {
  std::size_t begin = text.size();
  while (begin > 0 && separator(text[begin - 1])) {
    --begin;
  }
  return text.substr(begin);
}

// kSilence is the level below which a sample at a seam is silence: about
// forty decibels under full scale.
constexpr int kSilence = 330;
// kSeamMarginMs is the quiet kept either side of a seam, so the tail of a
// soft consonant is not cut off with the silence after it.
constexpr int kSeamMarginMs = 15;

// join appends part to out at a seam within one sentence: the silence each
// generation pads itself with is taken off and replaced by the pause the
// punctuation there asks for, so "via Echo Yankee" sounds like one phrase
// rather than three recordings.
void join(Audio &out, const Audio &part, const std::string &gap) {
  const int rate = out.sample_rate_hz > 0 ? out.sample_rate_hz : part.sample_rate_hz;
  const std::size_t margin = static_cast<std::size_t>(rate * kSeamMarginMs / 1000);
  std::size_t end = out.pcm.size();
  while (end > 0 && std::abs(out.pcm[end - 1]) < kSilence) {
    --end;
  }
  out.pcm.resize(std::min(out.pcm.size(), end + margin));
  std::size_t begin = 0;
  while (begin < part.pcm.size() && std::abs(part.pcm[begin]) < kSilence) {
    ++begin;
  }
  begin = begin > margin ? begin - margin : 0;
  int pause_ms = 40;
  if (gap.find_first_of(".;!?") != std::string::npos) {
    pause_ms = 300;
  } else if (gap.find_first_of(",:") != std::string::npos) {
    pause_ms = 160;
  }
  out.pcm.insert(out.pcm.end(), static_cast<std::size_t>(rate * pause_ms / 1000), 0);
  out.pcm.insert(out.pcm.end(), part.pcm.begin() + static_cast<std::ptrdiff_t>(begin), part.pcm.end());
}

}  // namespace

KokoroSynthesizer::KokoroSynthesizer(std::string model_dir, bool catalogue)
    : model_dir_(std::move(model_dir)),
      catalogue_(catalogue),
      speakers_(catalogue ? static_cast<int>(voice_catalogue().size()) : kEnglishSpeakers) {}

KokoroSynthesizer::~KokoroSynthesizer() {
  for (Engine *engine : {&american_, &british_, &spanish_, &french_}) {
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
  // The catalogue was prepared at startup, but an engine loads on the first
  // voice that needs it, which may be an hour later -- and a release unpacked
  // over a running simulator in between replaces model.onnx and leaves no
  // voices-zoal.bin. The load then fails, and every reply in that accent is
  // silent until X-Plane restarts. Preparing again is cheap when nothing
  // changed, because every write is skipped when the bytes already match.
  if (catalogue_ && !prepare_catalogue(model_dir_, nullptr)) {
    engine.load_failed = true;
    return nullptr;
  }
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
  // The aviation lexicon is English's words, and only an English engine reads it.
  const bool english = std::string(engine.lang).rfind("en", 0) == 0;
  if (english && file(aviation)) {
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

KokoroSynthesizer::Engine &KokoroSynthesizer::engine_for(const std::string &language,
                                                         const CatalogueVoice &voice) {
  if (language == "es") {
    return spanish_;
  }
  if (language == "fr") {
    return french_;
  }
  return voice.lexicon == Lexicon::British ? british_ : american_;
}

Audio KokoroSynthesizer::synthesize(const std::string &text, int speaker,
                                    const std::string &language) {
  std::lock_guard<std::mutex> lock(mutex_);
  Audio out;
  if (text.empty()) {
    return out;
  }
  const int sid = std::clamp(speaker, 0, speakers_ - 1);
  const CatalogueVoice &voice = voice_catalogue()[static_cast<std::size_t>(sid)];
  // A sentence in Spanish or French is read in that language, and the
  // spelling-alphabet words in it as English, in the same voice (readings).
  std::string pending_gap;
  for (const Reading &reading : readings(text, language)) {
    const std::string words = spoken_part(reading.text);
    if (words.empty()) {
      pending_gap += reading.text;
      continue;
    }
    Engine &engine = reading.english ? engine_for("en", voice) : engine_for(language, voice);
    Audio part = generate_locked(engine, words, sid, voice.speed);
    if (part.pcm.empty()) {
      return Audio{};
    }
    if (out.pcm.empty()) {
      out = std::move(part);
    } else {
      join(out, part, pending_gap + leading_separator(reading.text));
    }
    pending_gap = trailing_separator(reading.text);
  }
  return out;
}

Audio KokoroSynthesizer::generate_locked(Engine &engine, const std::string &text, int sid,
                                         float speed) {
  Audio out;
  const SherpaOnnxOfflineTts *tts = load_locked(engine);
  if (tts == nullptr) {
    return out;
  }
  SherpaOnnxGenerationConfig gen;
  std::memset(&gen, 0, sizeof(gen));
  gen.sid = sid;
  gen.speed = speed;
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
