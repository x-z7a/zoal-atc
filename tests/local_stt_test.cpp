#include "zoal_atc/stt/transcriber.hpp"
#include "zoal_atc/transport/async_websocket_ptt_sink.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace zoal_atc;
using transport::AsyncWebSocketPttSink;
using transport::TextMessageSink;
using transport::WebSocketStatus;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

class Capture final : public TextMessageSink {
public:
  WebSocketStatus send_text(std::string_view payload) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.emplace_back(payload);
    return WebSocketStatus::success();
  }
  std::vector<std::string> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
  }

private:
  mutable std::mutex mutex_;
  std::vector<std::string> messages_;
};

// FakeTranscriber hears one fixed sentence, or fails, and remembers the prompt
// it was handed.
class FakeTranscriber final : public stt::Transcriber {
public:
  stt::Transcript transcribe(const std::vector<std::int16_t> &, int,
                             const std::string &prompt) override {
    std::lock_guard<std::mutex> lock(mutex_);
    last_prompt_ = prompt;
    stt::Transcript out;
    if (fail) {
      out.error = "model not loaded";
      return out;
    }
    out.ok = true;
    out.text = "LaGuardia clearance, Delta 1616, \"IFR\" to DCA";
    out.confidence = 0.9;
    out.model = "ggml-small.en-q5_1";
    out.words.push_back({"clearance", 0, 400, 0.9});
    return out;
  }
  std::string last_prompt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_prompt_;
  }
  bool fail = false;

private:
  mutable std::mutex mutex_;
  std::string last_prompt_;
};

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

// wait_for polls until the sink has sent at least n messages.
std::vector<std::string> wait_for(const Capture &capture, std::size_t n) {
  for (int i = 0; i < 200; ++i) {
    auto sent = capture.snapshot();
    if (sent.size() >= n) {
      return sent;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return capture.snapshot();
}

ptt::PttEvent started(std::uint64_t sequence) {
  ptt::PttEvent event;
  event.type = ptt::PttEventType::Started;
  event.sequence = sequence;
  event.transmit_com = 1;
  event.frequency_hz = 121875000;
  return event;
}

ptt::PttEvent ready(std::uint64_t sequence) {
  ptt::PttEvent event;
  event.type = ptt::PttEventType::TransmissionReady;
  event.sequence = sequence;
  event.duration_ms = 2400;
  event.audio.pcm16 = {1, 2, 3, 4};
  return event;
}

// A turn transcribed in the cockpit is sent as words, not audio, with the
// console's prompt, and the radio read at key-down goes with its audio_start.
void transcribed_turn_is_sent_as_words() {
  Capture capture;
  FakeTranscriber transcriber;
  stt::PromptHolder prompt;
  prompt.set("Delta sixteen sixteen, DAL1616.");
  {
    AsyncWebSocketPttSink sink(capture, "session-1");
    sink.set_transcriber(&transcriber, &prompt);
    sink.publish(started(3));
    sink.publish(ready(3));
    const auto sent = wait_for(capture, 2);
    require(sent.size() == 2, "audio_start and transcript, nothing else");
    require(contains(sent[0], "\"type\":\"audio_start\""), "audio_start first");
    require(contains(sent[0], "\"transmit_com\":1,\"frequency_hz\":121875000"),
            "the keyed radio rides on audio_start");
    require(contains(sent[1], "{\"type\":\"transcript\""), "then the transcript");
    require(contains(sent[1], "\"sequence\":3,\"duration_ms\":2400"),
            "closing the same turn");
    require(!contains(sent[1], "pcm16_base64"), "no audio leaves the cockpit");
    require(contains(sent[1], "\\\"IFR\\\""), "free text is escaped");
    const auto text_at = sent[1].find("\"text\":\"LaGuardia");
    require(text_at != std::string::npos &&
                sent[1].find("\"model\"") < text_at,
            "the free text is the last field");
  }
  require(transcriber.last_prompt() == "Delta sixteen sixteen, DAL1616.",
          "the console's prompt is used");
}

// A transcriber that cannot produce a transcript costs the turn nothing: its
// audio goes to the console, as it always did.
void failed_transcription_sends_the_audio() {
  Capture capture;
  FakeTranscriber transcriber;
  transcriber.fail = true;
  stt::PromptHolder prompt;
  {
    AsyncWebSocketPttSink sink(capture, "session-1");
    sink.set_transcriber(&transcriber, &prompt);
    sink.publish(started(4));
    sink.publish(ready(4));
    const auto sent = wait_for(capture, 3);
    require(sent.size() == 3, "audio_start, chunk and end");
    require(contains(sent[1], "\"type\":\"audio_chunk\""), "the audio is sent");
    require(contains(sent[2], "\"type\":\"audio_end\""), "and the turn closed");
  }
}

// Without a transcriber nothing changes, and a start with no radio read sends
// the shipped audio_start exactly.
void without_a_transcriber_the_audio_goes_as_before() {
  Capture capture;
  {
    AsyncWebSocketPttSink sink(capture, "session-1");
    ptt::PttEvent start = started(5);
    start.frequency_hz = 0;
    sink.publish(start);
    sink.publish(ready(5));
    const auto sent = wait_for(capture, 3);
    require(sent.size() == 3, "audio_start, chunk and end");
    require(!contains(sent[0], "frequency_hz"), "no radio, no field");
  }
}

void silence_is_reported_as_silence() {
  stt::Transcript silent;
  silent.ok = true;
  silent.no_speech = true;
  silent.model = "m";
  silent.text = "[BLANK_AUDIO]";
  const auto frame = stt::build_transcript_frame("s", 1, 900, silent);
  require(contains(frame, "\"no_speech\":true"), "silence is said");
  require(contains(frame, "\"text\":\"\"}"), "and carries no words");
}

void the_console_prompt_is_read() {
  const auto prompt = stt::parse_stt_context(
      "{\"type\":\"stt_context\",\"prompt\":\"Air Canada two one, ACA21.\"}");
  require(prompt.has_value() && *prompt == "Air Canada two one, ACA21.",
          "stt_context carries the prompt");
  require(!stt::parse_stt_context("{\"type\":\"atc_reply\",\"text\":\"x\"}")
               .has_value(),
          "other frames are not");
}

} // namespace

int main() {
  transcribed_turn_is_sent_as_words();
  failed_transcription_sends_the_audio();
  without_a_transcriber_the_audio_goes_as_before();
  silence_is_reported_as_silence();
  the_console_prompt_is_read();
  std::cout << "local stt tests passed\n";
  return 0;
}
