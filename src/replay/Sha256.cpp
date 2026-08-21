#include "replay/Sha256.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace realmz::replay {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

[[nodiscard]] constexpr std::uint32_t choose(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {
  return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t majority(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {
  return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t big_sigma_zero(
    std::uint32_t value) noexcept {
  return std::rotr(value, 2) ^ std::rotr(value, 13) ^ std::rotr(value, 22);
}

[[nodiscard]] constexpr std::uint32_t big_sigma_one(
    std::uint32_t value) noexcept {
  return std::rotr(value, 6) ^ std::rotr(value, 11) ^ std::rotr(value, 25);
}

[[nodiscard]] constexpr std::uint32_t small_sigma_zero(
    std::uint32_t value) noexcept {
  return std::rotr(value, 7) ^ std::rotr(value, 18) ^ (value >> 3U);
}

[[nodiscard]] constexpr std::uint32_t small_sigma_one(
    std::uint32_t value) noexcept {
  return std::rotr(value, 17) ^ std::rotr(value, 19) ^ (value >> 10U);
}

[[nodiscard]] constexpr std::uint32_t load_big_endian_word(
    const std::byte* bytes) noexcept {
  return (std::to_integer<std::uint32_t>(bytes[0]) << 24U) |
      (std::to_integer<std::uint32_t>(bytes[1]) << 16U) |
      (std::to_integer<std::uint32_t>(bytes[2]) << 8U) |
      std::to_integer<std::uint32_t>(bytes[3]);
}

} // namespace

Sha256::Sha256() noexcept
    : state_{
          0x6A09E667U,
          0xBB67AE85U,
          0x3C6EF372U,
          0xA54FF53AU,
          0x510E527FU,
          0x9B05688CU,
          0x1F83D9ABU,
          0x5BE0CD19U,
      } {}

void Sha256::transform(const std::byte* block) noexcept {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0; index < 16; ++index) {
    words[index] = load_big_endian_word(block + (index * 4));
  }
  for (std::size_t index = 16; index < words.size(); ++index) {
    words[index] = small_sigma_one(words[index - 2]) + words[index - 7] +
        small_sigma_zero(words[index - 15]) + words[index - 16];
  }

  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];

  for (std::size_t index = 0; index < words.size(); ++index) {
    const std::uint32_t temporary_one = h + big_sigma_one(e) +
        choose(e, f, g) + kRoundConstants[index] + words[index];
    const std::uint32_t temporary_two =
        big_sigma_zero(a) + majority(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + temporary_one;
    d = c;
    c = b;
    b = a;
    a = temporary_one + temporary_two;
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

void Sha256::update(std::span<const std::byte> bytes) {
  constexpr std::uint64_t maximum_bytes =
      std::numeric_limits<std::uint64_t>::max() / 8U;
  if (bytes.size() > maximum_bytes - byte_count_) {
    throw std::length_error("SHA-256 input exceeds its 64-bit bit-length field");
  }
  byte_count_ += static_cast<std::uint64_t>(bytes.size());

  while (!bytes.empty()) {
    const std::size_t available = buffer_.size() - buffered_bytes_;
    const std::size_t copy_size = std::min(available, bytes.size());
    std::memcpy(
        buffer_.data() + buffered_bytes_, bytes.data(), copy_size);
    buffered_bytes_ += copy_size;
    bytes = bytes.subspan(copy_size);
    if (buffered_bytes_ == buffer_.size()) {
      transform(buffer_.data());
      buffered_bytes_ = 0;
    }
  }
}

void Sha256::update(std::string_view bytes) {
  update(std::as_bytes(std::span(bytes.data(), bytes.size())));
}

Sha256Digest Sha256::finalize() const noexcept {
  Sha256 final_state = *this;
  final_state.buffer_[final_state.buffered_bytes_++] = std::byte{0x80};

  if (final_state.buffered_bytes_ > 56) {
    std::fill(
        final_state.buffer_.begin() +
            static_cast<std::ptrdiff_t>(final_state.buffered_bytes_),
        final_state.buffer_.end(),
        std::byte{0});
    final_state.transform(final_state.buffer_.data());
    final_state.buffered_bytes_ = 0;
  }
  std::fill(
      final_state.buffer_.begin() +
          static_cast<std::ptrdiff_t>(final_state.buffered_bytes_),
      final_state.buffer_.begin() + 56,
      std::byte{0});

  const std::uint64_t bit_count = final_state.byte_count_ * 8U;
  for (std::size_t index = 0; index < 8; ++index) {
    const unsigned shift = static_cast<unsigned>((7 - index) * 8);
    final_state.buffer_[56 + index] =
        static_cast<std::byte>((bit_count >> shift) & 0xFFU);
  }
  final_state.transform(final_state.buffer_.data());

  Sha256Digest digest{};
  for (std::size_t word = 0; word < final_state.state_.size(); ++word) {
    for (std::size_t byte = 0; byte < 4; ++byte) {
      const unsigned shift = static_cast<unsigned>((3 - byte) * 8);
      digest[(word * 4) + byte] = static_cast<std::uint8_t>(
          (final_state.state_[word] >> shift) & 0xFFU);
    }
  }
  return digest;
}

std::uint64_t Sha256::byte_count() const noexcept {
  return byte_count_;
}

Sha256Digest sha256(std::span<const std::byte> bytes) {
  Sha256 hash;
  hash.update(bytes);
  return hash.finalize();
}

Sha256Digest sha256(std::string_view bytes) {
  Sha256 hash;
  hash.update(bytes);
  return hash.finalize();
}

std::string sha256_hex(const Sha256Digest& digest) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.resize(digest.size() * 2);
  for (std::size_t index = 0; index < digest.size(); ++index) {
    result[index * 2] = digits[digest[index] >> 4U];
    result[(index * 2) + 1] = digits[digest[index] & 0x0FU];
  }
  return result;
}

} // namespace realmz::replay
