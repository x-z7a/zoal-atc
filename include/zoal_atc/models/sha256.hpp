#ifndef ZOAL_ATC_MODELS_SHA256_HPP
#define ZOAL_ATC_MODELS_SHA256_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace zoal_atc::models {

// Sha256 is FIPS 180-4 SHA-256, which is what every model file is pinned by.
class Sha256 {
public:
  Sha256();
  void update(const void *data, std::size_t size);
  // hex_digest finishes the hash; the object is spent afterwards.
  std::string hex_digest();

private:
  void block(const unsigned char *chunk);

  std::array<std::uint32_t, 8> state_;
  std::array<unsigned char, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t length_ = 0;
};

std::string sha256_hex(const std::string &bytes);

// sha256_file_prefix hashes the first `length` bytes of a file: nothing when
// the file cannot be read, is shorter than that, or `cancel` is raised.
std::optional<std::string> sha256_file_prefix(const std::filesystem::path &path,
                                              std::uint64_t length,
                                              const std::atomic_bool *cancel = nullptr);

} // namespace zoal_atc::models

#endif // ZOAL_ATC_MODELS_SHA256_HPP
