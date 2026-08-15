#ifndef ZOAL_ATC_PTT_EVENT_HPP
#define ZOAL_ATC_PTT_EVENT_HPP

#include "zoal_atc/ptt/audio_recorder.hpp"

#include <cstdint>
#include <string>

namespace zoal_atc::ptt {

enum class PttEventType {
  Started,
  TransmissionReady,
  Cancelled,
  Ignored,
  Error,
};

enum class PttEndReason {
  CommandReleased,
  MaxDurationReached,
};

enum class PttCancelReason {
  TooShort,
};

enum class PttIgnoreReason {
  DuplicateBegin,
  ReleaseWithoutBegin,
  Faulted,
};

struct PttEvent {
  PttEventType type = PttEventType::Ignored;
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ms = 0;
  std::uint64_t duration_ms = 0;
  PttEndReason end_reason = PttEndReason::CommandReleased;
  PttCancelReason cancel_reason = PttCancelReason::TooShort;
  PttIgnoreReason ignore_reason = PttIgnoreReason::ReleaseWithoutBegin;
  AudioBuffer audio;
  std::string message;
};

class PttEventSink {
public:
  virtual ~PttEventSink() = default;

  virtual void publish(PttEvent event) = 0;
};

const char *to_string(PttEventType type);
const char *to_string(PttEndReason reason);
const char *to_string(PttCancelReason reason);
const char *to_string(PttIgnoreReason reason);

} // namespace zoal_atc::ptt

#endif // ZOAL_ATC_PTT_EVENT_HPP
