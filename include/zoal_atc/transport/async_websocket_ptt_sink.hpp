#ifndef ZOAL_ATC_TRANSPORT_ASYNC_WEBSOCKET_PTT_SINK_HPP
#define ZOAL_ATC_TRANSPORT_ASYNC_WEBSOCKET_PTT_SINK_HPP

#include "zoal_atc/ptt/ptt_event.hpp"
#include "zoal_atc/transport/websocket_client.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace zoal_atc::transport {

class AsyncWebSocketPttSink final : public ptt::PttEventSink {
public:
  using SendFailureHandler = std::function<void(const std::string &)>;

  explicit AsyncWebSocketPttSink(TextMessageSink &sink,
                                 std::string session_id = "xplane-user",
                                 SendFailureHandler on_send_failure = {});
  ~AsyncWebSocketPttSink() override;

  AsyncWebSocketPttSink(const AsyncWebSocketPttSink &) = delete;
  AsyncWebSocketPttSink &operator=(const AsyncWebSocketPttSink &) = delete;

  void publish(ptt::PttEvent event) override;
  void publish_text(std::string payload);
  void stop();

private:
  struct QueuedMessage {
    enum class Type { PttEvent, Text };

    Type type = Type::PttEvent;
    ptt::PttEvent event;
    std::string text;
  };

  void run();
  void process_event(const ptt::PttEvent &event);
  void send_text(const std::string &payload);
  void send_audio_start(std::uint64_t sequence);
  void send_audio_chunk(const ptt::PttEvent &event);
  void send_audio_end(const ptt::PttEvent &event);
  void send_audio_cancel(std::uint64_t sequence);

  TextMessageSink &sink_;
  std::string session_id_;
  SendFailureHandler on_send_failure_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<QueuedMessage> queue_;
  bool stopping_ = false;
  std::thread worker_;
};

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_ASYNC_WEBSOCKET_PTT_SINK_HPP
