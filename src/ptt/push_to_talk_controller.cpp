#include "zoal_atc/ptt/push_to_talk_controller.hpp"

#include <algorithm>
#include <utility>

namespace zoal_atc::ptt {

PushToTalkController::PushToTalkController(AudioRecorder &recorder,
                                           PttEventSink &sink,
                                           PushToTalkConfig config)
    : recorder_(recorder), sink_(sink), config_(config) {}

void PushToTalkController::command_begin(std::uint64_t now_ms) {
  if (state_ == PttState::Faulted) {
    publish_ignored(now_ms, PttIgnoreReason::Faulted);
    return;
  }
  if (state_ == PttState::Recording) {
    publish_ignored(now_ms, PttIgnoreReason::DuplicateBegin);
    return;
  }

  RecorderStatus status = recorder_.start();
  if (!status.ok) {
    state_ = PttState::Faulted;
    publish_error(now_ms, status.message);
    return;
  }

  state_ = PttState::Recording;
  active_sequence_ = next_sequence_++;
  started_at_ms_ = now_ms;

  PttEvent event;
  event.type = PttEventType::Started;
  event.sequence = active_sequence_;
  event.timestamp_ms = now_ms;
  sink_.publish(std::move(event));
}

void PushToTalkController::command_end(std::uint64_t now_ms) {
  if (state_ == PttState::Faulted) {
    publish_ignored(now_ms, PttIgnoreReason::Faulted);
    return;
  }
  if (state_ != PttState::Recording) {
    publish_ignored(now_ms, PttIgnoreReason::ReleaseWithoutBegin);
    return;
  }

  finish_recording(now_ms, PttEndReason::CommandReleased);
}

void PushToTalkController::tick(std::uint64_t now_ms) {
  if (state_ != PttState::Recording || config_.max_transmission_ms == 0) {
    return;
  }
  if (elapsed_ms(now_ms) >= config_.max_transmission_ms) {
    finish_recording(now_ms, PttEndReason::MaxDurationReached);
  }
}

void PushToTalkController::reset_fault() {
  if (state_ == PttState::Faulted) {
    state_ = PttState::Idle;
    active_sequence_ = 0;
    started_at_ms_ = 0;
  }
}

void PushToTalkController::finish_recording(std::uint64_t now_ms,
                                            PttEndReason reason) {
  AudioBuffer audio;
  RecorderStatus status = recorder_.stop(audio);
  if (!status.ok) {
    state_ = PttState::Faulted;
    publish_error(now_ms, status.message);
    return;
  }

  const std::uint64_t duration = elapsed_ms(now_ms);
  const std::uint64_t sequence = active_sequence_;

  state_ = PttState::Idle;
  active_sequence_ = 0;
  started_at_ms_ = 0;

  PttEvent event;
  event.sequence = sequence;
  event.timestamp_ms = now_ms;
  event.duration_ms = duration;
  event.end_reason = reason;
  event.audio = std::move(audio);

  if (duration < config_.min_transmission_ms) {
    event.type = PttEventType::Cancelled;
    event.cancel_reason = PttCancelReason::TooShort;
  } else {
    event.type = PttEventType::TransmissionReady;
  }

  sink_.publish(std::move(event));
}

void PushToTalkController::publish_ignored(std::uint64_t now_ms,
                                           PttIgnoreReason reason) {
  PttEvent event;
  event.type = PttEventType::Ignored;
  event.sequence = active_sequence_;
  event.timestamp_ms = now_ms;
  event.ignore_reason = reason;
  sink_.publish(std::move(event));
}

void PushToTalkController::publish_error(std::uint64_t now_ms,
                                         std::string message) {
  PttEvent event;
  event.type = PttEventType::Error;
  event.sequence = active_sequence_;
  event.timestamp_ms = now_ms;
  event.message = std::move(message);
  sink_.publish(std::move(event));
}

std::uint64_t PushToTalkController::elapsed_ms(std::uint64_t now_ms) const {
  if (now_ms < started_at_ms_) {
    return 0;
  }
  return now_ms - started_at_ms_;
}

const char *to_string(PttState state) {
  switch (state) {
  case PttState::Idle:
    return "idle";
  case PttState::Recording:
    return "recording";
  case PttState::Faulted:
    return "faulted";
  }
  return "unknown";
}

const char *to_string(PttEventType type) {
  switch (type) {
  case PttEventType::Started:
    return "started";
  case PttEventType::TransmissionReady:
    return "transmission_ready";
  case PttEventType::Cancelled:
    return "cancelled";
  case PttEventType::Ignored:
    return "ignored";
  case PttEventType::Error:
    return "error";
  }
  return "unknown";
}

const char *to_string(PttEndReason reason) {
  switch (reason) {
  case PttEndReason::CommandReleased:
    return "command_released";
  case PttEndReason::MaxDurationReached:
    return "max_duration_reached";
  }
  return "unknown";
}

const char *to_string(PttCancelReason reason) {
  switch (reason) {
  case PttCancelReason::TooShort:
    return "too_short";
  }
  return "unknown";
}

const char *to_string(PttIgnoreReason reason) {
  switch (reason) {
  case PttIgnoreReason::DuplicateBegin:
    return "duplicate_begin";
  case PttIgnoreReason::ReleaseWithoutBegin:
    return "release_without_begin";
  case PttIgnoreReason::Faulted:
    return "faulted";
  }
  return "unknown";
}

} // namespace zoal_atc::ptt
