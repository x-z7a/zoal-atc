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
  // The radio the pilot keyed, read when the button went down: 1 or 2, and the
  // frequency in Hz. Zero when it was not read, and the console then stamps the
  // freshest telemetry it holds instead.
  int transmit_com = 0;
  std::uint64_t frequency_hz = 0;
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
