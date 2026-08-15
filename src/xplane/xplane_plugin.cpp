#include "zoal_atc/navdata/navdata_service.hpp"
#include "zoal_atc/gui/gui_bridge.hpp"
#include "zoal_atc/gui/skyscript_panel.hpp"
#include "zoal_atc/ptt/miniaudio_audio_recorder.hpp"
#include "zoal_atc/ptt/push_to_talk_controller.hpp"
#include "zoal_atc/telemetry/telemetry_frame.hpp"
#include "zoal_atc/transport/async_websocket_ptt_sink.hpp"
#include "zoal_atc/transport/endpoint_config.hpp"
#include "zoal_atc/transport/json_util.hpp"
#include "zoal_atc/transport/speech_text.hpp"
#include "zoal_atc/transport/websocket_client.hpp"

#include <XPLMDataAccess.h>
#include <XPLMNavigation.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <random>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using zoal_atc::navdata::NavdataPaths;
using zoal_atc::navdata::NavdataPublisher;
using zoal_atc::navdata::NavdataRequest;
using zoal_atc::ptt::MiniaudioAudioRecorder;
using zoal_atc::ptt::PttEvent;
using zoal_atc::ptt::PttEventSink;
using zoal_atc::ptt::PttEventType;
using zoal_atc::ptt::PushToTalkConfig;
using zoal_atc::ptt::PushToTalkController;
using zoal_atc::telemetry::TelemetryRateController;
using zoal_atc::telemetry::TelemetrySnapshot;
using zoal_atc::transport::AsyncWebSocketPttSink;
using zoal_atc::transport::WebSocketClient;
using zoal_atc::transport::WebSocketEndpoint;
using zoal_atc::transport::apply_config_text;
using zoal_atc::transport::host_header;
using zoal_atc::transport::parse_websocket_url;
using zoal_atc::transport::json_bool_field;
using zoal_atc::transport::json_string_field;
using zoal_atc::transport::json_uint_field;

namespace {

constexpr const char *kCommandName = "zoal_atc/ptt";
constexpr const char *kLogPrefix = "[zoal-atc] ";

// Phase 22 M1: what used to be a compile-time constant here made every plugin
// ever built claim to be the same aircraft, so two connected pilots merged into
// one flight. The console now attributes frames to the socket they arrived on
// and treats this field as advisory, but it still carries the installation's
// own identity so logs on both ends name the same aircraft.
//
// Empty until resolve_console_endpoint runs; the legacy value is the fallback
// so a frame emitted before then still reads as the single legacy flight.
std::string g_client_id;

std::string frame_session_id() {
  return g_client_id.empty() ? std::string("xplane-user") : g_client_id;
}
constexpr const char *kNativePathsFeature = "XPLM_USE_NATIVE_PATHS";

XPLMCommandRef g_ptt_command = nullptr;
XPLMFlightLoopID g_flight_loop = nullptr;
std::unique_ptr<MiniaudioAudioRecorder> g_recorder;
std::unique_ptr<WebSocketClient> g_websocket;
std::unique_ptr<AsyncWebSocketPttSink> g_transport_sink;
std::unique_ptr<PttEventSink> g_sink;
std::unique_ptr<PushToTalkController> g_ptt;
std::atomic_bool g_reply_receiver_stopping{false};
std::thread g_reply_receiver_thread;
std::mutex g_reply_mutex;

// The in-sim panel's proxy to the console. It outlives every connection and is
// safe to hand to Skyscript before the socket exists, because it is only ever a
// queue: the receiver thread feeds it, the flight loop drains it, and neither
// waits on the other.
zoal_atc::gui::GuiBridge g_gui_bridge;

// The console commands the sampling rate/subscription (P2-M4); the controller
// honors a commanded interval and falls back to the local policy when none is
// set (no command yet, or cleared on disconnect). Declared here (ahead of the
// reply-receiver loop that applies commands) and guarded by g_rate_mutex since
// that thread writes while the flight loop reads.
TelemetryRateController g_rate_controller;
std::mutex g_rate_mutex;

// One-off telemetry pull queries (P2-M4 T4): the reply-receiver thread parses a
// telemetry_query and enqueues it here; the flight loop drains and services it on
// the main thread (XPLM dataref reads must not happen off the main thread).
std::vector<zoal_atc::telemetry::TelemetryQuery> g_pending_queries;
std::mutex g_query_mutex;

// Radio tune commands are parsed on the receiver thread but applied on the
// flight loop thread because XPLM dataref writes are main-thread-only.
std::vector<zoal_atc::telemetry::RadioTuneCommand> g_pending_radio_tunes;
std::mutex g_radio_tune_mutex;

struct ATCReplyMessage {
  std::string session_id;
  std::string category;
  std::string text;
  bool speak = false;
  std::uint64_t generation = 0;
  std::uint64_t queued_at_ms = 0;
};

std::deque<ATCReplyMessage> g_reply_queue;

// XPLMSpeakString is fire-and-forget: it returns immediately and exposes no
// completion callback. Track a conservative estimate of the previous utterance
// so the delivery log can distinguish "API invoked" from "probably heard" and
// flag calls that may have collided in X-Plane's asynchronous speech path.
struct SpeechCallState {
  std::uint64_t generation = 0;
  std::uint64_t called_at_ms = 0;
  std::uint64_t estimated_end_ms = 0;
};

struct SpeechDeliveryInfo {
  bool api_called = false;
  bool possible_overlap = false;
  std::string ack_basis = "api_not_invoked";
  std::uint64_t plugin_queue_ms = 0;
  std::uint64_t estimated_duration_ms = 0;
  std::uint64_t speech_elapsed_ms = 0;
  std::uint64_t previous_generation = 0;
  std::uint64_t previous_call_age_ms = 0;
  std::uint64_t previous_estimated_remaining_ms = 0;
};

struct ActiveSpeech {
  ATCReplyMessage reply;
  SpeechDeliveryInfo delivery;
  std::uint64_t started_at_ms = 0;
  std::uint64_t acknowledge_at_ms = 0;
};

SpeechCallState g_last_speech_call;
std::optional<ActiveSpeech> g_active_speech;

// Defined after the navdata worker below; called from the receiver thread.
void enqueue_navdata_request(NavdataRequest request);

std::uint64_t now_ms() {
  return static_cast<std::uint64_t>(XPLMGetElapsedTime() * 1000.0f);
}

std::uint64_t monotonic_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::uint64_t estimated_speech_duration_ms(const std::string &text) {
  std::istringstream words(text);
  std::string word;
  std::uint64_t word_count = 0;
  while (words >> word) {
    ++word_count;
  }
  // Approximately 150 words/minute plus startup/tail room. This is deliberately
  // conservative because the window both diagnoses overlap and gates the next
  // routine transmission when XPLMSpeakString cannot report completion.
  const std::uint64_t estimate = word_count * 400U + 300U;
  return estimate < 1000U ? 1000U : estimate;
}

void log_line(const std::string &message) {
  XPLMDebugString((std::string(kLogPrefix) + message + "\n").c_str());
}

// Where the console lives. Resolution order, last wins:
//   1. the loopback default (a console running on this machine, unchanged behaviour)
//   2. <X-Plane>/Output/preferences/zoal_atc.cfg
//   3. ZOAL_ATC_CONSOLE_URL / ZOAL_ATC_CONSOLE_TOKEN
//
// X-Plane is usually launched from Finder or Steam, where exported environment
// variables do not reach it, so the file is the path a user can actually rely
// on; the variables exist for a terminal-launched dev run.
constexpr const char *kConfigRelativePath = "Output/preferences/zoal_atc.cfg";

// Nullopt distinguishes "no such file" from "the file is empty": the first is
// a config we should offer to create, the second is one the user emptied on
// purpose and must not be written over.
std::optional<std::string> read_file(const std::string &path) {
  std::FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return std::nullopt;
  }
  std::string contents;
  char buffer[4096];
  while (const std::size_t n = std::fread(buffer, 1, sizeof(buffer), file)) {
    contents.append(buffer, n);
  }
  std::fclose(file);
  return contents;
}

