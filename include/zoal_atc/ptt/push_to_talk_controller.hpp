#ifndef ZOAL_ATC_PTT_PUSH_TO_TALK_CONTROLLER_HPP
#define ZOAL_ATC_PTT_PUSH_TO_TALK_CONTROLLER_HPP

#include "zoal_atc/ptt/audio_recorder.hpp"
#include "zoal_atc/ptt/ptt_event.hpp"

#include <cstdint>

namespace zoal_atc::ptt {

enum class PttState {
  Idle,
  Recording,
  Faulted,
};

struct PushToTalkConfig {
  std::uint64_t min_transmission_ms = 250;
  std::uint64_t max_transmission_ms = 30000;
};

class PushToTalkController {
public:
  PushToTalkController(AudioRecorder &recorder, PttEventSink &sink,
                       PushToTalkConfig config = {});

  [[nodiscard]] PttState state() const { return state_; }
  [[nodiscard]] std::uint64_t active_sequence() const {
    return active_sequence_;
  }

  void command_begin(std::uint64_t now_ms);
  void command_end(std::uint64_t now_ms);
  void tick(std::uint64_t now_ms);
  void reset_fault();

private:
  void finish_recording(std::uint64_t now_ms, PttEndReason reason);
  void publish_ignored(std::uint64_t now_ms, PttIgnoreReason reason);
  void publish_error(std::uint64_t now_ms, std::string message);
  [[nodiscard]] std::uint64_t elapsed_ms(std::uint64_t now_ms) const;

  AudioRecorder &recorder_;
  PttEventSink &sink_;
  PushToTalkConfig config_;
  PttState state_ = PttState::Idle;
  std::uint64_t next_sequence_ = 1;
  std::uint64_t active_sequence_ = 0;
  std::uint64_t started_at_ms_ = 0;
};

const char *to_string(PttState state);

} // namespace zoal_atc::ptt

#endif // ZOAL_ATC_PTT_PUSH_TO_TALK_CONTROLLER_HPP
