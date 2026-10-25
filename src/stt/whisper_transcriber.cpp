#include "zoal_atc/stt/whisper_transcriber.hpp"

#include <whisper.h>

#include <algorithm>
#include <sstream>
#include <thread>

namespace zoal_atc::stt {
namespace {

// The prompt the console's transcriber falls back to when it knows nothing
// about the flight. The console's stt_context replaces it as soon as it is sent.
constexpr const char *kAviationPrompt =
    "English air traffic control radio transmission.";

// without_annotations strips the bracketed asides whisper writes for audio it
// found no words in — "[BLANK_AUDIO]", "(music)" — which are the model
// describing the recording, not the pilot speaking.
std::string without_annotations(const std::string &text) {
  std::string kept;
  int depth = 0;
  for (const char ch : text) {
    if (ch == '[' || ch == '(') {
      ++depth;
    } else if (ch == ']' || ch == ')') {
      if (depth > 0) {
        --depth;
      }
    } else if (depth == 0) {
      kept.push_back(ch);
    }
  }
  std::istringstream words(kept);
  std::string word;
  std::string out;
  while (words >> word) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += word;
  }
  return out;
}

std::string trimmed(const char *text) {
  std::string value = text != nullptr ? text : "";
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

double clamp01(double p) { return std::min(1.0, std::max(0.0, p)); }

std::string base_name(const std::string &path) {
  const auto slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const auto dot = name.rfind(".bin");
  if (dot != std::string::npos && dot + 4 == name.size()) {
    name.resize(dot);
  }
  return name;
}

} // namespace

WhisperTranscriber::WhisperTranscriber(std::string model_path)
    : model_path_(std::move(model_path)), model_name_(base_name(model_path_)) {}

WhisperTranscriber::~WhisperTranscriber() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (context_ != nullptr) {
    whisper_free(context_);
    context_ = nullptr;
  }
}

bool WhisperTranscriber::load_locked(std::string &error) {
  if (context_ != nullptr) {
    return true;
  }
  if (load_failed_) {
    error = "whisper model failed to load: " + model_path_;
    return false;
  }
  whisper_context_params params = whisper_context_default_params();
  params.use_gpu = true;
  context_ = whisper_init_from_file_with_params(model_path_.c_str(), params);
  if (context_ == nullptr) {
    load_failed_ = true;
    error = "whisper model failed to load: " + model_path_;
    return false;
  }
  return true;
}

void WhisperTranscriber::warm() {
  // A second of near-silence: enough for whisper to load the model and build
  // every kernel a real turn will use.
  std::vector<std::int16_t> quiet(WHISPER_SAMPLE_RATE, 0);
  (void)transcribe(quiet, WHISPER_SAMPLE_RATE, std::string());
}

Transcript WhisperTranscriber::transcribe(const std::vector<std::int16_t> &pcm16,
                                          int sample_rate_hz,
                                          const std::string &prompt) {
  Transcript out;
  out.model = model_name_;
  if (sample_rate_hz != WHISPER_SAMPLE_RATE) {
    out.error = "whisper needs 16 kHz audio";
    return out;
  }
  if (pcm16.empty()) {
    out.ok = true;
    out.no_speech = true;
    return out;
  }
  std::vector<float> samples(pcm16.size());
  for (std::size_t i = 0; i < pcm16.size(); ++i) {
    samples[i] = static_cast<float>(pcm16[i]) / 32768.0F;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!load_locked(out.error)) {
    return out;
  }

  whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  // Leave a core for X-Plane, which is drawing frames while this runs.
  const unsigned cores = std::max(2U, std::thread::hardware_concurrency());
  params.n_threads = static_cast<int>(std::min(4U, cores - 1U));
  params.translate = false;
  params.no_context = true;
  params.token_timestamps = true;
  params.print_special = false;
  params.print_progress = false;
  params.print_realtime = false;
  params.print_timestamps = false;
  params.language = "en";
  const std::string context = prompt.empty() ? kAviationPrompt : prompt;
  params.initial_prompt = context.c_str();

  if (whisper_full(context_, params, samples.data(),
                   static_cast<int>(samples.size())) != 0) {
    out.error = "whisper failed to transcribe the turn";
    return out;
  }

  const whisper_token eot = whisper_token_eot(context_);
  std::string text;
  double total = 0;
  int count = 0;
  const int segments = whisper_full_n_segments(context_);
  for (int s = 0; s < segments; ++s) {
    const std::string said =
        without_annotations(trimmed(whisper_full_get_segment_text(context_, s)));
    if (said.empty()) {
      continue;
    }
    if (!text.empty()) {
      text.push_back(' ');
    }
    text += said;
    const int tokens = whisper_full_n_tokens(context_, s);
    for (int t = 0; t < tokens; ++t) {
      const whisper_token_data data = whisper_full_get_token_data(context_, s, t);
      if (data.id >= eot) {
        // Whisper's control tokens share the stream, spelt as text. They are
        // markers rather than anything the pilot said.
        continue;
      }
      const std::string spelling =
          trimmed(whisper_full_get_token_text(context_, s, t));
      if (spelling.empty()) {
        continue;
      }
      TranscriptWord word;
      word.text = spelling;
      // Token times are in hundredths of a second.
      word.start_ms = data.t0 > 0 ? static_cast<std::uint64_t>(data.t0) * 10U : 0U;
      word.end_ms = data.t1 > 0 ? static_cast<std::uint64_t>(data.t1) * 10U : 0U;
      if (word.end_ms < word.start_ms) {
        word.end_ms = word.start_ms;
      }
      word.confidence = clamp01(static_cast<double>(data.p));
      total += word.confidence;
      ++count;
      out.words.push_back(std::move(word));
    }
  }

  out.ok = true;
  if (text.empty()) {
    out.no_speech = true;
    out.words.clear();
    return out;
  }
  out.text = text;
  out.confidence = count > 0 ? total / count : 0;
  return out;
}

} // namespace zoal_atc::stt
