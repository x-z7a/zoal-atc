#include "zoal_atc/stt/transcriber.hpp"

#include "zoal_atc/transport/json_util.hpp"

#include <sstream>

namespace zoal_atc::stt {

using zoal_atc::transport::json_escape;
using zoal_atc::transport::json_number;
using zoal_atc::transport::json_string_field;

std::string build_transcript_frame(const std::string &session_id,
                                   std::uint64_t sequence,
                                   std::uint64_t duration_ms,
                                   const Transcript &transcript) {
  std::ostringstream out;
  out << "{\"type\":\"transcript\",\"session_id\":\"" << json_escape(session_id)
      << "\",\"sequence\":" << sequence << ",\"duration_ms\":" << duration_ms
      << ",\"confidence\":" << json_number(transcript.confidence);
  if (transcript.no_speech) {
    out << ",\"no_speech\":true";
  }
  out << ",\"model\":\"" << json_escape(transcript.model) << "\"";
  if (!transcript.no_speech && !transcript.words.empty()) {
    out << ",\"words\":[";
    for (std::size_t i = 0; i < transcript.words.size(); ++i) {
      const auto &word = transcript.words[i];
      if (i > 0) {
        out << ",";
      }
      out << "{\"text\":\"" << json_escape(word.text)
          << "\",\"start_ms\":" << word.start_ms << ",\"end_ms\":" << word.end_ms
          << ",\"confidence\":" << json_number(word.confidence) << "}";
    }
    out << "]";
  }
  out << ",\"text\":\""
      << json_escape(transcript.no_speech ? std::string() : transcript.text)
      << "\"}";
  return out.str();
}

std::optional<std::string> parse_stt_context(const std::string &payload) {
  const auto type = json_string_field(payload, "type");
  if (!type.has_value() || *type != "stt_context") {
    return std::nullopt;
  }
  return json_string_field(payload, "prompt").value_or(std::string());
}

} // namespace zoal_atc::stt
