#include "zoal_atc/transport/async_websocket_ptt_sink.hpp"

#include "zoal_atc/transport/base64.hpp"
#include "zoal_atc/transport/bounded_queue.hpp"

#include <cstdint>
#include <sstream>
#include <string_view>
#include <utility>

namespace zoal_atc::transport {
namespace {

std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  constexpr char kHex[] = "0123456789abcdef";
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    switch (ch) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (ch < 0x20U) {
        escaped += "\\u00";
        escaped.push_back(kHex[(ch >> 4U) & 0x0FU]);
        escaped.push_back(kHex[ch & 0x0FU]);
      } else {
        escaped.push_back(static_cast<char>(ch));
      }
      break;
    }
  }
  return escaped;
}

} // namespace

AsyncWebSocketPttSink::AsyncWebSocketPttSink(
    TextMessageSink &sink, std::string session_id,
    SendFailureHandler on_send_failure)
    : sink_(sink), session_id_(std::move(session_id)),
      on_send_failure_(std::move(on_send_failure)), worker_([this] {
        run();
      }) {}

AsyncWebSocketPttSink::~AsyncWebSocketPttSink() { stop(); }

void AsyncWebSocketPttSink::publish(ptt::PttEvent event) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
      return;
    }
    QueuedMessage message;
    message.type = QueuedMessage::Type::PttEvent;
    message.event = std::move(event);
    enqueue_locked(std::move(message));
  }
  cv_.notify_one();
}

void AsyncWebSocketPttSink::publish_text(std::string payload) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
      return;
    }
    QueuedMessage message;
    message.type = QueuedMessage::Type::Text;
    message.text = std::move(payload);
    enqueue_locked(std::move(message));
  }
  cv_.notify_one();
}

void AsyncWebSocketPttSink::enqueue_locked(QueuedMessage message) {
  dropped_ += push_bounded(queue_, std::move(message), kMaxQueuedMessages);
}

std::uint64_t AsyncWebSocketPttSink::dropped() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_;
}

void AsyncWebSocketPttSink::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_one();
  transcription_cv_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
  if (transcription_worker_.joinable()) {
    transcription_worker_.join();
  }
}

void AsyncWebSocketPttSink::set_transcriber(stt::Transcriber *transcriber,
                                            const stt::PromptHolder *prompt) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (transcriber_ != nullptr || transcriber == nullptr) {
    return;
  }
  transcriber_ = transcriber;
  prompt_ = prompt;
  transcription_worker_ = std::thread([this] { run_transcription(); });
}

// transcribe_or_send hands a finished turn to the transcription thread, or sends
// its audio when there is no transcriber or the queue is already full.
void AsyncWebSocketPttSink::transcribe_or_send(const ptt::PttEvent &event) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transcriber_ != nullptr && !stopping_ &&
        transcriptions_.size() < kMaxQueuedTranscriptions) {
      transcriptions_.push_back(event);
      transcription_cv_.notify_one();
      return;
    }
  }
  send_audio_chunk(event);
  send_audio_end(event);
}

// run_transcription is the recogniser's own thread. It is separate from the
// socket worker because a turn takes the recogniser a fraction of a second to
// several seconds, and telemetry queued behind it would go stale.
void AsyncWebSocketPttSink::run_transcription() {
  {
    stt::Transcriber *transcriber = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      transcriber = transcriber_;
    }
    if (transcriber != nullptr) {
      transcriber->warm();
    }
  }
  for (;;) {
    ptt::PttEvent event;
    stt::Transcriber *transcriber = nullptr;
    const stt::PromptHolder *prompt = nullptr;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      transcription_cv_.wait(
          lock, [this] { return stopping_ || !transcriptions_.empty(); });
      if (transcriptions_.empty()) {
        return;
      }
      event = std::move(transcriptions_.front());
      transcriptions_.pop_front();
      transcriber = transcriber_;
      prompt = prompt_;
    }
    const std::string context = prompt != nullptr ? prompt->get() : std::string();
    const stt::Transcript transcript = transcriber->transcribe(
        event.audio.pcm16, event.audio.sample_rate_hz, context);
    if (transcript.ok) {
      send_text(stt::build_transcript_frame(session_id_, event.sequence,
                                            event.duration_ms, transcript));
    } else {
      // Heard nothing usable here, so the console hears the recording and
      // transcribes it itself.
      send_audio_chunk(event);
      send_audio_end(event);
    }
  }
}

void AsyncWebSocketPttSink::run() {
  for (;;) {
    QueuedMessage message;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) {
        return;
      }
      message = std::move(queue_.front());
      queue_.pop_front();
    }
    if (message.type == QueuedMessage::Type::Text) {
      send_text(message.text);
    } else {
      process_event(message.event);
    }
  }
}

void AsyncWebSocketPttSink::process_event(const ptt::PttEvent &event) {
  switch (event.type) {
  case ptt::PttEventType::Started:
    send_audio_start(event);
    break;
  case ptt::PttEventType::TransmissionReady:
    transcribe_or_send(event);
    break;
  case ptt::PttEventType::Cancelled:
    send_audio_cancel(event.sequence);
    break;
  case ptt::PttEventType::Ignored:
    break;
  case ptt::PttEventType::Error:
    if (event.sequence != 0) {
      send_audio_cancel(event.sequence);
    }
    break;
  }
}

void AsyncWebSocketPttSink::send_text(const std::string &payload) {
  const auto status = sink_.send_text(payload);
  if (!status.ok && on_send_failure_) {
    on_send_failure_(status.message);
  }
}

void AsyncWebSocketPttSink::send_audio_start(const ptt::PttEvent &event) {
  std::ostringstream message;
  message << "{\"type\":\"audio_start\",\"session_id\":\""
          << json_escape(session_id_) << "\",\"sequence\":" << event.sequence
          << ",\"sample_rate_hz\":16000,\"channels\":1,"
             "\"format\":\"pcm_s16le\"";
  if (event.frequency_hz != 0) {
    // The radio read when the button went down, which the console believes
    // over its own stamp from telemetry.
    message << ",\"transmit_com\":" << event.transmit_com
            << ",\"frequency_hz\":" << event.frequency_hz;
  }
  message << "}";
  send_text(message.str());
}

void AsyncWebSocketPttSink::send_audio_chunk(const ptt::PttEvent &event) {
  if (event.audio.pcm16.empty()) {
    return;
  }
  const auto *bytes =
      reinterpret_cast<const std::uint8_t *>(event.audio.pcm16.data());
  const std::size_t byte_count =
      event.audio.pcm16.size() * sizeof(std::int16_t);
  const std::string payload = base64_encode(bytes, byte_count);

  std::ostringstream message;
  message << "{\"type\":\"audio_chunk\",\"session_id\":\""
          << json_escape(session_id_) << "\",\"sequence\":" << event.sequence
          << ",\"pcm16_base64\":\"" << payload << "\"}";
  send_text(message.str());
}

void AsyncWebSocketPttSink::send_audio_end(const ptt::PttEvent &event) {
  std::ostringstream message;
  message << "{\"type\":\"audio_end\",\"session_id\":\""
          << json_escape(session_id_) << "\",\"sequence\":" << event.sequence
          << ",\"duration_ms\":" << event.duration_ms << "}";
  send_text(message.str());
}

void AsyncWebSocketPttSink::send_audio_cancel(std::uint64_t sequence) {
  std::ostringstream message;
  message << "{\"type\":\"audio_cancel\",\"session_id\":\""
          << json_escape(session_id_) << "\",\"sequence\":" << sequence
          << "}";
  send_text(message.str());
}

} // namespace zoal_atc::transport
