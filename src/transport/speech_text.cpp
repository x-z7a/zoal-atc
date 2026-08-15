#include "zoal_atc/transport/speech_text.hpp"

#include <cctype>
#include <cstddef>

namespace zoal_atc::transport {

namespace {

const char *const kDigitWords[10] = {"zero", "one",  "two",   "three", "four",
                                     "five", "six",  "seven", "eight", "niner"};

bool is_digit(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

std::string digit_speech(const std::string &digits) {
  std::string out;
  for (const char c : digits) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += kDigitWords[c - '0'];
  }
  return out;
}

// Expands a radio-frequency-shaped word — three digits starting with '1', a
// '.', then 1–3 decimal digits ("133.400") — into digit-by-digit speech with
// trailing decimal zeros dropped ("one three three point four"). Anything else
// (altitudes, visibilities, altimeter settings) is returned unchanged for the
// synth's natural number reading. A sentence-terminating '.' on the word is
// preserved.
std::string expand_word(const std::string &word, bool is_heading_value) {
  std::string core = word;
  std::string tail;
  if (!core.empty() && core.back() == '.') {
    tail = ".";
    core.pop_back();
  }

  if (is_heading_value && core.size() == 3 && is_digit(core[0]) &&
      is_digit(core[1]) && is_digit(core[2])) {
    return digit_speech(core) + tail;
  }

  const std::size_t dot = core.find('.');
  if (dot != 3 || core.size() < 5 || core.size() > 7 || core[0] != '1') {
    return word;
  }
  for (std::size_t i = 0; i < core.size(); ++i) {
    if (i != dot && !is_digit(core[i])) {
      return word;
    }
  }

  std::string decimal = core.substr(dot + 1);
  while (decimal.size() > 1 && decimal.back() == '0') {
    decimal.pop_back();
  }

  std::string out = digit_speech(core.substr(0, dot));
  out += " point";
  for (const char c : decimal) {
    out.push_back(' ');
    out += kDigitWords[c - '0'];
  }
  return out + tail;
}

}  // namespace

std::string speech_text(std::string_view reply) {
  std::string out;
  out.reserve(reply.size());
  std::string word;
  std::string previous_word;
  bool pending_space = false;

  const auto flush_word = [&] {
    if (word.empty()) {
      return;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out += expand_word(word, previous_word == "heading");
    previous_word = word;
    if (!previous_word.empty() && previous_word.back() == '.') {
      previous_word.pop_back();
    }
    for (char &c : previous_word) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    word.clear();
  };

  for (const char c : reply) {
    // Punctuation the synth would read as a word ("comma", "semicolon") becomes
    // a word gap so adjacent words stay separated without being run together.
    if (c == ',' || c == ';' || c == ':') {
      flush_word();
      pending_space = !out.empty();
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      flush_word();
      pending_space = !out.empty();
      continue;
    }
    word.push_back(c);
  }
  flush_word();
  return out;
}

}  // namespace zoal_atc::transport
