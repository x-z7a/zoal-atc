#ifndef ZOAL_ATC_PTT_AUDIO_RECORDER_HPP
#define ZOAL_ATC_PTT_AUDIO_RECORDER_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zoal_atc::ptt {

struct AudioBuffer {
  std::vector<std::int16_t> pcm16;
  int sample_rate_hz = 16000;
  int channels = 1;
};

struct RecorderStatus {
  bool ok = true;
  std::string message;

  static RecorderStatus success() { return {true, {}}; }
  static RecorderStatus failure(std::string reason) {
    return {false, std::move(reason)};
  }
};

class AudioRecorder {
public:
  virtual ~AudioRecorder() = default;

  virtual RecorderStatus start() = 0;
  virtual RecorderStatus stop(AudioBuffer &out) = 0;
};

} // namespace zoal_atc::ptt

#endif // ZOAL_ATC_PTT_AUDIO_RECORDER_HPP
