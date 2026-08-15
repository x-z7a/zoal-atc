#include "zoal_atc/transport/endpoint_config.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace zoal_atc::transport {
namespace {

constexpr std::uint16_t kDefaultWsPort = 80;
constexpr const char *kDefaultPath = "/plugin";

std::string to_lower(std::string_view text) {
  std::string lowered(text);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return lowered;
}

std::string_view trim(std::string_view text) {
  const auto is_space = [](unsigned char c) {
    return std::isspace(c) != 0;
  };
  while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

// Splits `authority` into host and optional port text, honouring the bracket
// form so an IPv6 literal's own colons are not read as a port separator.
bool split_authority(std::string_view authority, std::string &host,
                     std::string_view &port_text, std::string &error) {
  if (!authority.empty() && authority.front() == '[') {
    const auto close = authority.find(']');
    if (close == std::string_view::npos) {
      error = "unterminated IPv6 literal in `" + std::string(authority) + "`";
      return false;
    }
    host = std::string(authority.substr(1, close - 1));
    const auto rest = authority.substr(close + 1);
    if (rest.empty()) {
      port_text = {};
      return true;
    }
    if (rest.front() != ':') {
      error = "unexpected text after IPv6 literal in `" +
              std::string(authority) + "`";
      return false;
    }
    port_text = rest.substr(1);
    return true;
  }

  const auto colon = authority.rfind(':');
  if (colon == std::string_view::npos) {
    host = std::string(authority);
    port_text = {};
    return true;
  }
  host = std::string(authority.substr(0, colon));
  port_text = authority.substr(colon + 1);
  return true;
}

bool parse_port(std::string_view text, std::uint16_t &port,
                std::string &error) {
  if (text.empty()) {
    error = "empty port";
    return false;
  }
  unsigned long value = 0;
  for (const char c : text) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      error = "port `" + std::string(text) + "` is not a number";
      return false;
    }
    value = value * 10U + static_cast<unsigned long>(c - '0');
    if (value > 65535U) {
      error = "port `" + std::string(text) + "` is out of range";
      return false;
    }
  }
  if (value == 0U) {
    error = "port 0 is not a port";
    return false;
  }
  port = static_cast<std::uint16_t>(value);
  return true;
}

} // namespace

namespace {

// True when `line` assigns `key`, by exactly the rules apply_config_text reads
// it with: the first `=` or `:` separates, and the key is compared lowercased
// and trimmed. Sharing the rule is the point - a writer that disagreed with the
// parser about what a key line is would edit one line while the plugin obeyed
// another.
bool assigns_key(std::string_view line, std::string_view key) {
  const auto separator = line.find_first_of("=:");
  if (separator == std::string_view::npos) {
    return false;
  }
  return to_lower(trim(line.substr(0, separator))) == key;
}

// Strips leading comment markers and space, so `# token = x` and `#token=x`
// both read as a commented assignment of `token`.
std::string_view uncomment(std::string_view line) {
  while (!line.empty() && (line.front() == '#' || line.front() == ';')) {
    line.remove_prefix(1);
  }
  return trim(line);
}

} // namespace

std::string upsert_config_value(std::string_view text, std::string_view key,
                                std::string_view value) {
  const std::string wanted = to_lower(trim(key));
  const std::string assignment = wanted + " = " + std::string(value);

  // Split into content and line ending separately, so a rewritten line keeps
  // the ending it had and a CRLF file does not gain a lone LF.
  std::vector<std::string> lines;
  std::vector<std::string> endings;
  for (std::size_t pos = 0; pos < text.size();) {
    const auto newline = text.find('\n', pos);
    if (newline == std::string_view::npos) {
      lines.emplace_back(text.substr(pos));
      endings.emplace_back("");
      break;
    }
    std::size_t content_end = newline;
    std::string ending = "\n";
    if (content_end > pos && text[content_end - 1] == '\r') {
      content_end -= 1;
      ending = "\r\n";
    }
    lines.emplace_back(text.substr(pos, content_end - pos));
    endings.emplace_back(std::move(ending));
    pos = newline + 1;
  }

  constexpr auto kNone = std::string_view::npos;
  std::size_t live = kNone;
  std::size_t commented = kNone;
  for (std::size_t i = 0; i < lines.size() && live == kNone; ++i) {
    const std::string_view line = trim(lines[i]);
    if (line.empty()) {
      continue;
    }
    if (line.front() == '#' || line.front() == ';') {
      if (commented == kNone && assigns_key(uncomment(line), wanted)) {
        commented = i;
      }
      continue;
    }
    if (assigns_key(line, wanted)) {
      live = i;
    }
  }

  const std::size_t target = live != kNone ? live : commented;
  if (target != kNone) {
    lines[target] = assignment;
  } else {
    // A file that did not end in a newline would otherwise have the new key
    // glued onto its last line.
    if (!endings.empty() && endings.back().empty()) {
      endings.back() = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    }
    lines.push_back(assignment);
    endings.emplace_back(text.find("\r\n") != std::string_view::npos ? "\r\n"
                                                                    : "\n");
  }
  // We are writing the file, so it ends like a text file should.
  if (!endings.empty() && endings.back().empty()) {
    endings.back() = "\n";
  }

  std::string out;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    out.append(lines[i]).append(endings[i]);
  }
  return out;
}

