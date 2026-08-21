#pragma once

#include <cstdint>

namespace realmz::replay {

// A replay-only deterministic generator. The algorithm is part of the replay
// protocol and must not be replaced by a platform random facility:
//
//   state_0 = mix64(seed ^ rotl(stream, 29) ^ 0x243f6a8885a308d3)
//   gamma   = mix64(stream ^ 0x9e3779b97f4a7c15) | 1
//   raw_n   = mix64(state_n += gamma)
//
// mix64 is the SplitMix64 finalizer. next_classic_random returns the low 16
// bits interpreted as a signed value, retrying the raw transition only for
// INT16_MIN, which Classic Random() excludes. draw_count counts public calls
// and returned Classic values, not rejected internal raw candidates.
class DeterministicReplayRng final {
public:
  DeterministicReplayRng(std::uint64_t seed, std::uint64_t stream) noexcept;

  [[nodiscard]] std::int16_t next_classic_random() noexcept;

  [[nodiscard]] std::uint64_t seed() const noexcept;
  [[nodiscard]] std::uint64_t stream() const noexcept;
  [[nodiscard]] std::uint64_t draw_count() const noexcept;

private:
  [[nodiscard]] std::uint64_t next_raw() noexcept;

  std::uint64_t seed_;
  std::uint64_t stream_;
  std::uint64_t state_;
  std::uint64_t gamma_;
  std::uint64_t draw_count_ = 0;
};

} // namespace realmz::replay
