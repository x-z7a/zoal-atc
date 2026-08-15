#ifndef ZOAL_ATC_PTT_NOOP_AUDIO_RECORDER_HPP
#define ZOAL_ATC_PTT_NOOP_AUDIO_RECORDER_HPP

#include "zoal_atc/ptt/audio_recorder.hpp"

namespace zoal_atc::ptt {

class NoopAudioRecorder final : public AudioRecorder {
public:
  RecorderStatus start() override;
  RecorderStatus stop(AudioBuffer &out) override;

private:
  bool active_ = false;
};

} // namespace zoal_atc::ptt

#endif // ZOAL_ATC_PTT_NOOP_AUDIO_RECORDER_HPP
