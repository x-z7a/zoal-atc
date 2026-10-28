#ifndef ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP
#define ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP

#include "zoal_atc/tts/synthesis.hpp"

#include <mutex>
#include <string>

struct SherpaOnnxOfflineTts;

namespace zoal_atc::tts {

// KokoroSynthesizer speaks with Kokoro v1.0 through sherpa-onnx, on the
// pilot's own machine, in the voice catalogue (voice_catalogue.hpp): the
// model's own speakers and the blends made from them, so the positions at a
// field and the centres around it each sound like somebody different, and a
// controller in Paris or Rome sounds like one.
//
// The console numbers voices from one, and voice n is catalogue entry n-1.
// With the catalogue prepared there are as many as it holds; without it — a
// plugin folder it could not write to — the model's stock table is used and
// the twenty-eight English speakers are all there are, as before.
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
  // kEnglishSpeakers is how many voices there are without the catalogue: the
  // stock table's American and British speakers.
  static constexpr int kEnglishSpeakers = 28;
  // kDefaultSpeaker is the voice for a reply that names none: am_michael.
  static constexpr int kDefaultSpeaker = 16;

  // catalogue says prepare_catalogue made model_dir ready for the catalogue.
  KokoroSynthesizer(std::string model_dir, bool catalogue);
  ~KokoroSynthesizer() override;

  KokoroSynthesizer(const KokoroSynthesizer &) = delete;
  KokoroSynthesizer &operator=(const KokoroSynthesizer &) = delete;

  Audio synthesize(const std::string &text, int speaker) override;
  int speakers() const override { return speakers_; }
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
  bool catalogue_;
  int speakers_;
  std::mutex mutex_;
  Engine american_{"lexicon-us-en.txt", "en-us"};
  Engine british_{"lexicon-gb-en.txt", "en"};
};

}  // namespace zoal_atc::tts

#endif  // ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP
