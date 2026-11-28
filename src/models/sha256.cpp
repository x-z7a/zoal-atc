#include "zoal_atc/models/sha256.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

namespace zoal_atc::models {
namespace {

constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::block(const unsigned char *chunk) {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(chunk[i * 4]) << 24) |
           (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) |
           static_cast<std::uint32_t>(chunk[i * 4 + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
    const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(const void *data, std::size_t size) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  length_ += static_cast<std::uint64_t>(size);
  while (size > 0) {
    const std::size_t take = std::min(size, buffer_.size() - buffered_);
    std::copy(bytes, bytes + take, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
    buffered_ += take;
    bytes += take;
    size -= take;
    if (buffered_ == buffer_.size()) {
      block(buffer_.data());
      buffered_ = 0;
    }
  }
}

std::string Sha256::hex_digest() {
  const std::uint64_t bits = length_ * 8;
  const unsigned char one = 0x80;
  update(&one, 1);
  const unsigned char zero = 0;
  while (buffered_ != 56) {
    update(&zero, 1);
  }
  unsigned char tail[8];
  for (int i = 0; i < 8; ++i) {
    tail[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
  }
  update(tail, sizeof(tail));
  std::string hex;
  hex.reserve(64);
  char digits[9];
  for (const std::uint32_t word : state_) {
    std::snprintf(digits, sizeof(digits), "%08x", static_cast<unsigned>(word));
    hex += digits;
  }
  return hex;
}

std::string sha256_hex(const std::string &bytes) {
  Sha256 hash;
  hash.update(bytes.data(), bytes.size());
  return hash.hex_digest();
}

std::optional<std::string> sha256_file_prefix(const std::filesystem::path &path,
                                              std::uint64_t length,
                                              const std::atomic_bool *cancel) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  Sha256 hash;
  std::vector<char> chunk(1 << 20);
  std::uint64_t left = length;
  while (left > 0) {
    if (cancel != nullptr && cancel->load()) {
      return std::nullopt;
    }
    const auto want = static_cast<std::streamsize>(std::min<std::uint64_t>(left, chunk.size()));
    in.read(chunk.data(), want);
    const std::streamsize got = in.gcount();
    if (got <= 0) {
      return std::nullopt;
    }
    hash.update(chunk.data(), static_cast<std::size_t>(got));
    left -= static_cast<std::uint64_t>(got);
  }
  return hash.hex_digest();
}

} // namespace zoal_atc::models
