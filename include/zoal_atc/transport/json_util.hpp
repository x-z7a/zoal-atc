#ifndef ZOAL_ATC_TRANSPORT_JSON_UTIL_HPP
#define ZOAL_ATC_TRANSPORT_JSON_UTIL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Minimal JSON helpers shared by the plugin's wire-frame builders and the
// inbound frame parsers. The plugin deliberately avoids a JSON library: frames
// are small, schemas are fixed (the plugin<->console contract in
// console/internal/pluginws), and the console side owns full validation.
namespace zoal_atc::transport {

// json_escape escapes a value for embedding inside a JSON string literal.
std::string json_escape(std::string_view value);

// json_number formats a double as a compact JSON number (no exponent surprises
// for the magnitudes we send; NaN/Inf degrade to 0).
std::string json_number(double value);

// json_string_field extracts the string value of `key` from a flat JSON
// payload, or nullopt when absent/not a string.
std::optional<std::string> json_string_field(const std::string &json,
                                             std::string_view key);

// json_bool_field extracts a boolean field, or nullopt when absent.
std::optional<bool> json_bool_field(const std::string &json,
                                    std::string_view key);

// json_uint_field extracts a non-negative integer field, or nullopt.
std::optional<std::uint64_t> json_uint_field(const std::string &json,
                                             std::string_view key);

// json_number_field extracts a numeric (integer or decimal) field as a double,
// or nullopt when absent/not a number.
std::optional<double> json_number_field(const std::string &json,
                                        std::string_view key);

// json_string_array_field extracts a flat array-of-strings field, e.g.
// `"datarefs":["a","b"]`, as a vector. An absent field or a non-array value
// yields an empty vector.
std::vector<std::string> json_string_array_field(const std::string &json,
                                                 std::string_view key);

// json_raw_field extracts the raw JSON text of the value bound to `key`, e.g.
// `{"payload":{"a":1}}` with key "payload" yields `{"a":1}`.
//
// Unlike the helpers above it matches only keys at the *top level* of the
// object. The gui_* frames wrap an opaque console payload the plugin forwards
// without understanding it, so a key inside that payload must never answer for
// the envelope around it.
std::optional<std::string> json_raw_field(const std::string &json,
                                          std::string_view key);

// json_unquote decodes a raw JSON string value (quotes included) into its text,
// or nullopt when `raw` is not a well-formed string.
std::optional<std::string> json_unquote(const std::string &raw);

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_JSON_UTIL_HPP