// Drops the commented template at `path` so the file is discoverable where it
// is read, not only in the README. Best effort in every direction: an existing
// file is never touched, and a read-only or otherwise unwritable X-Plane
// install is logged and flown past. The template is fully commented, so its
// appearance cannot move the console.
void write_config_template_if_missing(const std::string &path) {
  // "wx" fails if the path already exists, so a config written between the
  // read above and this call is still safe from being clobbered.
  std::FILE *file = std::fopen(path.c_str(), "wbx");
  if (file == nullptr) {
    log_line("could not create " + path +
             "; set the console URL via ZOAL_ATC_CONSOLE_URL instead");
    return;
  }
  const std::string contents =
      zoal_atc::transport::default_config_template();
  const std::size_t written =
      std::fwrite(contents.data(), 1, contents.size(), file);
  const bool complete = written == contents.size();
  if (std::fclose(file) != 0 || !complete) {
    log_line("could not finish writing " + path);
    return;
  }
  log_line("wrote a starter console config to " + path);
}

// Phase 22 M1. The console tells two connected aircraft apart by an identity
// the plugin carries, not by anything in the frames — so it has to be stable
// across restarts, which means it lives in the config file.
//
// It identifies the installation, the way a Mode S address identifies an
// airframe. It is an identifier, not a credential: anyone who learns it can
// claim it, which is what the JWT step (phase22 A2) exists to fix.
std::string generate_client_id() {
  std::random_device rd;
  std::uniform_int_distribution<int> hex(0, 15);
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string id;
  id.reserve(16);
  for (int i = 0; i < 16; ++i) {
    id.push_back(kDigits[hex(rd)]);
  }
  return id;
}

// Appends the generated identity rather than rewriting the file, so a config
// the pilot has edited keeps its comments and its ordering. Failure is logged
// and flown past: an identity that cannot be persisted still works for this
// session, it just will not survive a restart.
void append_client_id(const std::string &path, const std::string &client_id) {
  std::FILE *file = std::fopen(path.c_str(), "ab");
  if (file == nullptr) {
    log_line("could not record client_id in " + path +
             "; this aircraft will get a new identity next start");
    return;
  }
  const std::string block =
      "\n# Identity for this plugin installation, generated on first run.\n"
      "# The console uses it to tell connected aircraft apart. Copying it to\n"
      "# another machine makes both look like the same aircraft.\n"
      "client_id = " +
      client_id + "\n";
  const std::size_t written = std::fwrite(block.data(), 1, block.size(), file);
  const bool complete = written == block.size();
  if (std::fclose(file) != 0 || !complete) {
    log_line("could not finish recording client_id in " + path);
    return;
  }
  log_line("recorded a new client_id in " + path);
}

// --- the panel's connection settings ----------------------------------------
//
// The one thing the panel changes without the console, because it is how a
// console that refuses the connection becomes reachable at all. Everything else
// the Settings tab offers is stored console-side and needs a live socket; a
// token gated the same way could only be fixed by hand-editing zoal_atc.cfg,
// which is the file the panel exists to spare the pilot.
//
// The URL travels with it read-only. It is deliberately not editable: a mistyped
// endpoint takes the pilot off the air with no way back inside the sim, and the
// hosted console's address does not change.

std::string g_config_path;
std::string g_console_label;

std::string connection_settings_json() {
  const std::string token =
      g_websocket ? g_websocket->auth_token() : std::string();
  return std::string(R"({"token":")") +
         zoal_atc::transport::json_escape(token) + R"(","url":")" +
         zoal_atc::transport::json_escape(g_console_label) + R"("})";
}

