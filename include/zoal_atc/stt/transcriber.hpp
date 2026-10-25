#ifndef ZOAL_ATC_STT_TRANSCRIBER_HPP
#define ZOAL_ATC_STT_TRANSCRIBER_HPP

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zoal_atc::stt {

// TranscriptWord is one recognised word and when in the turn it was said.
struct TranscriptWord {
  std::string text;
  std::uint64_t start_ms = 0;
  std::uint64_t end_ms = 0;
  double confidence = 0;
};

// Transcript is what the recogniser heard in one push-to-talk turn: evidence,
// never interpretation. ok is false when it could not be produced at all, and
// the turn's audio is then sent to the console to transcribe instead.
struct Transcript {
  bool ok = false;
  std::string error;
  std::string text;
  double confidence = 0;
  bool no_speech = false;
  std::vector<TranscriptWord> words;
  std::string model;
};

// Transcriber turns a 16 kHz mono turn into words. It is called off the X-Plane
// main thread, one turn at a time.
class Transcriber {
public:
  virtual ~Transcriber() = default;

  virtual Transcript transcribe(const std::vector<std::int16_t> &pcm16,
                                int sample_rate_hz,
                                const std::string &prompt) = 0;

  // warm loads whatever the first transcription would otherwise wait for. It
  // is called once on the transcription thread before any turn, because the
  // first whisper call loads the model and compiles its GPU kernels — eight
  // seconds on an M-series Mac, against a fifth of a second for every call
  // after — and a pilot's first transmission should not pay for that.
  virtual void warm() {}
};

// PromptHolder is the prompt the console last sent in an stt_context: the
// callsign, the stations and places, the runways and route this flight is
// heard in the context of. Written on the socket thread, read on the
// transcription thread.
class PromptHolder {
public:
  void set(std::string prompt) {
    std::lock_guard<std::mutex> lock(mutex_);
    prompt_ = std::move(prompt);
  }
  std::string get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return prompt_;
  }

private:
  mutable std::mutex mutex_;
  std::string prompt_;
};

// build_transcript_frame is the transcript frame that closes a turn the
// aircraft transcribed itself, in place of audio_chunk and audio_end. Text is
// last, because it is the free text.
std::string build_transcript_frame(const std::string &session_id,
                                   std::uint64_t sequence,
                                   std::uint64_t duration_ms,
                                   const Transcript &transcript);

// parse_stt_context returns the prompt in an stt_context frame, or nothing for
// any other frame.
std::optional<std::string> parse_stt_context(const std::string &payload);

} // namespace zoal_atc::stt

#endif // ZOAL_ATC_STT_TRANSCRIBER_HPP
