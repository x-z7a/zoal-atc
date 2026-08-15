#include "zoal_atc/ptt/noop_audio_recorder.hpp"

namespace zoal_atc::ptt {

RecorderStatus NoopAudioRecorder::start() {
  active_ = true;
  return RecorderStatus::success();
}

RecorderStatus NoopAudioRecorder::stop(AudioBuffer &out) {
  if (!active_) {
    return RecorderStatus::failure("noop recorder stopped while inactive");
  }
  active_ = false;
  out = AudioBuffer{};
  return RecorderStatus::success();
}

} // namespace zoal_atc::ptt