// Writes through a temporary and renames, so a crash or a full disk cannot
// leave the pilot with a truncated config and no way to reach a console. The
// file holds their hand-written comments and their client_id, not just our key.
bool write_config_atomically(const std::string &path,
                             const std::string &contents) {
  const std::string temp = path + ".tmp";
  std::FILE *file = std::fopen(temp.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const std::size_t written =
      std::fwrite(contents.data(), 1, contents.size(), file);
  const bool complete = written == contents.size();
  if (std::fclose(file) != 0 || !complete) {
    std::remove(temp.c_str());
    return false;
  }
  // POSIX renames over an existing file atomically; Windows refuses, so the
  // target goes first there and the window is accepted.
#if defined(_WIN32)
  std::remove(path.c_str());
#endif
  if (std::rename(temp.c_str(), path.c_str()) != 0) {
    std::remove(temp.c_str());
    return false;
  }
  return true;
}

std::optional<zoal_atc::gui::LocalAnswer>
handle_local_gui_action(const std::string &action,
                        const std::string &payload_json) {
  if (action == "connection_settings") {
    return zoal_atc::gui::LocalAnswer{true, connection_settings_json(), ""};
  }
  if (action != "save_connection_settings") {
    // Not ours: the bridge proxies it to the console as usual.
    return std::nullopt;
  }

  const auto token =
      zoal_atc::transport::json_string_field(payload_json, "token");
  if (!token.has_value()) {
    return zoal_atc::gui::LocalAnswer{false, "null",
                                      "the request carried no token"};
  }
  // The same fence the config parser applies, refused here so the pilot is told
  // rather than finding the value silently dropped on the next start.
  if (!zoal_atc::transport::token_is_safe(*token)) {
    return zoal_atc::gui::LocalAnswer{
        false, "null", "a token cannot contain a control character"};
  }
  if (g_config_path.empty()) {
    return zoal_atc::gui::LocalAnswer{false, "null",
                                      "no config file path is known"};
  }

  const auto existing = read_file(g_config_path);
  const std::string updated = zoal_atc::transport::upsert_config_value(
      existing.value_or(std::string()), "token", *token);
  if (!write_config_atomically(g_config_path, updated)) {
    return zoal_atc::gui::LocalAnswer{false, "null",
                                      "could not write " + g_config_path};
  }

  // Present-or-absent, never the value: the log is the one artifact a pilot
  // pastes into a bug report.
  log_line(token->empty() ? "console token cleared from the panel"
                          : "console token set from the panel");

  // Swap it on the live socket and drop the connection, so the status line
  // shows whether the new credential works instead of waiting for a restart.
  if (g_websocket) {
    g_websocket->reauthenticate(*token);
  }
  return zoal_atc::gui::LocalAnswer{true, connection_settings_json(), ""};
}

std::optional<std::string> env_value(const char *name) {
  const char *value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

WebSocketEndpoint resolve_console_endpoint(const std::string &system_path) {
  WebSocketEndpoint endpoint;  // loopback default

  const std::string config_path = system_path + kConfigRelativePath;
  // Remembered so the panel can write the token back to the same file this
  // resolution read it from.
  g_config_path = config_path;
  const auto config_text = read_file(config_path);
  if (!config_text.has_value()) {
    // First run on this install: leave a documented file at the path that
    // reads it. It changes nothing by itself -- every key ships commented.
    write_config_template_if_missing(config_path);
  } else if (!config_text->empty()) {
    auto configured = apply_config_text(*config_text, endpoint);
    if (configured.ok) {
      endpoint = std::move(configured.endpoint);
      log_line("console config loaded from " + config_path);
    } else {
      // Keep flying against the previous endpoint rather than not at all, but
      // say plainly that the file was ignored.
      log_line("console config " + config_path + " ignored: " +
               configured.error);
    }
  }

  // Generated after the file is read so an existing identity is reused, and
  // before the env overrides so a URL override does not decide whether this
  // aircraft has one.
  if (endpoint.client_id.empty()) {
    endpoint.client_id = generate_client_id();
    append_client_id(config_path, endpoint.client_id);
  }

  if (const auto url = env_value("ZOAL_ATC_CONSOLE_URL")) {
    auto parsed = parse_websocket_url(*url);
    if (parsed.ok) {
      parsed.endpoint.auth_token = endpoint.auth_token;
      parsed.endpoint.client_id = endpoint.client_id;
      endpoint = std::move(parsed.endpoint);
      log_line("console URL overridden by ZOAL_ATC_CONSOLE_URL");
    } else {
      log_line("ZOAL_ATC_CONSOLE_URL ignored: " + parsed.error);
    }
  }
  if (const auto token = env_value("ZOAL_ATC_CONSOLE_TOKEN")) {
    endpoint.auth_token = *token;
    log_line("console token overridden by ZOAL_ATC_CONSOLE_TOKEN");
  }

  return endpoint;
}

std::optional<ATCReplyMessage> parse_atc_reply(const std::string &payload) {
  const auto type = json_string_field(payload, "type");
  if (!type.has_value() || *type != "atc_reply") {
    return std::nullopt;
  }
  const auto text = json_string_field(payload, "text");
  if (!text.has_value()) {
    return std::nullopt;
  }

  ATCReplyMessage reply;
  reply.session_id =
      json_string_field(payload, "session_id").value_or(frame_session_id());
  reply.category = json_string_field(payload, "category").value_or("");
  reply.text = *text;
  reply.speak = json_bool_field(payload, "speak").value_or(false);
  reply.generation = json_uint_field(payload, "generation").value_or(0);
  return reply;
}

using zoal_atc::transport::json_escape;

void queue_atc_reply(ATCReplyMessage reply) {
  reply.queued_at_ms = monotonic_ms();
  std::lock_guard<std::mutex> lock(g_reply_mutex);
  constexpr std::size_t kMaxQueuedReplies = 16;
  if (g_reply_queue.size() >= kMaxQueuedReplies) {
    g_reply_queue.pop_front();
  }
  g_reply_queue.push_back(std::move(reply));
}

bool urgent_speech(const ATCReplyMessage &reply) {
  return reply.category == "safety_alert" ||
         reply.category == "emergency_assistance";
}

std::optional<ATCReplyMessage> take_atc_reply(bool urgent_only) {
  std::lock_guard<std::mutex> lock(g_reply_mutex);
  if (g_reply_queue.empty()) {
    return std::nullopt;
  }
  auto selected = g_reply_queue.begin();
  if (urgent_only) {
    while (selected != g_reply_queue.end() && !urgent_speech(*selected)) {
      ++selected;
    }
    if (selected == g_reply_queue.end()) {
      return std::nullopt;
    }
  }
  ATCReplyMessage reply = std::move(*selected);
  g_reply_queue.erase(selected);
  return reply;
}

void send_speak_ack(const ATCReplyMessage &reply, bool ok,
                    const SpeechDeliveryInfo &speech) {
  if (!g_transport_sink || reply.generation == 0) {
    return;
  }
  std::ostringstream message;
  message << "{\"type\":\"speak_ack\",\"session_id\":\""
          << json_escape(reply.session_id) << "\",\"generation\":"
          << reply.generation << ",\"ok\":" << (ok ? "true" : "false")
          << ",\"speech_api\":\"XPLMSpeakString\""
          << ",\"speech_api_called\":"
          << (speech.api_called ? "true" : "false")
          << ",\"ack_basis\":\"" << json_escape(speech.ack_basis)
          << "\",\"plugin_queue_ms\":" << speech.plugin_queue_ms
          << ",\"estimated_duration_ms\":"
          << speech.estimated_duration_ms
          << ",\"speech_elapsed_ms\":" << speech.speech_elapsed_ms
          << ",\"possible_overlap\":"
          << (speech.possible_overlap ? "true" : "false")
          << ",\"previous_generation\":" << speech.previous_generation
          << ",\"previous_call_age_ms\":" << speech.previous_call_age_ms
          << ",\"previous_estimated_remaining_ms\":"
          << speech.previous_estimated_remaining_ms
          << "}";
  g_transport_sink->publish_text(message.str());
}

void finish_active_speech(std::uint64_t now, bool ok,
                          const std::string &ack_basis) {
  if (!g_active_speech.has_value()) {
    return;
  }
  ActiveSpeech active = std::move(*g_active_speech);
  g_active_speech.reset();
  active.delivery.ack_basis = ack_basis;
  if (now >= active.started_at_ms) {
    active.delivery.speech_elapsed_ms = now - active.started_at_ms;
  }

  std::ostringstream log;
  log << "speech_ack gen=" << active.reply.generation
      << " ok=" << (ok ? "true" : "false")
      << " ack_basis=" << ack_basis
      << " speech_elapsed_ms=" << active.delivery.speech_elapsed_ms
      << " estimated_duration_ms="
      << active.delivery.estimated_duration_ms;
  log_line(log.str());
  send_speak_ack(active.reply, ok, active.delivery);
}

void start_atc_reply(ATCReplyMessage reply, std::uint64_t process_at_ms) {
  std::ostringstream log;
  log << "ATC reply";
  if (!reply.category.empty()) {
    log << " category=" << reply.category;
  }
  log << " gen=" << reply.generation << ": " << reply.text;
  log_line(log.str());

  const bool ok = !reply.text.empty();
  SpeechDeliveryInfo speech;
  if (reply.queued_at_ms > 0 && process_at_ms >= reply.queued_at_ms) {
    speech.plugin_queue_ms = process_at_ms - reply.queued_at_ms;
  }
  if (!ok || !reply.speak) {
    speech.ack_basis = ok ? "speech_not_requested" : "empty_text";
    send_speak_ack(reply, ok, speech);
    return;
  }

  // reply.text keeps its punctuation for display/logging; the synth reads
  // commas etc. aloud ("... Quebec comma"), so voice a sanitized form.
  const std::string spoken_text =
      zoal_atc::transport::speech_text(reply.text);
  speech.api_called = true;
  speech.ack_basis = "estimated_playback_window_pending";
  speech.estimated_duration_ms = estimated_speech_duration_ms(spoken_text);
  if (g_last_speech_call.generation > 0) {
    speech.previous_generation = g_last_speech_call.generation;
    if (process_at_ms >= g_last_speech_call.called_at_ms) {
      speech.previous_call_age_ms =
          process_at_ms - g_last_speech_call.called_at_ms;
    }
    if (process_at_ms < g_last_speech_call.estimated_end_ms) {
      speech.possible_overlap = true;
      speech.previous_estimated_remaining_ms =
          g_last_speech_call.estimated_end_ms - process_at_ms;
    }
  }
  XPLMSpeakString(spoken_text.c_str());
  g_last_speech_call =
      SpeechCallState{reply.generation, process_at_ms,
                      process_at_ms + speech.estimated_duration_ms};
  g_active_speech = ActiveSpeech{
      std::move(reply), speech, process_at_ms,
      process_at_ms + speech.estimated_duration_ms};

  std::ostringstream speech_log;
  speech_log << "speech_api gen=" << g_active_speech->reply.generation
             << " api=XPLMSpeakString"
             << " ack_basis=estimated_playback_window_pending"
             << " ack_deferred=true"
             << " queue_ms=" << speech.plugin_queue_ms
             << " estimated_duration_ms="
             << speech.estimated_duration_ms
             << " possible_overlap="
             << (speech.possible_overlap ? "true" : "false");
  if (speech.previous_generation > 0) {
    speech_log << " previous_gen=" << speech.previous_generation
               << " previous_call_age_ms="
               << speech.previous_call_age_ms
               << " previous_estimated_remaining_ms="
               << speech.previous_estimated_remaining_ms;
  }
  log_line(speech_log.str());
}

void process_atc_replies() {
  const std::uint64_t now = monotonic_ms();

  if (g_active_speech.has_value()) {
    // Preserve the arbiter's class-A behavior: an immediate safety/emergency
    // transmission may preempt routine speech. The interrupted generation is
    // explicitly failed and logged before the urgent call begins.
    if (auto urgent = take_atc_reply(true); urgent.has_value()) {
      finish_active_speech(now, false, "preempted_by_urgent_speech");
      start_atc_reply(std::move(*urgent), now);
      return;
    }
    if (now < g_active_speech->acknowledge_at_ms) {
      return;
    }
    // XPLMSpeakString provides no completion signal. Reaching the conservative
    // speech window is the strongest acknowledgement available with this API.
    finish_active_speech(now, true, "estimated_playback_window_elapsed");
  }

  // Non-speech and invalid frames acknowledge immediately and do not occupy the
  // gate. Bound the loop so a malformed burst cannot monopolize a flight frame.
  constexpr int kMaxImmediateRepliesPerFrame = 16;
  for (int i = 0; i < kMaxImmediateRepliesPerFrame; ++i) {
    auto reply = take_atc_reply(false);
    if (!reply.has_value()) {
      return;
    }
    start_atc_reply(std::move(*reply), now);
    if (g_active_speech.has_value()) {
      return;
    }
  }
}

void reply_receiver_loop() {
  while (!g_reply_receiver_stopping.load()) {
    if (!g_websocket) {
      break;
    }
    std::string payload;
    const auto status = g_websocket->receive_text(payload);
    if (g_reply_receiver_stopping.load()) {
      break;
    }
    if (!status.ok) {
      log_line("websocket receive failed: " + status.message);
      // Connection lost: drop any console-commanded rate so sampling reverts to
      // the local policy until the console reconnects and re-commands (P2-M4).
      {
        std::lock_guard<std::mutex> lock(g_rate_mutex);
        g_rate_controller.reset();
      }
      // Settle whatever the panel was waiting on rather than leave it spinning
      // on a link that is gone.
      g_gui_bridge.set_connected(false, monotonic_ms());
      // A notification preference should not outlive the console that sent it.
      // The local default is "on", which is the safe direction for something
      // whose failure mode is silence.
      zoal_atc::gui::clear_notification_control();
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    // A frame arriving is the only evidence this thread gets that the socket is
    // up; receive_text reconnects underneath us.
    g_gui_bridge.set_connected(true, monotonic_ms());
    // GUI frames are queued, never delivered from here: Skyscript is pumped on
    // the main thread and this is not it.
    if (g_gui_bridge.on_socket_frame(payload, monotonic_ms())) {
      continue;
    }
    auto reply = parse_atc_reply(payload);
    if (reply.has_value()) {
      queue_atc_reply(std::move(*reply));
      continue;
    }
    // Console-commanded sampling profile/rate (P2-M4): apply it to the controller
    // so the flight loop samples at the commanded interval.
    auto control = zoal_atc::telemetry::parse_telemetry_control(payload);
    if (control.valid) {
      {
        std::lock_guard<std::mutex> lock(g_rate_mutex);
        g_rate_controller.apply(control);
      }
      log_line("telemetry_control: profile=" + control.profile);
      continue;
    }
    // Console-commanded notification preference (phase 24 M7). Applied on this
    // thread because the controller is plain data the flight loop reads; it
    // never touches Skyscript from here, which is the rule that keeps a socket
    // thread out of CEF.
    if (zoal_atc::gui::apply_notification_control(payload)) {
      log_line("notification_control applied");
      continue;
    }
    // Console one-off pull query (P2-M4 T4): enqueue for the flight loop to
    // service on the main thread (dataref reads are main-thread-only).
    auto query = zoal_atc::telemetry::parse_telemetry_query(payload);
    if (query.valid) {
      {
        std::lock_guard<std::mutex> lock(g_query_mutex);
        g_pending_queries.push_back(std::move(query));
      }
      continue;
    }
    auto radio_tune = zoal_atc::telemetry::parse_radio_tune_command(payload);
    if (radio_tune.valid) {
      {
        std::lock_guard<std::mutex> lock(g_radio_tune_mutex);
        g_pending_radio_tunes.push_back(std::move(radio_tune));
      }
      continue;
    }
    // Console cache miss: it asks for the full navdata text by hash.
    auto request = zoal_atc::navdata::parse_navdata_request(payload);
    if (request.has_value()) {
      enqueue_navdata_request(std::move(*request));
    }
  }
}

void start_reply_receiver() {
  g_reply_receiver_stopping.store(false);
  g_reply_receiver_thread = std::thread(reply_receiver_loop);
}

void stop_reply_receiver() {
  g_reply_receiver_stopping.store(true);
  if (g_websocket) {
    g_websocket->close();
  }
  if (g_reply_receiver_thread.joinable()) {
    g_reply_receiver_thread.join();
  }
  std::lock_guard<std::mutex> lock(g_reply_mutex);
  g_reply_queue.clear();
}

// --- Telemetry sampling (dataref half of docs/data-sources.md) --------------

// TelemetryDatarefs resolves the starter dataref catalog once at startup.
// Missing datarefs stay null and their fields degrade to zero.
struct TelemetryDatarefs {
  XPLMDataRef latitude = nullptr;
  XPLMDataRef longitude = nullptr;
  XPLMDataRef elevation_m = nullptr;
  XPLMDataRef y_agl_m = nullptr;
  XPLMDataRef groundspeed_mps = nullptr;
  XPLMDataRef indicated_airspeed_kias = nullptr;
  XPLMDataRef vh_ind_fpm = nullptr;
  XPLMDataRef true_psi = nullptr;
  XPLMDataRef onground_any = nullptr;
  XPLMDataRef com1_khz = nullptr;
  XPLMDataRef com2_khz = nullptr;
  XPLMDataRef com1_stby_khz = nullptr;
  XPLMDataRef com2_stby_khz = nullptr;
  XPLMDataRef audio_com_selection = nullptr;
  XPLMDataRef wind_dir_degt = nullptr;
  XPLMDataRef wind_speed_msc = nullptr;
  XPLMDataRef visibility_sm = nullptr;
  XPLMDataRef cloud_type = nullptr;
  XPLMDataRef cloud_base_msl_m = nullptr;
  XPLMDataRef temperature_c = nullptr;
  XPLMDataRef dewpoint_c = nullptr;
  XPLMDataRef qnh_pas = nullptr;
  XPLMDataRef transponder_code = nullptr;
  XPLMDataRef transponder_mode = nullptr;
  XPLMDataRef paused = nullptr;
  XPLMDataRef total_running_time_sec = nullptr;

  // TCAS traffic (phase21). tcas_num_acf stays null when neither spelling of
  // the count dataref resolves; traffic_resolved() is what turns that into an
  // explicit "no picture" rather than zero aircraft.
  XPLMDataRef tcas_num_acf = nullptr;
  XPLMDataRef tcas_modeS_id = nullptr;
  XPLMDataRef tcas_lat = nullptr;
  XPLMDataRef tcas_lon = nullptr;
  XPLMDataRef tcas_ele = nullptr;
  XPLMDataRef tcas_vertical_speed = nullptr;
  XPLMDataRef tcas_hpath = nullptr;
  XPLMDataRef tcas_psi = nullptr;
  XPLMDataRef tcas_v_msc = nullptr;
  XPLMDataRef tcas_weight_on_wheels = nullptr;
  XPLMDataRef tcas_flight_id = nullptr;
  XPLMDataRef tcas_icao_type = nullptr;

  // traffic_resolved reports whether the feed can be read at all. The position
  // arrays plus the count are the minimum: without them there is no picture,
  // and reporting zero aircraft for a dataref that does not exist is the exact
  // failure phase21 exists to prevent (docs/phase21/02-the-feed.md).
  bool traffic_resolved() const {
    return tcas_num_acf != nullptr && tcas_lat != nullptr &&
           tcas_lon != nullptr && tcas_ele != nullptr;
  }

  void resolve() {
    latitude = XPLMFindDataRef("sim/flightmodel/position/latitude");
    longitude = XPLMFindDataRef("sim/flightmodel/position/longitude");
    elevation_m = XPLMFindDataRef("sim/flightmodel/position/elevation");
    y_agl_m = XPLMFindDataRef("sim/flightmodel/position/y_agl");
    groundspeed_mps = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    indicated_airspeed_kias =
        XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed");
    vh_ind_fpm = XPLMFindDataRef("sim/flightmodel/position/vh_ind_fpm");
    true_psi = XPLMFindDataRef("sim/flightmodel/position/true_psi");
    onground_any = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    com1_khz =
        XPLMFindDataRef("sim/cockpit2/radios/actuators/com1_frequency_hz_833");
    com2_khz =
        XPLMFindDataRef("sim/cockpit2/radios/actuators/com2_frequency_hz_833");
    com1_stby_khz = XPLMFindDataRef(
        "sim/cockpit2/radios/actuators/com1_standby_frequency_hz_833");
    com2_stby_khz = XPLMFindDataRef(
        "sim/cockpit2/radios/actuators/com2_standby_frequency_hz_833");
    audio_com_selection =
        XPLMFindDataRef("sim/cockpit2/radios/actuators/audio_com_selection");
    wind_dir_degt =
        XPLMFindDataRef("sim/weather/aircraft/wind_direction_degt");
    wind_speed_msc = XPLMFindDataRef("sim/weather/aircraft/wind_speed_msc");
    visibility_sm =
        XPLMFindDataRef("sim/weather/aircraft/visibility_reported_sm");
    cloud_type = XPLMFindDataRef("sim/weather/aircraft/cloud_type");
    cloud_base_msl_m =
        XPLMFindDataRef("sim/weather/aircraft/cloud_base_msl_m");
    temperature_c =
        XPLMFindDataRef("sim/weather/aircraft/temperature_ambient_deg_c");
    dewpoint_c = XPLMFindDataRef("sim/weather/aircraft/dewpoint_deg_c");
    qnh_pas = XPLMFindDataRef("sim/weather/aircraft/qnh_pas");
    transponder_code =
        XPLMFindDataRef("sim/cockpit2/radios/actuators/transponder_code");
    transponder_mode =
        XPLMFindDataRef("sim/cockpit2/radios/actuators/transponder_mode");
    paused = XPLMFindDataRef("sim/time/paused");
    total_running_time_sec =
        XPLMFindDataRef("sim/time/total_running_time_sec");

    // The count dataref is documented by X-Plane with a typo — "tacos", not
    // "tcas" — and which spelling the sim actually registers varies. Try the
    // correct one first, then the documented misspelling. If neither resolves,
    // traffic_resolved() is false and the console is told `unavailable`; an
    // unresolved dataref must never read as an empty sky.
    tcas_num_acf = XPLMFindDataRef("sim/cockpit2/tcas/indicators/tcas_num_acf");
    if (tcas_num_acf == nullptr) {
      tcas_num_acf =
          XPLMFindDataRef("sim/cockpit2/tacos/indicators/tcas_num_acf");
      if (tcas_num_acf != nullptr) {
        log_line("traffic: count dataref resolved via the `tacos` spelling");
      }
    }
    tcas_modeS_id = XPLMFindDataRef("sim/cockpit2/tcas/targets/modeS_id");
    tcas_lat = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/lat");
    tcas_lon = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/lon");
    tcas_ele = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/ele");
    tcas_vertical_speed =
        XPLMFindDataRef("sim/cockpit2/tcas/targets/position/vertical_speed");
    tcas_hpath = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/hpath");
    tcas_psi = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/psi");
    tcas_v_msc = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/V_msc");
    tcas_weight_on_wheels = XPLMFindDataRef(
        "sim/cockpit2/tcas/targets/position/weight_on_wheels");
    tcas_flight_id = XPLMFindDataRef("sim/cockpit2/tcas/targets/flight_id");
    tcas_icao_type = XPLMFindDataRef("sim/cockpit2/tcas/targets/icao_type");
    if (!traffic_resolved()) {
      log_line("traffic: TCAS datarefs unavailable — reporting no picture");
    }
  }
};

double read_d(XPLMDataRef ref) { return ref ? XPLMGetDatad(ref) : 0.0; }
double read_f(XPLMDataRef ref) {
  return ref ? static_cast<double>(XPLMGetDataf(ref)) : 0.0;
}
int read_i(XPLMDataRef ref) { return ref ? XPLMGetDatai(ref) : 0; }
void write_i(XPLMDataRef ref, int value) {
  if (ref) {
    XPLMSetDatai(ref, value);
  }
}

// read_scalar_dataref looks up a dataref by path and reads it as a double,
// choosing the read by the dataref's declared type (double > float > int). Array
// and data (byte) datarefs, and unknown/absent paths, return nullopt so the
// caller omits them from the query result (graceful degradation). Main-thread
// only (XPLM constraint).
std::optional<double> read_scalar_dataref(const std::string &path) {
  XPLMDataRef ref = XPLMFindDataRef(path.c_str());
  if (ref == nullptr) {
    return std::nullopt;
  }
  const int types = XPLMGetDataRefTypes(ref);
  if (types & xplmType_Double) {
    return XPLMGetDatad(ref);
  }
  if (types & xplmType_Float) {
    return static_cast<double>(XPLMGetDataf(ref));
  }
  if (types & xplmType_Int) {
    return static_cast<double>(XPLMGetDatai(ref));
  }
  return std::nullopt; // array / data / unknown — unsupported in the first pass
}

// process_pending_queries services any queued one-off pull queries on the main
// (flight-loop) thread: read each requested dataref and send a
// telemetry_query_result correlated by query_id (P2-M4 T4).
void process_pending_queries() {
  std::vector<zoal_atc::telemetry::TelemetryQuery> queries;
  {
    std::lock_guard<std::mutex> lock(g_query_mutex);
    if (g_pending_queries.empty()) {
      return;
    }
    queries.swap(g_pending_queries);
  }
  if (!g_transport_sink) {
    return;
  }
  for (const auto &query : queries) {
    std::vector<std::pair<std::string, double>> values;
    values.reserve(query.datarefs.size());
    for (const auto &path : query.datarefs) {
      if (const auto value = read_scalar_dataref(path)) {
        values.emplace_back(path, *value);
      }
    }
    g_transport_sink->publish_text(
        zoal_atc::telemetry::build_telemetry_query_result(query.query_id, values));
  }
}

TelemetryDatarefs g_datarefs;
double g_last_telemetry_sent_secs = 0;
double g_last_airport_check_secs = 0;
double g_last_metar_sent_secs = 0;
double g_last_navdata_scan_secs = 0;
bool g_has_last_pause_sent = false;
bool g_last_pause_sent = false;
std::string g_airport_id;
std::string g_airport_name;
std::string g_last_navdata_scan_airport;
bool g_navdata_scan_retry_pending = false;
bool g_last_websocket_connected = false;

constexpr double kAirportCheckIntervalSecs = 3.0;
constexpr double kMetarIntervalSecs = 600.0;

// active_com maps the audio_com_selection dataref (6 = COM1, 7 = COM2 in
// X-Plane 11+) to the wire contract's 1 | 2 (0 = unknown).
int active_com_from_selection(int selection) {
  switch (selection) {
  case 6:
    return 1;
  case 7:
    return 2;
  default:
    return 0;
  }
}

XPLMDataRef radio_tune_dataref(const zoal_atc::telemetry::RadioTuneCommand &cmd) {
  int com = cmd.com;
  if (com != 1 && com != 2) {
    com = active_com_from_selection(read_i(g_datarefs.audio_com_selection));
  }
  if (cmd.target == "active") {
    return com == 2 ? g_datarefs.com2_khz : g_datarefs.com1_khz;
  }
  return com == 2 ? g_datarefs.com2_stby_khz : g_datarefs.com1_stby_khz;
}

void process_pending_radio_tunes() {
  std::vector<zoal_atc::telemetry::RadioTuneCommand> commands;
  {
    std::lock_guard<std::mutex> lock(g_radio_tune_mutex);
    if (g_pending_radio_tunes.empty()) {
      return;
    }
    commands.swap(g_pending_radio_tunes);
  }
  for (const auto &cmd : commands) {
    write_i(radio_tune_dataref(cmd),
            zoal_atc::telemetry::com_mhz_to_hz833(cmd.frequency_mhz));
    log_line("radio_tune: " + cmd.target + " " +
             std::to_string(cmd.frequency_mhz));
  }
}

// lowest_cloud_layer picks the lowest reported layer with a non-clear type
// from the 3-layer cloud arrays.
void lowest_cloud_layer(int &type_out, double &base_ft_out) {
  type_out = 0;
  base_ft_out = 0;
  if (!g_datarefs.cloud_type || !g_datarefs.cloud_base_msl_m) {
    return;
  }
  int types[3] = {};
  float bases[3] = {};
  XPLMGetDatavi(g_datarefs.cloud_type, types, 0, 3);
  XPLMGetDatavf(g_datarefs.cloud_base_msl_m, bases, 0, 3);
  for (int layer = 0; layer < 3; ++layer) {
    if (types[layer] <= 0) {
      continue;
    }
    const double base_ft =
        zoal_atc::telemetry::meters_to_feet(static_cast<double>(bases[layer]));
    if (type_out == 0 || base_ft < base_ft_out) {
      type_out = types[layer];
      base_ft_out = base_ft;
    }
  }
}

// --- Traffic sampling (phase21) ---------------------------------------------

// Bulk array reads: one XPLM call per dataref per sample, never a lookup per
// target. A null dataref yields an empty vector, which extract_targets treats
// as "field absent" and degrades on.
std::vector<float> read_array_f(XPLMDataRef ref, int count) {
  if (ref == nullptr) {
    return {};
  }
  std::vector<float> out(static_cast<std::size_t>(count), 0.f);
  const int got = XPLMGetDatavf(ref, out.data(), 0, count);
  out.resize(static_cast<std::size_t>(got < 0 ? 0 : got));
  return out;
}

std::vector<std::int32_t> read_array_i(XPLMDataRef ref, int count) {
  if (ref == nullptr) {
    return {};
  }
  std::vector<int> raw(static_cast<std::size_t>(count), 0);
  const int got = XPLMGetDatavi(ref, raw.data(), 0, count);
  const int n = got < 0 ? 0 : got;
  return std::vector<std::int32_t>(raw.begin(), raw.begin() + n);
}

std::vector<char> read_array_b(XPLMDataRef ref, int count) {
  if (ref == nullptr) {
    return {};
  }
  std::vector<char> out(static_cast<std::size_t>(count), '\0');
  const int got = XPLMGetDatab(ref, out.data(), 0, count);
  out.resize(static_cast<std::size_t>(got < 0 ? 0 : got));
  return out;
}

// The traffic picture changes far more slowly than ownship kinematics, so on
// the 5 Hz surface profile it is decimated to ~2 Hz (docs/data-sources.md's
// separate traffic column) and the previous extract is reused in between.
//
// Main-thread only: sample_telemetry runs on the flight loop, so these need no
// lock. Ownship and traffic still ride the same frame, which keeps the two
// mutually consistent — closure rate is derived from both.
constexpr double kTrafficMinIntervalSecs = 0.5;
zoal_atc::telemetry::TrafficExtract g_last_traffic;
double g_last_traffic_secs = 0.0;
bool g_has_traffic_sample = false;

zoal_atc::telemetry::TrafficExtract sample_traffic(const TelemetrySnapshot &own,
                                                   double now_secs) {
  namespace tel = zoal_atc::telemetry;
  if (g_has_traffic_sample &&
      now_secs - g_last_traffic_secs < kTrafficMinIntervalSecs &&
      now_secs >= g_last_traffic_secs) {
    return g_last_traffic;
  }

  tel::TrafficArrays arrays;
  const int n = tel::kTcasSlots;
  arrays.modeS_id = read_array_i(g_datarefs.tcas_modeS_id, n);
  arrays.lat = read_array_f(g_datarefs.tcas_lat, n);
  arrays.lon = read_array_f(g_datarefs.tcas_lon, n);
  arrays.ele_m = read_array_f(g_datarefs.tcas_ele, n);
  arrays.vertical_speed_fpm = read_array_f(g_datarefs.tcas_vertical_speed, n);
  arrays.hpath_deg = read_array_f(g_datarefs.tcas_hpath, n);
  arrays.psi_deg = read_array_f(g_datarefs.tcas_psi, n);
  arrays.v_msc = read_array_f(g_datarefs.tcas_v_msc, n);
  arrays.weight_on_wheels = read_array_i(g_datarefs.tcas_weight_on_wheels, n);
  arrays.flight_id =
      read_array_b(g_datarefs.tcas_flight_id, n * tel::kTcasStringSlotWidth);
  arrays.icao_type =
      read_array_b(g_datarefs.tcas_icao_type, n * tel::kTcasStringSlotWidth);

  tel::OwnPosition own_pos;
  own_pos.lat_deg = own.latitude_deg;
  own_pos.lon_deg = own.longitude_deg;
  own_pos.alt_ft_msl = own.altitude_ft_msl;

  const int num_acf = read_i(g_datarefs.tcas_num_acf);
  g_last_traffic = tel::extract_targets(arrays, num_acf, own_pos,
                                        tel::TrafficBounds{},
                                        g_datarefs.traffic_resolved());
  g_last_traffic_secs = now_secs;
  g_has_traffic_sample = true;
  return g_last_traffic;
}

TelemetrySnapshot sample_telemetry() {
  namespace tel = zoal_atc::telemetry;
  TelemetrySnapshot s;
  s.latitude_deg = read_d(g_datarefs.latitude);
  s.longitude_deg = read_d(g_datarefs.longitude);
  s.altitude_ft_msl = tel::meters_to_feet(read_d(g_datarefs.elevation_m));
  s.height_agl_ft = tel::meters_to_feet(read_f(g_datarefs.y_agl_m));
  s.groundspeed_kts = tel::mps_to_knots(read_f(g_datarefs.groundspeed_mps));
  // indicated_airspeed is already in knots (KIAS) — no conversion.
  s.indicated_airspeed_kts = read_f(g_datarefs.indicated_airspeed_kias);
  // vh_ind_fpm is already in feet per minute — no conversion.
  s.vertical_speed_fpm = read_f(g_datarefs.vh_ind_fpm);
  s.heading_true_deg = read_f(g_datarefs.true_psi);
  s.on_ground = read_i(g_datarefs.onground_any) != 0;

  s.com1_freq_mhz = tel::com_hz833_to_mhz(read_i(g_datarefs.com1_khz));
  s.com2_freq_mhz = tel::com_hz833_to_mhz(read_i(g_datarefs.com2_khz));
  s.com1_standby_mhz =
      tel::com_hz833_to_mhz(read_i(g_datarefs.com1_stby_khz));
  s.com2_standby_mhz =
      tel::com_hz833_to_mhz(read_i(g_datarefs.com2_stby_khz));
  s.active_com =
      active_com_from_selection(read_i(g_datarefs.audio_com_selection));

  s.airport_id = g_airport_id;
  s.airport_name = g_airport_name;

  s.wind_direction_deg = read_f(g_datarefs.wind_dir_degt);
  s.wind_speed_kt = tel::mps_to_knots(read_f(g_datarefs.wind_speed_msc));
  s.visibility_m = tel::sm_to_meters(read_f(g_datarefs.visibility_sm));
  lowest_cloud_layer(s.cloud_type, s.cloud_base_ft_msl);
  s.temperature_c = read_f(g_datarefs.temperature_c);
  s.dewpoint_c = read_f(g_datarefs.dewpoint_c);
  const double qnh_pa = read_f(g_datarefs.qnh_pas);
  s.qnh_inhg = tel::pascals_to_inhg(qnh_pa);
  s.qnh_hpa = tel::pascals_to_hpa(qnh_pa);

  s.transponder_code = read_i(g_datarefs.transponder_code);
  s.transponder_mode = read_i(g_datarefs.transponder_mode);
  s.paused = read_i(g_datarefs.paused) != 0;
  s.now_secs = read_f(g_datarefs.total_running_time_sec);

  const tel::TrafficExtract traffic = sample_traffic(s, s.now_secs);
  s.traffic = traffic.targets;
  s.traffic_status = traffic.status;
  s.traffic_truncated = traffic.truncated;
  s.traffic_census = traffic.census;
  return s;
}

// --- Navdata worker (file half of docs/data-sources.md) ----------------------
//
// File reads (a multi-hundred-MB apt.dat scan on airport change) never run on
// the flight loop: a dedicated worker owns the NavdataPublisher and processes
// scan/request/metar jobs; outbound frames go through the async sink's queue.
class NavdataWorker {
public:
  explicit NavdataWorker(const std::string &xplane_root)
      : publisher_(NavdataPaths{xplane_root},
                   [](const std::string &frame) {
                     const auto type =
                         json_string_field(frame, "type").value_or("unknown");
                     log_line("queue " + type + " frame bytes=" +
                              std::to_string(frame.size()));
                     if (g_transport_sink) {
                       g_transport_sink->publish_text(frame);
                     }
                   },
                   [](const std::string &message) { log_line(message); }),
        metar_dir_(NavdataPaths{xplane_root}.metar_dir()),
        thread_([this] { run(); }) {}

  ~NavdataWorker() { stop(); }

  void enqueue_airport_scan(std::string icao) {
    push(Job{JobKind::Scan, std::move(icao), NavdataRequest{}});
  }

  void enqueue_request(NavdataRequest request) {
    push(Job{JobKind::Request, "", std::move(request)});
  }

  void enqueue_metar() { push(Job{JobKind::Metar, "", NavdataRequest{}}); }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  enum class JobKind { Scan, Request, Metar };
  struct Job {
    JobKind kind;
    std::string icao;
    NavdataRequest request;
  };

  void push(Job job) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
  }

  void run() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (stopping_ && jobs_.empty()) {
          return;
        }
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      switch (job.kind) {
      case JobKind::Scan:
        log_line("navdata scan started for " + job.icao);
        publisher_.publish_for_airport(job.icao);
        log_line("navdata scan finished for " + job.icao);
        break;
      case JobKind::Request:
        if (!publisher_.handle_request(job.request)) {
          log_line("navdata request for unknown hash " + job.request.md5);
        }
        break;
      case JobKind::Metar:
        send_metar();
        break;
      }
    }
  }

  void send_metar() {
    namespace nav = zoal_atc::navdata;
    const auto newest = nav::newest_metar_file(publisher_paths_metar_dir());
    if (!newest.has_value()) {
      return;
    }
    const auto text = nav::read_file_text(*newest);
    if (!text.has_value() || text->empty()) {
      return;
    }
    if (g_transport_sink) {
      const auto frame = nav::build_metar_frame(*text);
      log_line("queue weather_metar frame bytes=" +
               std::to_string(frame.size()));
      g_transport_sink->publish_text(frame);
    }
  }

  std::string publisher_paths_metar_dir() const { return metar_dir_; }

  NavdataPublisher publisher_;
  std::string metar_dir_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Job> jobs_;
  bool stopping_ = false;
  std::thread thread_;
};

