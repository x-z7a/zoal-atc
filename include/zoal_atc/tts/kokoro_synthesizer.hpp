#ifndef ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP
#define ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP

#include "zoal_atc/tts/synthesis.hpp"

#include <mutex>
#include <string>

struct SherpaOnnxOfflineTts;

namespace zoal_atc::tts {

// KokoroSynthesizer speaks with Kokoro v0.19 through sherpa-onnx, on the
// pilot's own machine. Eleven speakers, American and British, so a field's
// positions each sound like somebody different.
//
// The model is loaded on the synthesis thread rather than at plugin start, so
// X-Plane is not held up by it, and a model that will not load fails the
// transmission — which X-Plane's own voice then reads — rather than the plugin.
class KokoroSynthesizer final : public Synthesizer {
public:
  // kSpeakers is Kokoro v0.19's speaker count, known before the model loads
  // because the console's voice is mapped onto it as the reply arrives.
  static constexpr int kSpeakers = 11;
  // kDefaultSpeaker is the voice for a reply that names none: am_adam.
  static constexpr int kDefaultSpeaker = 5;

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
  bool load_locked();

  std::string model_dir_;
  std::mutex mutex_;
  const SherpaOnnxOfflineTts *tts_ = nullptr;
  bool load_failed_ = false;
};

}  // namespace zoal_atc::tts

#endif  // ZOAL_ATC_TTS_KOKORO_SYNTHESIZER_HPP
