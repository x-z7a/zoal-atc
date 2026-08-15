#include "zoal_atc/transport/json_util.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

int g_failures = 0;

void fail(const char *label, const std::string &got, const std::string &want) {
  std::cerr << label << ": got [" << got << "], want [" << want << "]\n";
  ++g_failures;
}

void expect_raw(const std::optional<std::string> &got, const char *want,
                const char *label) {
  const std::string got_text = got.has_value() ? *got : "<none>";
  if (got_text == want) {
    return;
  }
  fail(label, got_text, want);
}

void expect_absent(const std::optional<std::string> &got, const char *label) {
  if (!got.has_value()) {
    return;
  }
  fail(label, *got, "<none>");
}

} // namespace

int main() {
  using zoal_atc::transport::json_raw_field;
  using zoal_atc::transport::json_unquote;

  // A nested object comes back verbatim, so the gui_* frames can carry an
  // opaque console payload the plugin never has to understand.
  const std::string response =
      R"({"type":"gui_response","request_id":7,"ok":true,)"
      R"("payload":{"callsign":"GABC","comm":{"active_mhz":118.8}}})";
  expect_raw(json_raw_field(response, "type"), "\"gui_response\"",
             "raw string field");
  expect_raw(json_raw_field(response, "request_id"), "7", "raw uint field");
  expect_raw(json_raw_field(response, "ok"), "true", "raw bool field");
  expect_raw(json_raw_field(response, "payload"),
             R"({"callsign":"GABC","comm":{"active_mhz":118.8}})",
             "raw nested object");
  expect_absent(json_raw_field(response, "error"), "absent field");

  // The whole point of scanning top level only: a key inside the payload must
  // never answer for the envelope, whatever order the console wrote them in.
  const std::string shadowed =
      R"({"payload":{"type":"inner","request_id":99},"type":"gui_event",)"
      R"("event":"flight_snapshot"})";
  expect_raw(json_raw_field(shadowed, "type"), "\"gui_event\"",
             "outer type not shadowed by nested type");
  expect_absent(json_raw_field(shadowed, "request_id"),
                "nested request_id is not a top-level field");
  expect_raw(json_raw_field(shadowed, "event"), "\"flight_snapshot\"",
             "field after a nested object");

  // Arrays, escapes, and a brace inside a string must not confuse the scan.
  const std::string tricky =
      R"({"topics":["flight","facility"],"note":"a } and a \" quote",)"
      R"("count":-3.5,"nothing":null})";
  expect_raw(json_raw_field(tricky, "topics"), R"(["flight","facility"])",
             "raw array");
  expect_raw(json_raw_field(tricky, "note"), R"("a } and a \" quote")",
             "string containing a brace and an escaped quote");
  expect_raw(json_raw_field(tricky, "count"), "-3.5", "negative decimal");
  expect_raw(json_raw_field(tricky, "nothing"), "null", "null value");

  expect_absent(json_raw_field("not json", "type"), "non-object input");
  expect_absent(json_raw_field(R"({"type":)", "type"), "truncated input");
  expect_raw(json_raw_field(R"(  { "type" : "gui_event" } )", "type"),
             "\"gui_event\"", "whitespace tolerant");

  expect_raw(json_unquote(R"("line\nbreak")"), "line\nbreak", "unquote escape");
  expect_raw(json_unquote(R"("")"), "", "unquote empty");
  expect_absent(json_unquote("7"), "unquote non-string");
  expect_absent(json_unquote(R"("unterminated)"), "unquote unterminated");

  if (g_failures != 0) {
    return EXIT_FAILURE;
  }
  return 0;
}
