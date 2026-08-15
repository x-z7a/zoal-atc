#include "zoal_atc/transport/endpoint_config.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

using zoal_atc::transport::apply_config_text;
using zoal_atc::transport::default_config_template;
using zoal_atc::transport::EndpointConfigResult;
using zoal_atc::transport::host_header;
using zoal_atc::transport::parse_websocket_url;
using zoal_atc::transport::upsert_config_value;
using zoal_atc::transport::WebSocketEndpoint;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void require_eq(const std::string &got, const std::string &want,
                const std::string &message) {
  if (got != want) {
    std::cerr << "FAIL: " << message << "\n  got:  `" << got << "`\n  want: `"
              << want << "`\n";
    std::exit(1);
  }
}

void require_eq(std::uint16_t got, std::uint16_t want,
                const std::string &message) {
  if (got != want) {
    std::cerr << "FAIL: " << message << "\n  got:  " << got
              << "\n  want: " << want << "\n";
    std::exit(1);
  }
}

EndpointConfigResult ok_url(const std::string &url) {
  const auto result = parse_websocket_url(url);
  require(result.ok, "expected `" + url + "` to parse: " + result.error);
  return result;
}

void reject_url(const std::string &url, const std::string &why) {
  const auto result = parse_websocket_url(url);
  require(!result.ok, "expected `" + url + "` to be rejected (" + why + ")");
  require(!result.error.empty(), "rejection of `" + url + "` needs a reason");
}

}  // namespace

