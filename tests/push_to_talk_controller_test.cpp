#include "zoal_atc/ptt/push_to_talk_controller.hpp"
#include "zoal_atc/transport/async_websocket_ptt_sink.hpp"
#include "zoal_atc/transport/base64.hpp"

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace zoal_atc::ptt;
using zoal_atc::transport::AsyncWebSocketPttSink;
using zoal_atc::transport::TextMessageSink;
using zoal_atc::transport::WebSocketStatus;

namespace {

class FakeRecorder final : public AudioRecorder {
public:
  RecorderStatus start() override {
    ++starts;
    active = true;
    if (fail_start) {
      return RecorderStatus::failure("start failed");
    }
    return RecorderStatus::success();
  }

  RecorderStatus stop(AudioBuffer &out) override {
    ++stops;
    active = false;
    if (fail_stop) {
      return RecorderStatus::failure("stop failed");
    }
    out.sample_rate_hz = 16000;
    out.channels = 1;
    out.pcm16 = samples;
    return RecorderStatus::success();
  }

  int starts = 0;
  int stops = 0;
  bool active = false;
  bool fail_start = false;
  bool fail_stop = false;
  std::vector<std::int16_t> samples{1, 2, 3, 4};
};

class CapturingSink final : public PttEventSink {
public:
  void publish(PttEvent event) override { events.push_back(std::move(event)); }

  std::vector<PttEvent> events;
};

class CapturingTextMessageSink final : public TextMessageSink {
public:
  WebSocketStatus send_text(std::string_view payload) override {
    std::lock_guard<std::mutex> lock(mutex);
    messages.emplace_back(payload);
    return WebSocketStatus::success();
  }

  std::vector<std::string> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return messages;
  }

private:
  mutable std::mutex mutex;
  std::vector<std::string> messages;
};

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

PushToTalkConfig test_config() {
  PushToTalkConfig config;
  config.min_transmission_ms = 250;
  config.max_transmission_ms = 1000;
  return config;
}

void starts_and_finishes_transmission() {
  FakeRecorder recorder;
  CapturingSink sink;
  PushToTalkController controller(recorder, sink, test_config());

  controller.command_begin(1000);
  require(controller.state() == PttState::Recording, "PTT should record");
  require(recorder.starts == 1, "recorder should start once");
  require(sink.events.size() == 1, "start event should publish");
  require(sink.events[0].type == PttEventType::Started,
          "first event should be Started");

  controller.command_end(1400);
  require(controller.state() == PttState::Idle, "PTT should return to idle");
  require(recorder.stops == 1, "recorder should stop once");
  require(sink.events.size() == 2, "ready event should publish");
  require(sink.events[1].type == PttEventType::TransmissionReady,
          "second event should be TransmissionReady");
  require(sink.events[1].duration_ms == 400, "duration should be tracked");
  require(sink.events[1].audio.pcm16.size() == 4, "audio should be attached");
}

void duplicate_begin_is_ignored() {
  FakeRecorder recorder;
  CapturingSink sink;
  PushToTalkController controller(recorder, sink, test_config());

  controller.command_begin(1000);
  controller.command_begin(1010);

  require(controller.state() == PttState::Recording,
          "duplicate begin should keep recording");
  require(recorder.starts == 1, "duplicate begin should not restart capture");
  require(sink.events.size() == 2, "duplicate begin should publish ignored");
  require(sink.events[1].type == PttEventType::Ignored,
          "duplicate begin event should be ignored");
  require(sink.events[1].ignore_reason == PttIgnoreReason::DuplicateBegin,
          "duplicate begin reason should be preserved");
}

void release_without_begin_is_ignored() {
  FakeRecorder recorder;
  CapturingSink sink;
  PushToTalkController controller(recorder, sink, test_config());

  controller.command_end(1000);

  require(controller.state() == PttState::Idle,
          "stray release should keep controller idle");
  require(recorder.stops == 0, "stray release should not stop recorder");
  require(sink.events.size() == 1, "stray release should publish ignored");
  require(sink.events[0].ignore_reason == PttIgnoreReason::ReleaseWithoutBegin,
          "stray release reason should be preserved");
}

void short_transmission_is_cancelled() {
  FakeRecorder recorder;
  CapturingSink sink;
  PushToTalkController controller(recorder, sink, test_config());

  controller.command_begin(1000);
  controller.command_end(1100);

  require(controller.state() == PttState::Idle,
          "short transmission should return to idle");
  require(sink.events.back().type == PttEventType::Cancelled,
          "short transmission should be cancelled");
  require(sink.events.back().cancel_reason == PttCancelReason::TooShort,
          "short transmission should use TooShort reason");
}