// The token is interpolated into an HTTP header, so a stray control character
// would let its source append headers of its own.
bool token_is_safe(std::string_view token) {
  return std::none_of(token.begin(), token.end(), [](char c) {
    const auto value = static_cast<unsigned char>(c);
    return value < 0x20U || value == 0x7FU;
  });
}

EndpointConfigResult parse_websocket_url(std::string_view url) {
  const std::string_view trimmed = trim(url);
  if (trimmed.empty()) {
    return EndpointConfigResult::failure("empty console URL");
  }

  std::string_view rest = trimmed;
  const auto scheme_end = rest.find("://");
  if (scheme_end != std::string_view::npos) {
    const std::string scheme = to_lower(rest.substr(0, scheme_end));
    if (scheme == "wss" || scheme == "https") {
      return EndpointConfigResult::failure(
          "`" + scheme +
          "://` needs TLS, which this plugin does not speak yet; use `ws://` "
          "(the console is reachable on port 80)");
    }
    if (scheme != "ws") {
      return EndpointConfigResult::failure("unsupported scheme `" + scheme +
                                           "://`; expected `ws://`");
    }
    rest.remove_prefix(scheme_end + 3);
  }
  if (rest.empty()) {
    return EndpointConfigResult::failure("no host in `" + std::string(trimmed) +
                                         "`");
  }

  std::string_view authority = rest;
  std::string path(kDefaultPath);
  // An IPv6 literal is scanned from its closing bracket so the `/` search
  // cannot land inside the address.
  std::size_t search_from = 0;
  if (rest.front() == '[') {
    const auto close = rest.find(']');
    if (close == std::string_view::npos) {
      return EndpointConfigResult::failure("unterminated IPv6 literal in `" +
                                           std::string(trimmed) + "`");
    }
    search_from = close;
  }
  const auto path_start = rest.find('/', search_from);
  if (path_start != std::string_view::npos) {
    authority = rest.substr(0, path_start);
    path = std::string(rest.substr(path_start));
  }

  WebSocketEndpoint endpoint;
  std::string_view port_text;
  std::string error;
  if (!split_authority(authority, endpoint.host, port_text, error)) {
    return EndpointConfigResult::failure(error);
  }
  if (endpoint.host.empty()) {
    return EndpointConfigResult::failure("no host in `" + std::string(trimmed) +
                                         "`");
  }

  endpoint.port = kDefaultWsPort;
  if (!port_text.empty() && !parse_port(port_text, endpoint.port, error)) {
    return EndpointConfigResult::failure(error);
  }
  endpoint.path = std::move(path);
  return EndpointConfigResult::success(std::move(endpoint));
}

