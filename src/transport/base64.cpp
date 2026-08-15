#include "zoal_atc/transport/base64.hpp"

namespace zoal_atc::transport {

std::string base64_encode(const std::uint8_t *data, std::size_t size) {
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve(((size + 2) / 3) * 4);

  for (std::size_t i = 0; i < size; i += 3) {
    const std::uint32_t b0 = data[i];
    const std::uint32_t b1 = (i + 1 < size) ? data[i + 1] : 0;
    const std::uint32_t b2 = (i + 2 < size) ? data[i + 2] : 0;
    const std::uint32_t triple = (b0 << 16U) | (b1 << 8U) | b2;

    out.push_back(kTable[(triple >> 18U) & 0x3FU]);
    out.push_back(kTable[(triple >> 12U) & 0x3FU]);
    out.push_back(i + 1 < size ? kTable[(triple >> 6U) & 0x3FU] : '=');
    out.push_back(i + 2 < size ? kTable[triple & 0x3FU] : '=');
  }

  return out;
}

} // namespace zoal_atc::transport