std::unique_ptr<NavdataWorker> g_navdata_worker;

void enqueue_navdata_request(NavdataRequest request) {
  if (g_navdata_worker) {
    if (request.scan) {
      g_navdata_worker->enqueue_airport_scan(std::move(request.icao));
    } else {
      g_navdata_worker->enqueue_request(std::move(request));
    }
  }
}

bool websocket_connected() {
  return g_websocket && g_websocket->connected();
}

void enqueue_navdata_scan(const std::string &icao, double now_secs,
                          const std::string &reason) {
  if (!g_navdata_worker || icao.empty()) {
    return;
  }
  const bool connected = websocket_connected();
  std::string message = reason + "; scanning navdata for " + icao;
  if (!connected) {
    message += " (websocket not connected; will retry on reconnect)";
  }
  log_line(message);

  g_navdata_worker->enqueue_airport_scan(icao);
  g_navdata_worker->enqueue_metar();
  g_last_metar_sent_secs = now_secs;
  g_last_navdata_scan_secs = now_secs;
  g_last_navdata_scan_airport = icao;
  g_navdata_scan_retry_pending = !connected;
}

void retry_navdata_after_websocket_reconnect(double now_secs) {
  const bool connected = websocket_connected();
  if (!connected) {
    // Force the first frame after reconnect even when X-Plane is paused and its
    // monotonic clock cannot make the normal rate controller due.
    g_has_last_pause_sent = false;
    if (g_last_websocket_connected && !g_airport_id.empty()) {
      g_navdata_scan_retry_pending = true;
    }
    g_last_websocket_connected = false;
    return;
  }

  const bool reconnected = !g_last_websocket_connected;
  g_last_websocket_connected = true;
  if (!g_navdata_scan_retry_pending || g_airport_id.empty()) {
    return;
  }
  if (g_last_navdata_scan_airport == g_airport_id &&
      now_secs - g_last_navdata_scan_secs < 1.0) {
    return;
  }
  enqueue_navdata_scan(
      g_airport_id, now_secs,
      reconnected ? "websocket connected; retrying navdata scan"
                  : "websocket ready; retrying pending navdata scan");
}

