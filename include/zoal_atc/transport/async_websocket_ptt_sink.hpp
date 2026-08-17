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

  // How many unsent messages may wait for a console that is not taking them.
  //
  // The queue used to be unbounded, so an outage accumulated a backlog at the
  // telemetry sampling rate — and then delivered all of it at once when the
  // console came back, which is a flood of positions the aircraft has long since
  // left. Bounded, an outage costs a fixed amount of memory and reconnecting
  // resumes from roughly now.
  static constexpr std::size_t kMaxQueuedMessages = 256;

  void publish(ptt::PttEvent event) override;
  void publish_text(std::string payload);
  void stop();

  // How many messages have been dropped for want of queue space, so a caller
  // can say so rather than leaving the gap unexplained.
  [[nodiscard]] std::uint64_t dropped() const;

private:
  struct QueuedMessage {
    enum class Type { PttEvent, Text };

    Type type = Type::PttEvent;
    ptt::PttEvent event;
    std::string text;
  };

  void run();
  // Appends under the caller's lock, discarding the oldest message when the
  // queue is full. Oldest first, because for a position feed the stale end is
  // the worthless end.
  void enqueue_locked(QueuedMessage message);
  void process_event(const ptt::PttEvent &event);
  void send_text(const std::string &payload);
  void send_audio_start(std::uint64_t sequence);
  void send_audio_chunk(const ptt::PttEvent &event);
  void send_audio_end(const ptt::PttEvent &event);
  void send_audio_cancel(std::uint64_t sequence);

  TextMessageSink &sink_;
  std::string session_id_;
  SendFailureHandler on_send_failure_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<QueuedMessage> queue_;
  std::uint64_t dropped_ = 0;
  bool stopping_ = false;
  std::thread worker_;
};

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_ASYNC_WEBSOCKET_PTT_SINK_HPP