int main() {
  // Where a plugin nobody configured connects. The console is hosted, and the
  // plugin is the only artifact a pilot downloads, so a loopback default would
  // point every install at a machine that is not running a console -- and the
  // panel's URL is read-only, so the only way out would be hand-editing the
  // very file the panel exists to spare them.
  {
    const WebSocketEndpoint fresh;
    require_eq(fresh.host, "atc.zoal.app", "default host is the hosted console");
    require_eq(fresh.port, 80, "default port is the scheme default");
    require_eq(fresh.path, "/plugin", "default path is the console handler");
    require(fresh.auth_token.empty(), "no token by default");
    require_eq(host_header(fresh), "atc.zoal.app",
               "default endpoint needs no port in the Host header");
  }

  // The deployed console: no port in the URL means the scheme default (80),
  // not the loopback development port.
  {
    const auto parsed = ok_url("ws://atc.zoal.app/plugin");
    require_eq(parsed.endpoint.host, "atc.zoal.app", "host parsed");
    require_eq(parsed.endpoint.port, 80, "scheme default port");
    require_eq(parsed.endpoint.path, "/plugin", "path parsed");
  }

  // The loopback form the plugin shipped with must keep working verbatim.
  {
    const auto parsed = ok_url("ws://127.0.0.1:8765/plugin");
    require_eq(parsed.endpoint.host, "127.0.0.1", "loopback host");
    require_eq(parsed.endpoint.port, 8765, "explicit port wins");
    require_eq(parsed.endpoint.path, "/plugin", "loopback path");
  }

  // A URL with no path still lands on the console's handler rather than `/`,
  // which answers 404.
  require_eq(ok_url("ws://atc.zoal.app").endpoint.path, "/plugin",
             "path defaults to the console handler");
  require_eq(ok_url("ws://atc.zoal.app:8765").endpoint.path, "/plugin",
             "path defaults with an explicit port too");

  // A bare host is a kindness to anyone typing into the config file.
  {
    const auto parsed = ok_url("atc.zoal.app/plugin");
    require_eq(parsed.endpoint.host, "atc.zoal.app", "bare host parsed");
    require_eq(parsed.endpoint.port, 80, "bare host assumes ws default");
  }

  // Scheme is case-insensitive; a trailing slash is a real (root) path.
  require_eq(ok_url("WS://atc.zoal.app/plugin").endpoint.host, "atc.zoal.app",
             "uppercase scheme accepted");
  require_eq(ok_url("ws://atc.zoal.app/").endpoint.path, "/",
             "explicit root path preserved");

  // IPv6 literals keep their brackets out of the host but their colons in.
  {
    const auto parsed = ok_url("ws://[::1]:8765/plugin");
    require_eq(parsed.endpoint.host, "::1", "IPv6 host unbracketed");
    require_eq(parsed.endpoint.port, 8765, "IPv6 port parsed");
  }
  require_eq(ok_url("ws://[::1]/plugin").endpoint.port, 80,
             "IPv6 default port");

  // `wss://` must fail loudly. This client speaks plain TCP, so quietly
  // treating it as `ws://` would downgrade the transport without telling
  // anyone -- the one outcome worse than refusing to connect.
  {
    const auto result = parse_websocket_url("wss://atc.zoal.app/plugin");
    require(!result.ok, "wss:// rejected");
    require(result.error.find("wss") != std::string::npos,
            "wss:// rejection names the scheme; got: " + result.error);
  }

  reject_url("http://atc.zoal.app/plugin", "wrong scheme");
  reject_url("", "empty");
  reject_url("ws://", "no host");
  reject_url("ws:///plugin", "no host");
  reject_url("ws://atc.zoal.app:notaport/plugin", "non-numeric port");
  reject_url("ws://atc.zoal.app:0/plugin", "port zero");
  reject_url("ws://atc.zoal.app:70000/plugin", "port out of range");
  reject_url("ws://[::1/plugin", "unterminated IPv6 literal");

  // --- config file ---------------------------------------------------------

  // The file the user actually writes: a URL, a token, comments, and the
  // blank lines and CRLF endings a Windows editor leaves behind.
  {
    const std::string text =
        "# zoal-atc console\r\n"
        "\r\n"
        "url = ws://atc.zoal.app/plugin\r\n"
        "token = s3cr3t-value\r\n";
    const auto result = apply_config_text(text, WebSocketEndpoint{});
    require(result.ok, "config parsed: " + result.error);
    require_eq(result.endpoint.host, "atc.zoal.app", "config host");
    require_eq(result.endpoint.port, 80, "config port");
    require_eq(result.endpoint.path, "/plugin", "config path");
    require_eq(result.endpoint.auth_token, "s3cr3t-value", "config token");
  }

  // Keys are case-insensitive and tolerate missing spaces around `=`.
  {
    const auto result =
        apply_config_text("URL=ws://host:9000/x\nToken:abc\n", WebSocketEndpoint{});
    require(result.ok, "loose syntax parsed: " + result.error);
    require_eq(result.endpoint.host, "host", "loose host");
    require_eq(result.endpoint.port, 9000, "loose port");
    require_eq(result.endpoint.auth_token, "abc", "loose token");
  }

  // A token containing `=` (base64 padding is everywhere) survives intact:
  // only the first separator splits the line.
  require_eq(apply_config_text("token = YWJjZA==\n", WebSocketEndpoint{})
                 .endpoint.auth_token,
             "YWJjZA==", "token keeps base64 padding");

  // An unknown key is ignored rather than fatal, so a config written for a
  // newer plugin never stops an older one from starting.
  {
    const auto result = apply_config_text(
        "future_option = 1\ntoken = keep-me\n", WebSocketEndpoint{});
    require(result.ok, "unknown key ignored: " + result.error);
    require_eq(result.endpoint.auth_token, "keep-me", "token still applied");
  }

  // Phase 22 M1: the plugin carries a stable identity of its own, so the
  // console can tell two aircraft apart. It is the airframe's identity, not the
  // flight's — the analogue of a Mode S address rather than a callsign.
  {
    const auto result =
        apply_config_text("client_id = 3f1a-aaaa\n", WebSocketEndpoint{});
    require(result.ok, "client_id parsed: " + result.error);
    require_eq(result.endpoint.client_id, "3f1a-aaaa", "client_id applied");
  }

  // A url line rebuilds the endpoint from the parsed URL. The identity must
  // survive that rebuild, exactly as the token already does — otherwise key
  // order in the file silently decides whether the plugin has an identity.
  {
    const auto result = apply_config_text(
        "client_id = keep-me\nurl = ws://atc.zoal.app/plugin\n",
        WebSocketEndpoint{});
    require(result.ok, "client_id before url: " + result.error);
    require_eq(result.endpoint.client_id, "keep-me",
               "client_id survives a url line");
    require_eq(result.endpoint.host, "atc.zoal.app", "url still applied");
  }

  // The identity is interpolated into a JSON frame, so a quote or control
  // character in it would let the file forge fields of its own.
  {
    const auto result =
        apply_config_text("client_id = bad\"id\n", WebSocketEndpoint{});
    require(!result.ok, "unsafe client_id rejected");
  }

  // A token-only config leaves the base endpoint untouched.
  {
    WebSocketEndpoint base;
    base.host = "127.0.0.1";
    base.port = 8765;
    const auto result = apply_config_text("token = t\n", base);
    require(result.ok, "token-only config: " + result.error);
    require_eq(result.endpoint.host, "127.0.0.1", "base host retained");
    require_eq(result.endpoint.port, 8765, "base port retained");
  }

  // A bad URL in the file is an error, not a silent fall back to loopback:
  // connecting somewhere other than where the user asked is a worse failure
  // than not connecting.
  {
    const auto result =
        apply_config_text("url = wss://atc.zoal.app/plugin\n", WebSocketEndpoint{});
    require(!result.ok, "bad config url rejected");
    require(result.error.find("url") != std::string::npos,
            "config error names the offending key; got: " + result.error);
  }

  // The token is interpolated into an HTTP header, so a control character in
  // the config file must not become a header of the config file's choosing.
  {
    const auto result = apply_config_text("token = abc\rX-Evil: 1\n",
                                          WebSocketEndpoint{});
    require(!result.ok, "control character in token rejected");
  }

  // An empty value clears rather than corrupts.
  require(apply_config_text("token =\n", WebSocketEndpoint{})
              .endpoint.auth_token.empty(),
          "empty token value yields no token");

  // --- first-run template --------------------------------------------------

  // The template the plugin drops next to X-Plane's preferences on first run.
  // Every key is commented out, so a user who never edits the file connects
  // exactly where they did before it appeared. Writing a config file must not
  // be a behaviour change.
  {
    WebSocketEndpoint base;
    base.host = "127.0.0.1";
    base.port = 8765;
    base.path = "/plugin";

    const auto result = apply_config_text(default_config_template(), base);
    require(result.ok, "template parses: " + result.error);
    require_eq(result.endpoint.host, "127.0.0.1", "template leaves host alone");
    require_eq(result.endpoint.port, 8765, "template leaves port alone");
    require_eq(result.endpoint.path, "/plugin", "template leaves path alone");
    require(result.endpoint.auth_token.empty(), "template sets no token");
  }

  // It has to be useful once uncommented, so the keys it documents must be the
  // keys the parser actually honours -- a template naming a key we ignore is
  // worse than no template.
  {
    const std::string tmpl = default_config_template();
    require(tmpl.find("url") != std::string::npos, "template documents `url`");
    require(tmpl.find("token") != std::string::npos,
            "template documents `token`");
    require(tmpl.find("ws://") != std::string::npos,
            "template shows a ws:// example");
  }

  // Uncommenting the example line has to produce a working endpoint. This
  // catches a template whose sample URL drifts out of the parser's grammar.
  {
    std::string tmpl = default_config_template();
    const auto marker = tmpl.find("# url");
    require(marker != std::string::npos, "template has a commented url line");
    tmpl.erase(marker, 2);  // strip the "# "

    const auto result = apply_config_text(tmpl, WebSocketEndpoint{});
    require(result.ok, "uncommented template url parses: " + result.error);
    require(!result.endpoint.host.empty(), "uncommented url yields a host");
  }

  // --- Host header ---------------------------------------------------------

  // Cloudflare (and every other name-based virtual host) keys on the exact
  // Host value, so the default port must not be appended.
  {
    WebSocketEndpoint endpoint;
    endpoint.host = "atc.zoal.app";
    endpoint.port = 80;
    require_eq(host_header(endpoint), "atc.zoal.app", "default port omitted");
  }
  {
    WebSocketEndpoint endpoint;
    endpoint.host = "127.0.0.1";
    endpoint.port = 8765;
    require_eq(host_header(endpoint), "127.0.0.1:8765",
               "non-default port retained");
  }
  {
    WebSocketEndpoint endpoint;
    endpoint.host = "::1";
    endpoint.port = 8765;
    require_eq(host_header(endpoint), "[::1]:8765", "IPv6 host re-bracketed");
  }
  {
    WebSocketEndpoint endpoint;
    endpoint.host = "::1";
    endpoint.port = 80;
    require_eq(host_header(endpoint), "[::1]", "IPv6 host bracketed bare");
  }

  // --- upsert_config_value ---------------------------------------------------
  //
  // The panel writes the token into a file the pilot also edits by hand, so the
  // property under test is mostly what *survives* the write.
  {
    require_eq(upsert_config_value("token = old\n", "token", "new"),
               "token = new\n", "rewrites a live assignment");
  }
  {
    require_eq(upsert_config_value("", "token", "secret"), "token = secret\n",
               "appends to an empty file");
  }
  {
    require_eq(upsert_config_value("url = ws://host/plugin", "token", "secret"),
               "url = ws://host/plugin\ntoken = secret\n",
               "appends a missing key, terminating the previous line");
  }
  {
    // The template ships every key commented out. Uncommenting in place keeps
    // the key under the comment that explains it.
    require_eq(upsert_config_value("# token = your-shared-secret\n", "token",
                                   "secret"),
               "token = secret\n", "uncomments the template line in place");
    require_eq(upsert_config_value("#token=x\n", "token", "secret"),
               "token = secret\n", "uncomments without spaces");
  }
  {
    const std::string before = "# a comment\n"
                               "url = ws://host/plugin\n"
                               "\n"
                               "# explains the token\n"
                               "# token = your-shared-secret\n"
                               "client_id = abc\n";
    const std::string after = "# a comment\n"
                              "url = ws://host/plugin\n"
                              "\n"
                              "# explains the token\n"
                              "token = secret\n"
                              "client_id = abc\n";
    require_eq(upsert_config_value(before, "token", "secret"), after,
               "comments, blank lines and other keys survive");
  }
  {
    // A live assignment wins over a commented one no matter the order, or a
    // save would edit the dead line and appear to do nothing.
    require_eq(upsert_config_value("token = live\n# token = dead\n", "token",
                                   "new"),
               "token = new\n# token = dead\n",
               "prefers the live assignment over a commented one");
    require_eq(upsert_config_value("# token = dead\ntoken = live\n", "token",
                                   "new"),
               "# token = dead\ntoken = new\n",
               "prefers the live assignment even when it is second");
  }
  {
    require_eq(upsert_config_value("token = old\r\n", "token", "new"),
               "token = new\r\n", "preserves CRLF line endings");
  }
  {
    // Clearing is a write, not a delete: the key stays visible so the pilot can
    // see it is deliberately empty rather than never set.
    require_eq(upsert_config_value("token = old\n", "token", ""),
               "token = \n", "an empty value clears in place");
  }
  {
    // `auth_token` is the accepted alias, and must not be mistaken for `token`
    // in either direction - writing one while the other is live would leave the
    // file with two assignments and the parser taking the last.
    require_eq(upsert_config_value("auth_token = old\n", "token", "new"),
               "auth_token = old\ntoken = new\n",
               "does not rewrite a different key that ends the same way");
    require_eq(upsert_config_value("# url = ws://elsewhere\n", "token", "s"),
               "# url = ws://elsewhere\ntoken = s\n",
               "does not uncomment an unrelated key");
  }
  {
    // Round trip: what we write has to be what the parser reads back.
    WebSocketEndpoint base;
    const std::string written =
        upsert_config_value(default_config_template(), "token", "secret");
    const auto parsed = apply_config_text(written, base);
    require(parsed.ok, "written template still parses");
    require_eq(parsed.endpoint.auth_token, "secret",
               "the parser reads back what was written");
  }

  std::cout << "endpoint config tests passed\n";
  return 0;
}