// check_nearest_airport refreshes the active field from the nav database and
// kicks a navdata scan when it changes. Runs at a slow cadence off the flight
// loop (the lookup is cheap but not free).
void check_nearest_airport(double now_secs) {
  if (now_secs - g_last_airport_check_secs < kAirportCheckIntervalSecs &&
      g_last_airport_check_secs != 0) {
    return;
  }
  g_last_airport_check_secs = now_secs;

  float lat = static_cast<float>(read_d(g_datarefs.latitude));
  float lon = static_cast<float>(read_d(g_datarefs.longitude));
  const XPLMNavRef ref =
      XPLMFindNavAid(nullptr, nullptr, &lat, &lon, nullptr, xplm_Nav_Airport);
  if (ref == XPLM_NAV_NOT_FOUND) {
    return;
  }
  char id[32] = {};
  char name[256] = {};
  XPLMGetNavAidInfo(ref, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    id, name, nullptr);
  if (id[0] == '\0' || g_airport_id == id) {
    return;
  }
  g_airport_id = id;
  g_airport_name = name;
  enqueue_navdata_scan(g_airport_id, now_secs, "active airport changed");
}

void sample_and_send_telemetry(double now_secs) {
  if (!g_transport_sink) {
    return;
  }
  const bool on_ground = read_i(g_datarefs.onground_any) != 0;
  const double agl_ft =
      zoal_atc::telemetry::meters_to_feet(read_f(g_datarefs.y_agl_m));
  const bool paused = read_i(g_datarefs.paused) != 0;
  bool due = false;
  {
    std::lock_guard<std::mutex> lock(g_rate_mutex);
    due = !g_has_last_pause_sent || paused != g_last_pause_sent ||
          g_rate_controller.due(now_secs, g_last_telemetry_sent_secs, on_ground,
                                agl_ft);
  }
  if (!due) {
    return;
  }
  g_last_telemetry_sent_secs = now_secs;
  g_has_last_pause_sent = true;
  g_last_pause_sent = paused;
  g_transport_sink->publish_text(
      zoal_atc::telemetry::build_telemetry_frame(sample_telemetry(),
                                                 frame_session_id()));
}

