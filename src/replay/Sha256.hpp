#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace realmz::replay {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Dependency-free, streaming SHA-256 as specified by FIPS 180-4. finalize()
// operates on a copy, so it is repeatable and does not prevent later update()
// calls. Inputs are limited to the SHA-256 maximum of 2^64 - 1 bits.
class Sha256 final {
public:
  Sha256() noexcept;

  void update(std::span<const std::byte> bytes);
  void update(std::string_view bytes);

  [[nodiscard]] Sha256Digest finalize() const noexcept;
  [[nodiscard]] std::uint64_t byte_count() const noexcept;

private:
  void transform(const std::byte* block) noexcept;

  std::array<std::uint32_t, 8> state_;
  std::array<std::byte, 64> buffer_{};
  std::size_t buffered_bytes_ = 0;
  std::uint64_t byte_count_ = 0;
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> bytes);
[[nodiscard]] Sha256Digest sha256(std::string_view bytes);
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

} // namespace realmz::replay
