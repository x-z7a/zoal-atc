#include "zoal_atc/transport/json_util.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace zoal_atc::transport {

namespace {

void skip_ws(const std::string &json, std::size_t &pos) {
  while (pos < json.size() &&
         std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
}

std::optional<std::size_t> field_value_pos(const std::string &json,
                                           std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t pos = 0;
  while ((pos = json.find(needle, pos)) != std::string::npos) {
    pos += needle.size();
    skip_ws(json, pos);
    if (pos < json.size() && json[pos] == ':') {
      ++pos;
      skip_ws(json, pos);
      return pos;
    }
  }
  return std::nullopt;
}

bool parse_string_at(const std::string &json, std::size_t &pos,
                     std::string &out) {
  if (pos >= json.size() || json[pos] != '"') {
    return false;
  }
  ++pos;
  out.clear();
  while (pos < json.size()) {
    const char ch = json[pos++];
    if (ch == '"') {
      return true;
    }
    if (ch != '\\') {
      out.push_back(ch);
      continue;
    }
    if (pos >= json.size()) {
      return false;
    }
    const char escaped = json[pos++];
    switch (escaped) {
    case '"':
    case '\\':
    case '/':
      out.push_back(escaped);
      break;
    case 'b':
      out.push_back('\b');
      break;
    case 'f':
      out.push_back('\f');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'u':
      if (pos + 4U > json.size()) {
        return false;
      }
      pos += 4U;
      out.push_back('?');
      break;
    default:
      return false;
    }
  }
  return false;
}

// skip_value advances `pos` past one complete JSON value, returning false when
// the text runs out or is malformed. Strings are stepped through rather than
// scanned for, so a brace or bracket inside one cannot unbalance the count.
bool skip_value(const std::string &json, std::size_t &pos) {
  skip_ws(json, pos);
  if (pos >= json.size()) {
    return false;
  }
  const char first = json[pos];
  if (first == '"') {
    std::string ignored;
    return parse_string_at(json, pos, ignored);
  }
  if (first == '{' || first == '[') {
    const char open = first;
    const char close = (open == '{') ? '}' : ']';
    int depth = 0;
    while (pos < json.size()) {
      const char ch = json[pos];
      if (ch == '"') {
        std::string ignored;
        if (!parse_string_at(json, pos, ignored)) {
          return false;
        }
        continue;
      }
      ++pos;
      if (ch == open) {
        ++depth;
      } else if (ch == close) {
        --depth;
        if (depth == 0) {
          return true;
        }
      }
    }
    return false;
  }
  // Numbers, true/false/null: everything up to the next structural character.
  const std::size_t start = pos;
  while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
         json[pos] != ']' &&
         std::isspace(static_cast<unsigned char>(json[pos])) == 0) {
    ++pos;
  }
  return pos > start;
}

} // namespace

std::optional<std::string> json_raw_field(const std::string &json,
                                          std::string_view key) {
  std::size_t pos = 0;
  skip_ws(json, pos);
  if (pos >= json.size() || json[pos] != '{') {
    return std::nullopt;
  }
  ++pos;
  for (;;) {
    skip_ws(json, pos);
    if (pos >= json.size() || json[pos] == '}') {
      return std::nullopt;
    }
    std::string field;
    if (!parse_string_at(json, pos, field)) {
      return std::nullopt;
    }
    skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') {
      return std::nullopt;
    }
    ++pos;
    skip_ws(json, pos);
    const std::size_t value_start = pos;
    if (!skip_value(json, pos)) {
      return std::nullopt;
    }
    if (field == key) {
      return json.substr(value_start, pos - value_start);
    }
    skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != ',') {
      return std::nullopt;
    }
    ++pos;
  }
}

std::optional<std::string> json_unquote(const std::string &raw) {
  std::size_t pos = 0;
  skip_ws(raw, pos);
  std::string value;
  if (!parse_string_at(raw, pos, value)) {
    return std::nullopt;
  }
  return value;
}

std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  constexpr char kHex[] = "0123456789abcdef";
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    switch (ch) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (ch < 0x20U) {
        escaped += "\\u00";
        escaped.push_back(kHex[(ch >> 4U) & 0x0FU]);
        escaped.push_back(kHex[ch & 0x0FU]);
      } else {
        escaped.push_back(static_cast<char>(ch));
      }
      break;
    }
  }
  return escaped;
}

std::string json_number(double value) {
  if (!std::isfinite(value)) {
    return "0";
  }
  char buffer[64] = {};
  // %.9g keeps lat/lon precision (~1e-7 deg) while staying compact for the
  // common small magnitudes; JSON has no issue with exponent forms but the Go
  // decoder accepts either.
  std::snprintf(buffer, sizeof(buffer), "%.9g", value);
  return buffer;
}

std::optional<std::string> json_string_field(const std::string &json,
                                             std::string_view key) {
  auto pos = field_value_pos(json, key);
  if (!pos.has_value()) {
    return std::nullopt;
  }
  std::string value;
  if (!parse_string_at(json, *pos, value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<bool> json_bool_field(const std::string &json,
                                    std::string_view key) {
  auto pos = field_value_pos(json, key);
  if (!pos.has_value()) {
    return std::nullopt;
  }
  if (json.compare(*pos, 4U, "true") == 0) {
    return true;
  }
  if (json.compare(*pos, 5U, "false") == 0) {
    return false;
  }
  return std::nullopt;
}

std::optional<std::uint64_t> json_uint_field(const std::string &json,
                                             std::string_view key) {
  auto pos = field_value_pos(json, key);
  if (!pos.has_value()) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  bool saw_digit = false;
  while (*pos < json.size()) {
    const auto ch = static_cast<unsigned char>(json[*pos]);
    if (ch < static_cast<unsigned char>('0') ||
        ch > static_cast<unsigned char>('9')) {
      break;
    }
    saw_digit = true;
    value = (value * 10U) +
            static_cast<std::uint64_t>(ch - static_cast<unsigned char>('0'));
    ++(*pos);
  }
  if (!saw_digit) {
    return std::nullopt;
  }
  return value;
}

std::optional<double> json_number_field(const std::string &json,
                                        std::string_view key) {
  auto pos = field_value_pos(json, key);
  if (!pos.has_value()) {
    return std::nullopt;
  }
  std::size_t start = *pos;
  std::size_t end = start;
  bool saw_digit = false;
  if (end < json.size() && (json[end] == '-' || json[end] == '+')) {
    ++end;
  }
  while (end < json.size()) {
    const char ch = json[end];
    if (ch >= '0' && ch <= '9') {
      saw_digit = true;
      ++end;
    } else if (ch == '.' || ch == 'e' || ch == 'E' || ch == '-' || ch == '+') {
      ++end;
    } else {
      break;
    }
  }
  if (!saw_digit) {
    return std::nullopt;
  }
  try {
    return std::stod(json.substr(start, end - start));
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<std::string> json_string_array_field(const std::string &json,
                                                 std::string_view key) {
  std::vector<std::string> out;
  auto pos = field_value_pos(json, key);
  if (!pos.has_value() || *pos >= json.size() || json[*pos] != '[') {
    return out;
  }
  std::size_t i = *pos + 1; // past '['
  while (i < json.size()) {
    skip_ws(json, i);
    if (i < json.size() && json[i] == ']') {
      break;
    }
    std::string value;
    if (!parse_string_at(json, i, value)) {
      break; // not a string element; stop (best-effort)
    }
    out.push_back(std::move(value));
    skip_ws(json, i);
    if (i < json.size() && json[i] == ',') {
      ++i;
      continue;
    }
    break;
  }
  return out;
}

} // namespace zoal_atc::transport
