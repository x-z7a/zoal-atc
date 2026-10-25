#ifndef ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP
#define ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP

#include "zoal_atc/tts/synthesis.hpp"

#include <mutex>
#include <string>

struct SherpaOnnxOfflineTts;

namespace zoal_atc::tts {

// KokoroSynthesizer speaks with Kokoro v1.0 through sherpa-onnx, on the
// pilot's own machine, in its English voices only: nineteen American and eight
// British, so the positions at a field and the centres around it each sound
// like somebody different.
//
// The console numbers voices from one, and voice n is speaker n-1: 1-20 are
// American (af_alloy … am_santa) and 21-28 British (bf_alice … bm_lewis). The
// console never asks for am_santa, the Santa Claus voice, nor for any of the
// model's other languages.
//
// A British voice is spoken with the British lexicon and espeak's own English,
// which is British ("en"; there is no "en-gb" voice), and
// an American one with the American: the voice is the timbre, and the lexicon
// is how "schedule" and "route" come out. Each is its own engine, loaded the
// first time a voice of that accent speaks, so a pilot flying in Canada never
// loads the British one.
//
// Loaded on the synthesis thread rather than at plugin start, so X-Plane is
// not held up by it, and a model that will not load fails the transmission
// rather than the plugin.
class KokoroSynthesizer final : public Synthesizer {
public:
  // kSpeakers is how many English voices there are, known before the model
  // loads because the console's voice is mapped onto it as the reply arrives.
  static constexpr int kSpeakers = 28;
  // kFirstBritish is the first British speaker; everything before it is
  // American.
  static constexpr int kFirstBritish = 20;
  // kDefaultSpeaker is the voice for a reply that names none: am_michael.
  static constexpr int kDefaultSpeaker = 16;

  explicit KokoroSynthesizer(std::string model_dir);
  ~KokoroSynthesizer() override;

  KokoroSynthesizer(const KokoroSynthesizer &) = delete;
  KokoroSynthesizer &operator=(const KokoroSynthesizer &) = delete;

  Audio synthesize(const std::string &text, int speaker) override;
  int speakers() const override { return kSpeakers; }
  void warm() override;

  // installed reports whether model_dir holds everything Kokoro needs.
  static bool installed(const std::string &model_dir);

private:
  struct Engine {
    const char *lexicon;
    const char *lang;
    const SherpaOnnxOfflineTts *tts = nullptr;
    bool load_failed = false;
  };

  const SherpaOnnxOfflineTts *load_locked(Engine &engine);

  std::string model_dir_;
  std::mutex mutex_;
  Engine american_{"lexicon-us-en.txt", "en-us"};
  Engine british_{"lexicon-gb-en.txt", "en"};
};

}  // namespace zoal_atc::tts

#endif  // ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP
