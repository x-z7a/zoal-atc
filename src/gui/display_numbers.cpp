#include "zoal_atc/gui/display_numbers.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zoal_atc::gui {

namespace {

enum class Kind { none, digit, thousand, hundred, hundreds, point, keyword, tens, procedure };

struct Word {
  const char *key;
  Kind kind;
  int value;
};

// English (with the ICAO tree/fife/niner), Spanish and French. Words two
// languages share ("six") say the same number in both.
constexpr Word kWords[] = {
    {"zero", Kind::digit, 0},      {"one", Kind::digit, 1},
    {"two", Kind::digit, 2},       {"three", Kind::digit, 3},
    {"tree", Kind::digit, 3},      {"four", Kind::digit, 4},
    {"five", Kind::digit, 5},      {"fife", Kind::digit, 5},
    {"six", Kind::digit, 6},       {"seven", Kind::digit, 7},
    {"eight", Kind::digit, 8},     {"nine", Kind::digit, 9},
    {"niner", Kind::digit, 9},     {"cero", Kind::digit, 0},
    {"uno", Kind::digit, 1},       {"dos", Kind::digit, 2},
    {"tres", Kind::digit, 3},      {"cuatro", Kind::digit, 4},
    {"cinco", Kind::digit, 5},     {"seis", Kind::digit, 6},
    {"siete", Kind::digit, 7},     {"ocho", Kind::digit, 8},
    {"nueve", Kind::digit, 9},     {"z\xc3\xa9ro", Kind::digit, 0},
    {"un", Kind::digit, 1},        {"deux", Kind::digit, 2},
    {"trois", Kind::digit, 3},     {"quatre", Kind::digit, 4},
    {"cinq", Kind::digit, 5},      {"sept", Kind::digit, 7},
    {"huit", Kind::digit, 8},      {"neuf", Kind::digit, 9},
    {"thousand", Kind::thousand, 1000}, {"mil", Kind::thousand, 1000},
    {"mille", Kind::thousand, 1000},    {"hundred", Kind::hundred, 100},
    {"cent", Kind::hundred, 100},       {"cents", Kind::hundred, 100},
    {"doscientos", Kind::hundreds, 200},    {"trescientos", Kind::hundreds, 300},
    {"cuatrocientos", Kind::hundreds, 400}, {"quinientos", Kind::hundreds, 500},
    {"seiscientos", Kind::hundreds, 600},   {"setecientos", Kind::hundreds, 700},
    {"ochocientos", Kind::hundreds, 800},   {"novecientos", Kind::hundreds, 900},
    {"point", Kind::point, 0},     {"decimal", Kind::point, 0},
    {"punto", Kind::point, 0},     {"coma", Kind::point, 0},
    {"virgule", Kind::point, 0},   {"runway", Kind::keyword, 0},
    {"heading", Kind::keyword, 0}, {"level", Kind::keyword, 0},
    {"squawk", Kind::keyword, 0},  {"gate", Kind::keyword, 0},
    {"stand", Kind::keyword, 0},   {"pista", Kind::keyword, 0},
    {"rumbo", Kind::keyword, 0},   {"nivel", Kind::keyword, 0},
    {"piste", Kind::keyword, 0},   {"cap", Kind::keyword, 0},
    {"niveau", Kind::keyword, 0},
    {"ten", Kind::tens, 10},       {"eleven", Kind::tens, 11},
    {"twelve", Kind::tens, 12},     {"thirteen", Kind::tens, 13},
    {"fourteen", Kind::tens, 14},   {"fifteen", Kind::tens, 15},
    {"sixteen", Kind::tens, 16},    {"seventeen", Kind::tens, 17},
    {"eighteen", Kind::tens, 18},   {"nineteen", Kind::tens, 19},
    {"twenty", Kind::tens, 20},     {"thirty", Kind::tens, 30},
    {"forty", Kind::tens, 40},      {"fifty", Kind::tens, 50},
    {"sixty", Kind::tens, 60},      {"seventy", Kind::tens, 70},
    {"eighty", Kind::tens, 80},     {"ninety", Kind::tens, 90},
    {"departure", Kind::procedure, 0}, {"arrival", Kind::procedure, 0},
    {"salida", Kind::procedure, 0},   {"llegada", Kind::procedure, 0},
    {"d\xc3\xa9part", Kind::procedure, 0}, {"arriv\xc3\xa9" "e", Kind::procedure, 0},
};

struct Token {
  std::string_view gap;  // the whitespace before it
  std::string_view text;
  std::string_view prefix;
  std::string_view suffix;
  Kind kind = Kind::none;
  int value = 0;
  std::string_view core;  // the actual word without prefix/suffix
};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool is_prefix(char c) { return c == '(' || c == '"' || c == '\'' || c == '['; }

bool is_suffix(char c) {
  return c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?' ||
         c == ')' || c == '"' || c == '\'' || c == ']';
}

void classify(Token &t) {
  std::size_t begin = 0;
  while (begin < t.text.size() && is_prefix(t.text[begin])) {
    ++begin;
  }
  std::size_t end = t.text.size();
  while (end > begin && is_suffix(t.text[end - 1])) {
    --end;
  }
  t.prefix = t.text.substr(0, begin);
  t.suffix = t.text.substr(end);
  std::string key(t.text.substr(begin, end - begin));
  for (char &c : key) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  t.core = t.text.substr(begin, end - begin);
  for (const Word &w : kWords) {
    if (key == w.key) {
      t.kind = w.kind;
      t.value = w.value;
      return;
    }
  }
  
  // Handle hyphenated tens
  std::size_t dash_pos = key.find('-');
  if (dash_pos != std::string::npos && 
      key.find('-', dash_pos + 1) == std::string::npos) {
    std::string left = key.substr(0, dash_pos);
    std::string right = key.substr(dash_pos + 1);
    
    // Check if left is a tens word and right is a digit word
    int left_value = -1;
    int right_value = -1;
    
    for (const Word &w : kWords) {
      if (left == w.key && w.kind == Kind::tens) {
        left_value = w.value;
      }
      if (right == w.key && w.kind == Kind::digit) {
        right_value = w.value;
      }
    }
    
    if (left_value >= 20 && left_value <= 90 && left_value % 10 == 0 && 
        right_value >= 1 && right_value <= 9) {
      t.kind = Kind::tens;
      t.value = left_value + right_value;
    }
  }
}

class Reader {
 public:
  explicit Reader(const std::vector<Token> &tokens) : t_(tokens) {}

  bool is(std::size_t k, Kind kind) const { return k < t_.size() && t_[k].kind == kind; }

  // Token k continues a number that ends at k - 1: no punctuation between.
  bool linked(std::size_t k) const {
    return k > 0 && k < t_.size() && t_[k - 1].suffix.empty() && t_[k].prefix.empty();
  }

  // The digits of the linked digit words from k on; end is past the last.
  std::string run(std::size_t k, std::size_t &end, bool first_free) const {
    std::string digits;
    end = k;
    while ((is(end, Kind::digit) || is(end, Kind::tens)) && 
           ((first_free && end == k) || linked(end))) {
      if (is(end, Kind::digit)) {
        digits.push_back(static_cast<char>('0' + t_[end].value));
      } else {  // tens
        // Special case: if this is a multiple of ten and next token is a digit,
        // combine them into one two-digit number
        if (t_[end].value >= 20 && t_[end].value <= 90 && t_[end].value % 10 == 0 &&
            end + 1 < t_.size() && is(end + 1, Kind::digit) &&
            t_[end + 1].value >= 1 && linked(end + 1)) {
          digits += std::to_string(t_[end].value + t_[end + 1].value);
          ++end; // consume both tokens
        } else {
          digits += std::to_string(t_[end].value);
        }
      }
      ++end;
    }
    return digits;
  }