class XPlanePttSink final : public PttEventSink {
public:
  explicit XPlanePttSink(PttEventSink &delegate) : delegate_(delegate) {}

  void publish(PttEvent event) override {
    char buffer[512] = {};
    switch (event.type) {
    case PttEventType::Started:
      std::snprintf(buffer, sizeof(buffer), "PTT started seq=%llu",
                    static_cast<unsigned long long>(event.sequence));
      break;
    case PttEventType::TransmissionReady:
      std::snprintf(buffer, sizeof(buffer),
                    "PTT transmission ready seq=%llu duration_ms=%llu "
                    "reason=%s samples=%zu",
                    static_cast<unsigned long long>(event.sequence),
                    static_cast<unsigned long long>(event.duration_ms),
                    to_string(event.end_reason), event.audio.pcm16.size());
      break;
    case PttEventType::Cancelled:
      std::snprintf(buffer, sizeof(buffer),
                    "PTT cancelled seq=%llu duration_ms=%llu reason=%s",
                    static_cast<unsigned long long>(event.sequence),
                    static_cast<unsigned long long>(event.duration_ms),
                    to_string(event.cancel_reason));
      break;
    case PttEventType::Ignored:
      std::snprintf(buffer, sizeof(buffer), "PTT ignored reason=%s",
                    to_string(event.ignore_reason));
      break;
    case PttEventType::Error:
      std::snprintf(buffer, sizeof(buffer), "PTT error seq=%llu message=%s",
                    static_cast<unsigned long long>(event.sequence),
                    event.message.c_str());
      break;
    }
    log_line(buffer);
    delegate_.publish(std::move(event));
  }

private:
  PttEventSink &delegate_;
};

