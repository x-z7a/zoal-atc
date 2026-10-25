#ifndef ZOAL_ATC_STT_WHISPER_TRANSCRIBER_HPP
#define ZOAL_ATC_STT_WHISPER_TRANSCRIBER_HPP

#include "zoal_atc/stt/transcriber.hpp"

#include <memory>
#include <mutex>
#include <string>

struct whisper_context;

namespace zoal_atc::stt {

// WhisperTranscriber transcribes push-to-talk in the cockpit with whisper.cpp,
// the same release and the same settings the console's own transcriber uses,
// so a turn is heard the same way whichever end transcribes it.
//
// The model is loaded on first use rather than at plugin start, so X-Plane is
// not held up loading it, and a model that will not load fails that turn —
// whose audio then goes to the console — rather than the plugin.
class WhisperTranscriber final : public Transcriber {
public:
  explicit WhisperTranscriber(std::string model_path);
  ~WhisperTranscriber() override;

  WhisperTranscriber(const WhisperTranscriber &) = delete;
  WhisperTranscriber &operator=(const WhisperTranscriber &) = delete;

  Transcript transcribe(const std::vector<std::int16_t> &pcm16,
                        int sample_rate_hz, const std::string &prompt) override;
  void warm() override;

private:
  bool load_locked(std::string &error);

  std::string model_path_;
  std::string model_name_;
  std::mutex mutex_;
  whisper_context *context_ = nullptr;
  bool load_failed_ = false;
};

} // namespace zoal_atc::stt

#endif // ZOAL_ATC_STT_WHISPER_TRANSCRIBER_HPP
