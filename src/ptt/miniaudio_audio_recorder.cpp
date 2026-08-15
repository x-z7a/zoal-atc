#include "zoal_atc/ptt/miniaudio_audio_recorder.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <utility>

namespace zoal_atc::ptt {
namespace {

constexpr int kSampleRateHz = 16000;
constexpr int kChannels = 1;

void audio_callback(ma_device *device, void *, const void *input,
                    ma_uint32 frame_count) {
  if (device == nullptr || device->pUserData == nullptr || input == nullptr ||
      frame_count == 0) {
    return;
  }
  auto *recorder =
      static_cast<MiniaudioAudioRecorder *>(device->pUserData);
  recorder->append_samples(input, frame_count);
}

} // namespace

struct MiniaudioAudioRecorder::DeviceState {
  ma_device device{};
  bool initialized = false;
};

MiniaudioAudioRecorder::MiniaudioAudioRecorder()
    : state_(std::make_unique<DeviceState>()) {}

MiniaudioAudioRecorder::~MiniaudioAudioRecorder() {
  AudioBuffer ignored;
  (void)stop(ignored);
}

RecorderStatus MiniaudioAudioRecorder::start() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recording_) {
      return RecorderStatus::failure("audio recorder already recording");
    }

    buffer_ = AudioBuffer{};
    buffer_.sample_rate_hz = kSampleRateHz;
    buffer_.channels = kChannels;

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_s16;
    config.capture.channels = kChannels;
    config.sampleRate = kSampleRateHz;
    config.dataCallback = audio_callback;
    config.pUserData = this;

    const ma_result init_result =
        ma_device_init(nullptr, &config, &state_->device);
    if (init_result != MA_SUCCESS) {
      return RecorderStatus::failure("failed to initialize audio capture device");
    }
    state_->initialized = true;
    recording_ = true;
  }

  const ma_result start_result = ma_device_start(&state_->device);
  if (start_result != MA_SUCCESS) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      recording_ = false;
      state_->initialized = false;
    }
    ma_device_uninit(&state_->device);
    return RecorderStatus::failure("failed to start audio capture device");
  }

  return RecorderStatus::success();
}

RecorderStatus MiniaudioAudioRecorder::stop(AudioBuffer &out) {
  bool should_uninit = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    should_uninit = state_->initialized;
    state_->initialized = false;
    recording_ = false;
  }

  if (should_uninit) {
    ma_device_stop(&state_->device);
    ma_device_uninit(&state_->device);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (buffer_.pcm16.empty()) {
    out = AudioBuffer{};
    out.sample_rate_hz = kSampleRateHz;
    out.channels = kChannels;
    return RecorderStatus::success();
  }

  out = std::move(buffer_);
  buffer_ = AudioBuffer{};
  buffer_.sample_rate_hz = kSampleRateHz;
  buffer_.channels = kChannels;
  return RecorderStatus::success();
}

void MiniaudioAudioRecorder::append_samples(const void *input,
                                            std::uint32_t frame_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!recording_) {
    return;
  }
  const auto *samples = static_cast<const std::int16_t *>(input);
  const std::size_t sample_count =
      static_cast<std::size_t>(frame_count) * kChannels;
  buffer_.pcm16.insert(buffer_.pcm16.end(), samples, samples + sample_count);
}

} // namespace zoal_atc::ptt
