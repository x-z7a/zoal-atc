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
  // use_gpu is the pilot's (stt_device in zoal_atc.cfg). The GPU is the
  // default and is fastest, but on macOS it shares Metal with X-Plane's frames,
  // and a pilot who sees a stutter at the end of every call can move whisper
  // to the CPU instead (x-z7a/zoal-atc#66).
  WhisperTranscriber(std::string model_path, bool use_gpu);
  ~WhisperTranscriber() override;

  WhisperTranscriber(const WhisperTranscriber &) = delete;
  WhisperTranscriber &operator=(const WhisperTranscriber &) = delete;

  Transcript transcribe(const std::vector<std::int16_t> &pcm16,
                        int sample_rate_hz, const std::string &prompt,
                        const std::string &language) override;
  void warm() override;

  // set_use_gpu moves whisper between the GPU and the CPU. The model is let go
  // and loaded again on the next turn, on the new device; a turn being
  // transcribed finishes where it started.
  void set_use_gpu(bool use_gpu);

  // gpu_available is whether this build can put whisper on a GPU at all. Only
  // the macOS build has one (Metal); elsewhere whisper always runs on the CPU.
  static bool gpu_available();

private:
  bool load_locked(std::string &error);

  std::string model_path_;
  std::string model_name_;
  std::mutex mutex_;
  whisper_context *context_ = nullptr;
  bool load_failed_ = false;
  bool use_gpu_ = true;
};

} // namespace zoal_atc::stt

#endif // ZOAL_ATC_STT_WHISPER_TRANSCRIBER_HPP
