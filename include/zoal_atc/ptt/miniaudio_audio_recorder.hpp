#ifndef ZOAL_ATC_PTT_MINIAUDIO_AUDIO_RECORDER_HPP
#define ZOAL_ATC_PTT_MINIAUDIO_AUDIO_RECORDER_HPP

#include "zoal_atc/ptt/audio_recorder.hpp"

#include <memory>
#include <mutex>

namespace zoal_atc::ptt {

class MiniaudioAudioRecorder final : public AudioRecorder {
public:
  MiniaudioAudioRecorder();
  ~MiniaudioAudioRecorder() override;

  MiniaudioAudioRecorder(const MiniaudioAudioRecorder &) = delete;
  MiniaudioAudioRecorder &operator=(const MiniaudioAudioRecorder &) = delete;

  RecorderStatus start() override;
  RecorderStatus stop(AudioBuffer &out) override;
  void append_samples(const void *input, std::uint32_t frame_count);

private:
  struct DeviceState;

  std::unique_ptr<DeviceState> state_;
  std::mutex mutex_;
  AudioBuffer buffer_;
  bool recording_ = false;
};

} // namespace zoal_atc::ptt

#endif // ZOAL_ATC_PTT_MINIAUDIO_AUDIO_RECORDER_HPP