void max_duration_auto_finalizes() {
  FakeRecorder recorder;
  CapturingSink sink;
  PushToTalkController controller(recorder, sink, test_config());

  controller.command_begin(1000);
  controller.tick(1999);
  require(controller.state() == PttState::Recording,
          "before max duration should still record");

  controller.tick(2000);
  require(controller.state() == PttState::Idle,
          "max duration should stop recording");
  require(sink.events.back().type == PttEventType::TransmissionReady,
          "max duration should produce a transmission");
  require(sink.events.back().end_reason == PttEndReason::MaxDurationReached,
          "max duration reason should be preserved");
}

void recorder_start_failure_faults_until_reset() {
  FakeRecorder recorder;
  recorder.fail_start = true;
  CapturingSink sink;
  PushToTalkController controller(recorder, sink, test_config());

  controller.command_begin(1000);
  require(controller.state() == PttState::Faulted,
          "start failure should fault controller");
  require(sink.events.back().type == PttEventType::Error,
          "start failure should publish error");

  controller.command_end(1100);
  require(sink.events.back().type == PttEventType::Ignored,
          "faulted release should be ignored");
  require(sink.events.back().ignore_reason == PttIgnoreReason::Faulted,
          "faulted reason should be preserved");

  recorder.fail_start = false;
  controller.reset_fault();
  controller.command_begin(1200);
  require(controller.state() == PttState::Recording,
          "reset should allow recording again");
}

void recorder_stop_failure_faults() {
  FakeRecorder recorder;
  recorder.fail_stop = true;
  CapturingSink sink;
  PushToTalkController controller(recorder, sink, test_config());

  controller.command_begin(1000);
  controller.command_end(1300);

  require(controller.state() == PttState::Faulted,
          "stop failure should fault controller");
  require(sink.events.back().type == PttEventType::Error,
          "stop failure should publish error");
}

void base64_encodes_binary_audio_payloads() {
  const std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const std::string encoded = zoal_atc::transport::base64_encode(data, 4);
  require(encoded == "AQIDBA==", "base64 should encode binary audio");
}

void async_websocket_sink_sends_bounded_audio_turn() {
  CapturingTextMessageSink transport;
  AsyncWebSocketPttSink sink(transport, "unit-test");

  PttEvent started;
  started.type = PttEventType::Started;
  started.sequence = 42;
  sink.publish(std::move(started));

  PttEvent ready;
  ready.type = PttEventType::TransmissionReady;
  ready.sequence = 42;
  ready.duration_ms = 600;
  ready.audio.sample_rate_hz = 16000;
  ready.audio.channels = 1;
  ready.audio.pcm16 = {1, 2};
  sink.publish(std::move(ready));

  sink.stop();

  const auto messages = transport.snapshot();
  require(messages.size() == 3, "async sink should send start, chunk, end");
  require(messages[0].find("\"type\":\"audio_start\"") != std::string::npos,
          "first async message should start audio");
  require(messages[1].find("\"type\":\"audio_chunk\"") != std::string::npos,
          "second async message should contain audio chunk");
  require(messages[1].find("\"pcm16_base64\":\"AQACAA==\"") !=
              std::string::npos,
          "audio chunk should contain little-endian pcm16 base64");
  require(messages[2].find("\"type\":\"audio_end\"") != std::string::npos,
          "third async message should end audio");
  require(messages[2].find("\"duration_ms\":600") != std::string::npos,
          "audio end should include duration");
}

void async_websocket_sink_sends_raw_text() {
  CapturingTextMessageSink transport;
  AsyncWebSocketPttSink sink(transport, "unit-test");

  sink.publish_text("{\"type\":\"speak_ack\"}");
  sink.stop();

  const auto messages = transport.snapshot();
  require(messages.size() == 1, "async sink should send raw text");
  require(messages[0] == "{\"type\":\"speak_ack\"}",
          "raw text should be forwarded unchanged");
}

} // namespace

int main() {
  starts_and_finishes_transmission();
  duplicate_begin_is_ignored();
  release_without_begin_is_ignored();
  short_transmission_is_cancelled();
  max_duration_auto_finalizes();
  recorder_start_failure_faults_until_reset();
  recorder_stop_failure_faults();
  base64_encodes_binary_audio_payloads();
  async_websocket_sink_sends_bounded_audio_turn();
  async_websocket_sink_sends_raw_text();

  std::cout << "zoal_atc_tests: all tests passed\n";
  return 0;
}
