#include "zoal_atc/transport/async_websocket_ptt_sink.hpp"

#include "zoal_atc/transport/base64.hpp"

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
    queue_.push_back(std::move(message));
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
    queue_.push_back(std::move(message));
  }
  cv_.notify_one();
}

void AsyncWebSocketPttSink::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
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
    send_audio_start(event.sequence);
    break;
  case ptt::PttEventType::TransmissionReady:
    send_audio_chunk(event);
    send_audio_end(event);
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

void AsyncWebSocketPttSink::send_audio_start(std::uint64_t sequence) {
  std::ostringstream message;
  message << "{\"type\":\"audio_start\",\"session_id\":\""
          << json_escape(session_id_) << "\",\"sequence\":" << sequence
          << ",\"sample_rate_hz\":16000,\"channels\":1,"
             "\"format\":\"pcm_s16le\"}";
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