int ptt_command_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (!g_ptt) {
    return 0;
  }

  if (phase == xplm_CommandBegin) {
    log_line("PTT command begin");
    g_ptt->command_begin(now_ms());
  } else if (phase == xplm_CommandEnd) {
    log_line("PTT command end");
    g_ptt->command_end(now_ms());
  }
  return 0;
}

// process_gui_bridge is the main-thread half of the GUI proxy: it expires stale
// panel requests, publishes whatever the panel asked for, and lets the panel
// owner post the results. Everything the browser sees passes through here.
void process_gui_bridge() {
  g_gui_bridge.tick(monotonic_ms());
  if (g_transport_sink) {
    const auto frames = g_gui_bridge.take_socket_frames();
    for (const auto &frame : frames) {
      g_transport_sink->publish_text(frame);
    }
  }
  zoal_atc::gui::tick_skyscript_panel();
}

float flight_loop_callback(float, float, int, void *) {
  if (g_ptt) {
    g_ptt->tick(now_ms());
  }
  process_gui_bridge();
  process_atc_replies();

  const double now_secs = read_f(g_datarefs.total_running_time_sec);
  check_nearest_airport(now_secs);
  process_pending_radio_tunes();
  sample_and_send_telemetry(now_secs);
  process_pending_queries();
  retry_navdata_after_websocket_reconnect(now_secs);
  // Real weather refreshes periodically; re-send the newest METAR file on a
  // slow cadence (also sent immediately on airport change).
  if (g_navdata_worker && g_last_metar_sent_secs != 0 &&
      now_secs - g_last_metar_sent_secs >= kMetarIntervalSecs) {
    g_last_metar_sent_secs = now_secs;
    g_navdata_worker->enqueue_metar();
  }
  return -1.0f;
}

} // namespace

