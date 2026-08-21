#include "replay/DeterministicReplayRng.hpp"

#include <bit>
#include <limits>

namespace realmz::replay {

namespace {

[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t value) noexcept {
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] constexpr std::int16_t signed_low_word(
    std::uint64_t value) noexcept {
  const std::uint16_t word = static_cast<std::uint16_t>(value & 0xFFFFU);
  if (word <= 0x7FFFU) {
    return static_cast<std::int16_t>(word);
  }
  return static_cast<std::int16_t>(static_cast<std::int32_t>(word) - 0x10000);
}

} // namespace

DeterministicReplayRng::DeterministicReplayRng(
    std::uint64_t seed,
    std::uint64_t stream) noexcept
    : seed_(seed),
      stream_(stream),
      state_(mix64(
          seed ^ std::rotl(stream, 29) ^ 0x243F6A8885A308D3ULL)),
      gamma_(mix64(stream ^ 0x9E3779B97F4A7C15ULL) | 1ULL) {}

std::uint64_t DeterministicReplayRng::next_raw() noexcept {
  state_ += gamma_;
  return mix64(state_);
}

std::int16_t DeterministicReplayRng::next_classic_random() noexcept {
  std::int16_t result = 0;
  do {
    result = signed_low_word(next_raw());
  } while (result == std::numeric_limits<std::int16_t>::min());
  ++draw_count_;
  return result;
}

std::uint64_t DeterministicReplayRng::seed() const noexcept {
  return seed_;
}

std::uint64_t DeterministicReplayRng::stream() const noexcept {
  return stream_;
}

std::uint64_t DeterministicReplayRng::draw_count() const noexcept {
  return draw_count_;
}

} // namespace realmz::replay