 private:
  const std::vector<Token> &t_;
};

std::uint64_t value_of(const std::string &digits) {
  std::uint64_t v = 0;
  for (const char c : digits) {
    v = v * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return v;
}

bool is_name(const Token &t) {
  if (!t.prefix.empty() || !t.suffix.empty() || t.core.size() < 3 ||
      t.core.size() > 6) {
    return false;
  }
  for (char c : t.core) {
    if (c < 'A' || c > 'Z') {
      return false;
    }
  }
  return t.prefix.empty() && t.suffix.empty();
}

// A number starting at token i: its text, and end past its last token.
// Returns false when token i does not start one.
bool parse_number(const std::vector<Token> &t, std::size_t i, std::string &out,
                  std::size_t &end, bool &glue) {
  const Reader r(t);
  std::size_t e = i;
  const std::string d1 = r.run(i, e, true);

  // A decimal: digits, point, digits.
  if (!d1.empty() && r.is(e, Kind::point) && r.linked(e)) {
    std::size_t e2 = e + 1;
    const std::string d2 = r.run(e + 1, e2, false);
    if (!d2.empty()) {
      out = d1 + "." + d2;
      end = e2;
      glue = false;
      return true;
    }
  }

  // A magnitude: [digits thousand] [digits hundred | hundreds-word].
  std::uint64_t total = 0;
  bool magnitude = false;
  std::size_t pos = e;
  std::string cur = d1;
  if (!cur.empty() && r.is(pos, Kind::thousand) && r.linked(pos)) {
    total += value_of(cur) * 1000;
    magnitude = true;
    ++pos;
    cur.clear();
  }
  std::size_t after = pos;
  std::string next = magnitude ? r.run(pos, after, false) : cur;
  if (!magnitude) {
    after = pos;
  }
  if (!next.empty() && r.is(after, Kind::hundred) && r.linked(after)) {
    total += value_of(next) * 100;
    magnitude = true;
    pos = after + 1;
  } else if (d1.empty() || magnitude) {
    if (r.is(pos, Kind::hundreds) && (pos == i || r.linked(pos))) {
      total += static_cast<std::uint64_t>(t[pos].value);
      magnitude = true;
      ++pos;
    }
  }
  if (magnitude) {
    out = std::to_string(total);
    end = pos;
    glue = false;
    return true;
  }

  // A plain run: two or more digits, or one after a word that takes a number.
  if (d1.size() >= 2 || (d1.size() == 1 && i > 0 && r.is(i - 1, Kind::keyword) &&
                         r.linked(i))) {
    out = d1;
    end = e;
    glue = false;
    return true;
  }
  
  // A SID or STAR: one digit before "departure"/"arrival", or after the name
  // in "llegada BOXUM cinco". On an all-capitals name it is written onto it.
  if (d1.size() == 1) {
    if (r.is(e, Kind::procedure) && r.linked(e)) {
      glue = i > 0 && is_name(t[i - 1]) && r.linked(i);
      out = d1;
      end = e;
      return true;
    }
    if (i >= 2 && is_name(t[i - 1]) && r.linked(i) &&
        r.is(i - 2, Kind::procedure) && r.linked(i - 1)) {
      glue = true;
      out = d1;
      end = e;
      return true;
    }
  }

  return false;
}


}  // namespace

std::string display_numbers(std::string_view text) {
  std::vector<Token> tokens;
  std::size_t i = 0;
  while (true) {
    const std::size_t gap_start = i;
    while (i < text.size() && is_space(text[i])) {
      ++i;
    }
    if (i == text.size()) {
      break;
    }
    const std::size_t word_start = i;
    while (i < text.size() && !is_space(text[i])) {
      ++i;
    }
    Token t;
    t.gap = text.substr(gap_start, word_start - gap_start);
    t.text = text.substr(word_start, i - word_start);
    classify(t);
    tokens.push_back(t);
  }
  std::size_t trailing = text.size();
  while (trailing > 0 && is_space(text[trailing - 1])) {
    --trailing;
  }

  std::string out;
  out.reserve(text.size());
  for (std::size_t k = 0; k < tokens.size();) {
    const Token &t = tokens[k];
    std::string number;
    std::size_t end = k;
    bool glue = false;
    
    // Call parse_number first, then append `t.gap` unless glue is true
    if ((t.kind == Kind::digit || t.kind == Kind::hundreds || t.kind == Kind::tens) &&
        parse_number(tokens, k, number, end, glue)) {
      if (!glue) {
        out += t.gap;
      }
      out += t.prefix;
      out += number;
      out += tokens[end - 1].suffix;
      k = end;
    } else {
      out += t.gap;
      out += t.text;
      ++k;
    }
  }
  out += text.substr(tokens.empty() ? 0 : trailing);
  return out;
}

}  // namespace zoal_atc::gui