EndpointConfigResult apply_config_text(std::string_view text,
                                       WebSocketEndpoint base) {
  std::size_t line_start = 0;
  while (line_start <= text.size()) {
    const auto newline = text.find('\n', line_start);
    const auto line_end = newline == std::string_view::npos ? text.size()
                                                            : newline;
    const std::string_view line =
        trim(text.substr(line_start, line_end - line_start));
    line_start = line_end + 1;

    if (line.empty() || line.front() == '#' || line.front() == ';') {
      if (newline == std::string_view::npos) {
        break;
      }
      continue;
    }

    const auto separator = line.find_first_of("=:");
    if (separator == std::string_view::npos) {
      if (newline == std::string_view::npos) {
        break;
      }
      continue;
    }

    const std::string key = to_lower(trim(line.substr(0, separator)));
    // Only the first separator splits: a base64 token keeps its `=` padding.
    const std::string value(trim(line.substr(separator + 1)));

    if (key == "url" || key == "console_url") {
      auto parsed = parse_websocket_url(value);
      if (!parsed.ok) {
        return EndpointConfigResult::failure("config key `" + key + "`: " +
                                             parsed.error);
      }
      parsed.endpoint.auth_token = base.auth_token;
      // Carry the identity across the rebuild too, or key order in the file
      // decides whether the plugin has one (phase22 M1).
      parsed.endpoint.client_id = base.client_id;
      parsed.endpoint.simbrief_id = base.simbrief_id;
      base = std::move(parsed.endpoint);
    } else if (key == "token" || key == "auth_token") {
      if (!token_is_safe(value)) {
        return EndpointConfigResult::failure(
            "config key `" + key + "`: token contains a control character");
      }
      base.auth_token = value;
    } else if (key == "client_id") {
      // Interpolated into a JSON frame, so the same character fence the token
      // gets: a quote or control character here would forge fields of its own.
      if (!token_is_safe(value) ||
          value.find('"') != std::string::npos ||
          value.find('\\') != std::string::npos) {
        return EndpointConfigResult::failure(
            "config key `client_id`: contains an unsafe character");
      }
      base.client_id = value;
    } else if (key == "simbrief_id" || key == "simbrief_pilot_id") {
      // Interpolated into a JSON frame like client_id, so the same fence.
      if (!token_is_safe(value) || value.find('"') != std::string::npos ||
          value.find('\\') != std::string::npos) {
        return EndpointConfigResult::failure(
            "config key `" + key + "`: contains an unsafe character");
      }
      base.simbrief_id = value;
    }
    // Any other key belongs to a version of this plugin that is not us.

    if (newline == std::string_view::npos) {
      break;
    }
  }
  return EndpointConfigResult::success(std::move(base));
}

std::string default_config_template() {
  return
      "# zoal-atc plugin configuration\n"
      "#\n"
      "# Where the ATC console is. Uncomment `url` to point the plugin at a\n"
      "# console that is not on this machine; leave it commented to keep the\n"
      "# default, ws://127.0.0.1:8765/plugin.\n"
      "#\n"
      "# Omitting the port means the scheme default (80), not 8765.\n"
      "# wss:// is not supported: this plugin has no TLS layer and refuses to\n"
      "# connect rather than quietly sending your audio in the clear.\n"
      "#\n"
      "# url = ws://atc.zoal.app/plugin\n"
      "\n"
      "# Shared secret for a console that requires one. It is sent as an\n"
      "# Authorization: Bearer header, and must match ZOAL_ATC_PLUGIN_TOKEN in\n"
      "# the console's environment. Over ws:// it crosses the network in\n"
      "# cleartext, so treat it as a gate against strangers finding an open\n"
      "# endpoint, not as transport security.\n"
      "#\n"
      "# You can also set this in the sim, on the in-sim panel's Settings tab.\n"
      "# Saving there rewrites this line and reconnects, so you can see whether\n"
      "# the console accepts it. Editing here works too; the plugin reads this\n"
      "# file at startup.\n"
      "#\n"
      "# token = your-shared-secret\n"
      "\n"
      "# Identity for this plugin installation. The plugin generates one on\n"
      "# first run and appends it below, so you do not need to set this. The\n"
      "# console uses it to tell connected aircraft apart; copying it to\n"
      "# another machine makes both look like the same aircraft.\n"
      "#\n"
      "# client_id = 0123456789abcdef\n"
      "\n"
      "# Your SimBrief pilot ID. The console fetches this aircraft's flight\n"
      "# plan with it, which is how a pilot flying against someone else's\n"
      "# console gets an OFP at all - there is no UI on your side to type\n"
      "# it into. Leave it unset to fly without one.\n"
      "#\n"
      "# simbrief_id = your-simbrief-id\n";
}

std::string host_header(const WebSocketEndpoint &endpoint) {
  const bool ipv6 = endpoint.host.find(':') != std::string::npos;
  std::string host = ipv6 ? "[" + endpoint.host + "]" : endpoint.host;
  if (endpoint.port == kDefaultWsPort) {
    return host;
  }
  return host + ":" + std::to_string(endpoint.port);
}

} // namespace zoal_atc::transport
