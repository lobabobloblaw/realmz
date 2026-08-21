#include "replay/DeterministicReplayRng.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using realmz::replay::DeterministicReplayRng;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

template <std::size_t Size>
void check_vector(
    std::uint64_t seed,
    std::uint64_t stream,
    const std::array<std::int16_t, Size>& expected) {
  DeterministicReplayRng rng(seed, stream);
  CHECK(rng.seed() == seed);
  CHECK(rng.stream() == stream);
  CHECK(rng.draw_count() == 0);
  for (std::size_t index = 0; index < expected.size(); ++index) {
    CHECK(rng.next_classic_random() == expected[index]);
    CHECK(rng.draw_count() == index + 1);
  }
}

void test_protocol_golden_vectors() {
  // These vectors were independently calculated from the fixed algorithm in
  // DeterministicReplayRng.hpp. Changing any value is a protocol change.
  check_vector(
      0,
      0,
      std::array<std::int16_t, 12>{
          -31953,
          -11672,
          -448,
          1630,
          1733,
          -14085,
          -18677,
          -16289,
          11663,
          -25421,
          -6133,
          -20560,
      });
  check_vector(
      0x0123456789ABCDEFULL,
      0xFEDCBA9876543210ULL,
      std::array<std::int16_t, 12>{
          22067,
          -16737,
          1047,
          6734,
          -25013,
          -27046,
          -22241,
          28643,
          -27827,
          -24586,
          1134,
          1381,
      });
  check_vector(
      std::numeric_limits<std::uint64_t>::max(),
      std::numeric_limits<std::uint64_t>::max(),
      std::array<std::int16_t, 12>{
          13782,
          6760,
          -14647,
          5040,
          14460,
          -11297,
          803,
          -24413,
          -27241,
          -9790,
          -4821,
          9931,
      });
}

void test_reproducibility_and_independent_streams() {
  DeterministicReplayRng first(123, 456);
  DeterministicReplayRng second(123, 456);
  DeterministicReplayRng other_seed(124, 456);
  DeterministicReplayRng other_stream(123, 457);
  bool seed_diverged = false;
  bool stream_diverged = false;
  for (std::size_t index = 0; index < 128; ++index) {
    const std::int16_t value = first.next_classic_random();
    const std::int16_t seed_value = other_seed.next_classic_random();
    const std::int16_t stream_value = other_stream.next_classic_random();
    CHECK(second.next_classic_random() == value);
    seed_diverged = seed_diverged || seed_value != value;
    stream_diverged = stream_diverged || stream_value != value;
  }
  CHECK(seed_diverged);
  CHECK(stream_diverged);
  CHECK(first.draw_count() == 128);
  CHECK(second.draw_count() == 128);
  CHECK(other_seed.draw_count() == 128);
  CHECK(other_stream.draw_count() == 128);
}

void test_classic_exclusion_and_returned_draw_count() {
  // Seed 440699, stream 0 has 0x8000 in the first raw low word. The generator
  // must reject that internal candidate, return the second, and count one
  // public/returned draw rather than two internal state transitions.
  DeterministicReplayRng rejection_case(440699, 0);
  CHECK(rejection_case.next_classic_random() == 12369);
  CHECK(rejection_case.draw_count() == 1);
  CHECK(rejection_case.next_classic_random() == -3657);
  CHECK(rejection_case.draw_count() == 2);

  DeterministicReplayRng sample(0xDEADBEEFCAFEBABEULL, 0x1020304050607080ULL);
  for (std::size_t index = 0; index < 200000; ++index) {
    CHECK(sample.next_classic_random() !=
        std::numeric_limits<std::int16_t>::min());
  }
  CHECK(sample.draw_count() == 200000);
}

} // namespace

int main() {
  try {
    test_protocol_golden_vectors();
    test_reproducibility_and_independent_streams();
    test_classic_exclusion_and_returned_draw_count();
    std::cout << "DeterministicReplayRngTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "DeterministicReplayRngTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
