#ifndef ZOAL_ATC_TRANSPORT_ENDPOINT_CONFIG_HPP
#define ZOAL_ATC_TRANSPORT_ENDPOINT_CONFIG_HPP

#include "zoal_atc/transport/websocket_client.hpp"

#include <string>
#include <string_view>

namespace zoal_atc::transport {

struct EndpointConfigResult {
  bool ok = false;
  std::string error;
  WebSocketEndpoint endpoint;

  static EndpointConfigResult success(WebSocketEndpoint endpoint) {
    return {true, {}, std::move(endpoint)};
  }
  static EndpointConfigResult failure(std::string reason) {
    return {false, std::move(reason), {}};
  }
};

// Parses `ws://host[:port][/path]`, defaulting the port to 80 and the path to
// the console's `/plugin` handler. A bare `host[:port][/path]` is accepted and
// assumed `ws`.
//
// `wss://` is rejected rather than downgraded: WebSocketClient speaks plain
// TCP, so silently treating it as `ws://` would put push-to-talk audio on the
// wire in the clear while the config file claimed otherwise.
EndpointConfigResult parse_websocket_url(std::string_view url);

// Applies `key = value` lines over `base`. `#` comments, blank lines and CRLF
// endings are tolerated, and an unrecognized key is ignored so a config written
// for a newer plugin never stops an older one from starting. A malformed `url`
// is an error, because connecting to the previous endpoint instead of the
// requested one is a worse failure than not connecting.
EndpointConfigResult apply_config_text(std::string_view text,
                                       WebSocketEndpoint base);

// False when the token holds a character that would let it forge headers of
// its own once interpolated into the upgrade request.
bool token_is_safe(std::string_view token);

// Returns `text` with `key` set to `value`, for the one setting the panel is
// allowed to write. The file is the pilot's: their comments, their ordering and
// every key we did not touch survive, because a "save" that rewrote the file
// from our model of it would silently discard whatever they had put there.
//
// In priority order it rewrites the first live `key =` line, else uncomments the
// template's commented one in place, else appends. Uncommenting in place matters
// because the template explains each key directly above it, and an appended
// duplicate would leave the explanation attached to a line that no longer has
// any effect.
//
// The value is written verbatim, so callers validate first - token_is_safe for
// the token. An empty value is legitimate and means "clear".
std::string upsert_config_value(std::string_view text, std::string_view key,
                                std::string_view value);

// The commented config file the plugin writes on first run, so the file is
// discoverable at the path that reads it rather than only in the README.
//
// Every key is commented out. Creating the file must not move the console:
// an untouched template has to leave the resolved endpoint exactly as it was.
std::string default_config_template();

// The value for the HTTP `Host` header: the port is omitted when it is the
// scheme default, and an IPv6 literal is re-bracketed. Name-based virtual
// hosts (Cloudflare included) key on the exact string.
std::string host_header(const WebSocketEndpoint &endpoint);

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_ENDPOINT_CONFIG_HPP
