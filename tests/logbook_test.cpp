#include "zoal_atc/gui/logbook.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

std::string slurp(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::size_t logs_in(const std::filesystem::path &dir) {
  std::size_t n = 0;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() == ".log") {
      ++n;
    }
  }
  return n;
}

} // namespace

int main() {
  using zoal_atc::gui::Logbook;
  const auto root = std::filesystem::temp_directory_path() / "zoal-atc-logbook-test";
  std::filesystem::remove_all(root);

  Logbook book(root.string());
  book.set_callsign("ACA4096");
  book.set_language("es");

  // A controller line heard in Spanish: the Spanish under es/, its English in
  // the English log.
  require(book.record(R"({"kind":"atc","text":"ACA4096, colación correcta.","english":"ACA4096, readback correct.","language":"es","station":"Toronto Clearance","at":"2026-11-22T22:51:05.123Z"})"),
          "a line is recorded");
  // A pilot call by button: the same, the other way round.
  require(book.record(R"({"kind":"pilot","text":"Toronto Clearance, ACA4096, solicito rodaje","english":"Toronto Clearance, ACA4096, request taxi","at":"2026-11-22T22:51:20Z"})"),
          "the pilot's line is recorded");
  // A line with no English beside it is English already.
  require(book.record(R"({"kind":"atc","text":"ACA4096, roger.","station":"Toronto Clearance","at":"2026-11-22T22:52:00Z"})"),
          "an English line is recorded");
  // Delivered again: not written twice.
  require(book.record(R"({"kind":"atc","text":"ACA4096, colación correcta.","english":"ACA4096, readback correct.","language":"es","station":"Toronto Clearance","at":"2026-11-22T22:51:05.123Z"})"),
          "a redelivery is accepted");
  require(!book.record(R"({"kind":"atc","at":"2026-11-22T22:52:00Z"})"), "a payload with no words is no line");

  const std::string english = slurp(root / "ACA4096-2026-11-22.log");
  const std::string spanish = slurp(root / "es" / "ACA4096-2026-11-22.log");
  require(english ==
              "[22:51:05Z] Toronto Clearance: ACA4096, readback correct.\n"
              "[22:51:20Z] ACA4096: Toronto Clearance, ACA4096, request taxi\n"
              "[22:52:00Z] Toronto Clearance: ACA4096, roger.\n",
          "the English log:\n" + english);
  require(spanish ==
              "[22:51:05Z] Toronto Clearance: ACA4096, colación correcta.\n"
              "[22:51:20Z] ACA4096: Toronto Clearance, ACA4096, solicito rodaje\n"
              "[22:52:00Z] Toronto Clearance: ACA4096, roger.\n",
          "the Spanish log:\n" + spanish);

  // English only for a pilot who hears English.
  Logbook english_only((root / "en-only").string());
  english_only.set_callsign("DAL141");
  english_only.record(R"({"kind":"atc","text":"DAL141, roger.","at":"2026-11-22T10:00:00Z"})");
  require(std::filesystem::exists(root / "en-only" / "DAL141-2026-11-22.log"), "the English log");
  require(!std::filesystem::exists(root / "en-only" / "en"), "and no language folder");

  // Only the newest files are kept.
  Logbook kept((root / "kept").string());
  kept.set_keep(3);
  for (int day = 10; day < 16; ++day) {
    kept.record("{\"kind\":\"atc\",\"text\":\"AC21, roger.\",\"at\":\"2026-11-" +
                std::to_string(day) + "T10:00:00Z\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  require(logs_in(root / "kept") == 3, "three files kept");
  require(std::filesystem::exists(root / "kept" / "flight-2026-11-15.log"), "the newest kept");
  require(!std::filesystem::exists(root / "kept" / "flight-2026-11-10.log"), "the oldest removed");
  kept.set_keep(1);
  require(logs_in(root / "kept") == 1, "a smaller number takes effect at once");
  kept.set_keep(0);
  kept.record(R"({"kind":"atc","text":"AC21, roger.","at":"2026-11-20T10:00:00Z"})");
  require(logs_in(root / "kept") == 2, "zero keeps every file");

  require(zoal_atc::gui::logbook_file_name("a/c 21", "2026-11-22") == "AC21-2026-11-22.log",
          "a callsign is made safe for a file name");

  std::filesystem::remove_all(root);
  std::cout << "logbook tests passed\n";
  return 0;
}