PLUGIN_API int XPluginStart(char *name, char *sig, char *desc) {
  // Opt in before asking XPLM for any paths. On macOS this makes
  // XPLMGetSystemPath return POSIX paths instead of legacy HFS paths.
  XPLMEnableFeature(kNativePathsFeature, 1);

#ifdef ZOAL_ATC_VERSION
  std::snprintf(name, 256, "zoal-atc v%s", ZOAL_ATC_VERSION);
#else
  std::snprintf(name, 256, "zoal-atc");
#endif
  std::snprintf(sig, 256, "ai.zoal.atc");
  std::snprintf(desc, 256, "Audio-driven ATC AI edge plugin");

  zoal_atc::gui::SkyscriptPanelConfig panel_config;
#ifdef ZOAL_ATC_VERSION
  panel_config.plugin_version = ZOAL_ATC_VERSION;
#else
  panel_config.plugin_version = "dev";
#endif
  panel_config.bridge = &g_gui_bridge;
  panel_config.now_ms = monotonic_ms;
  zoal_atc::gui::start_skyscript_panel(panel_config);

  char system_path[1024] = {};
  XPLMGetSystemPath(system_path);

  g_recorder = std::make_unique<MiniaudioAudioRecorder>();
  auto endpoint = resolve_console_endpoint(std::string(system_path));
  if (!zoal_atc::transport::token_is_safe(endpoint.auth_token)) {
    log_line("console token contains a control character; dropping it");
    endpoint.auth_token.clear();
  }
  // Publish the resolved identity before anything builds a frame, so the
  // session_id every frame carries names this installation rather than the old
  // shared constant (phase22 M1).
  g_client_id = endpoint.client_id;
  // The panel shows this beside the token so the pilot can see which console
  // the credential is for. Read-only there, and built before the move.
  g_console_label = "ws://" + host_header(endpoint) + endpoint.path;
  const std::string endpoint_description =
      g_console_label +
      (endpoint.auth_token.empty() ? " (no token)" : " (token set)");
  g_websocket = std::make_unique<WebSocketClient>(std::move(endpoint));
  // Installed once the socket exists, because saving a token re-dials it.
  g_gui_bridge.set_local_action(handle_local_gui_action);
  g_transport_sink =
      std::make_unique<AsyncWebSocketPttSink>(
          *g_websocket, frame_session_id(), [](const std::string &reason) {
            log_line("websocket send failed: " + reason);
          });
  g_sink = std::make_unique<XPlanePttSink>(*g_transport_sink);

  // Dataref sampling + navdata sender (the second design input's edge half).
  g_datarefs.resolve();
  g_navdata_worker = std::make_unique<NavdataWorker>(std::string(system_path));

  start_reply_receiver();

  PushToTalkConfig config;
  config.min_transmission_ms = 250;
  config.max_transmission_ms = 30000;
  g_ptt = std::make_unique<PushToTalkController>(*g_recorder, *g_sink, config);

  g_ptt_command = XPLMCreateCommand(kCommandName, "zoal-atc: Push-to-Talk");
  XPLMRegisterCommandHandler(g_ptt_command, ptt_command_handler, 1, nullptr);

  XPLMCreateFlightLoop_t loop_params{};
  loop_params.structSize = sizeof(loop_params);
  loop_params.phase = xplm_FlightLoop_Phase_AfterFlightModel;
  loop_params.callbackFunc = flight_loop_callback;
  g_flight_loop = XPLMCreateFlightLoop(&loop_params);
  XPLMScheduleFlightLoop(g_flight_loop, -1.0f, true);

  log_line("plugin started; bind command zoal_atc/ptt for push-to-talk");
  log_line("enabled XPLM_USE_NATIVE_PATHS for POSIX SDK paths");
  log_line("console endpoint: " + endpoint_description);
  log_line("override with " + std::string(kConfigRelativePath) +
           " (url = ws://host[:port]/plugin, token = ...)");
  return 1;
}

PLUGIN_API void XPluginStop() {
  zoal_atc::gui::stop_skyscript_panel();

  if (g_flight_loop) {
    XPLMDestroyFlightLoop(g_flight_loop);
    g_flight_loop = nullptr;
  }

  if (g_ptt_command) {
    XPLMUnregisterCommandHandler(g_ptt_command, ptt_command_handler, 1,
                                 nullptr);
    g_ptt_command = nullptr;
  }

  g_ptt.reset();
  g_sink.reset();
  g_navdata_worker.reset(); // joins the worker before the sink goes away
  g_active_speech.reset();
  g_last_speech_call = SpeechCallState{};
  g_transport_sink.reset();
  stop_reply_receiver();
  g_websocket.reset();
  g_recorder.reset();
  log_line("plugin stopped");
}

PLUGIN_API int XPluginEnable() { return 1; }

PLUGIN_API void XPluginDisable() {}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}
