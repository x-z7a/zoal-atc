#ifndef ZOAL_ATC_TRANSPORT_BASE64_HPP
#define ZOAL_ATC_TRANSPORT_BASE64_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace zoal_atc::transport {

std::string base64_encode(const std::uint8_t *data, std::size_t size);

} // namespace zoal_atc::transport

#endif // ZOAL_ATC_TRANSPORT_BASE64_HPP
