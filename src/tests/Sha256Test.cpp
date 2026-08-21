#include "replay/Sha256.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace realmz::replay;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

void check_vector(std::string_view input, std::string_view expected_hex) {
  const Sha256Digest digest = sha256(input);
  CHECK(sha256_hex(digest) == expected_hex);

  Sha256 byte_at_a_time;
  for (const char byte : input) {
    byte_at_a_time.update(std::string_view(&byte, 1));
  }
  CHECK(byte_at_a_time.byte_count() == input.size());
  CHECK(byte_at_a_time.finalize() == digest);
  CHECK(byte_at_a_time.finalize() == digest);
}

void test_fips_and_nist_vectors() {
  check_vector(
      "",
      "e3b0c44298fc1c149afbf4c8996fb924"
      "27ae41e4649b934ca495991b7852b855");
  check_vector(
      "abc",
      "ba7816bf8f01cfea414140de5dae2223"
      "b00361a396177a9cb410ff61f20015ad");
  check_vector(
      "abcdbcdecdefdefgefghfghighijhijk"
      "ijkljklmklmnlmnomnopnopq",
      "248d6a61d20638b8e5c026930c3e6039"
      "a33ce45964ff2167f6ecedd419db06c1");
  check_vector(
      "The quick brown fox jumps over the lazy dog",
      "d7a8fbb307d7809469ca9abcb0082e4f"
      "8d5651e46d3cdb762d02d0bf37c9e592");
}

void test_million_a_vector_and_arbitrary_chunks() {
  Sha256 million;
  const std::string thousand(1000, 'a');
  for (std::size_t index = 0; index < 1000; ++index) {
    million.update(thousand);
  }
  CHECK(million.byte_count() == 1000000);
  CHECK(
      sha256_hex(million.finalize()) ==
      "cdc76e5c9914fb9281a1c7e284d73e67"
      "f1809a48a497200e046d39ccc7112cd0");

  std::string message;
  for (std::size_t index = 0; index < 4097; ++index) {
    message.push_back(static_cast<char>((index * 29U) & 0xFFU));
  }
  const Sha256Digest one_shot = sha256(message);
  Sha256 chunked;
  std::size_t offset = 0;
  std::size_t chunk_size = 1;
  while (offset < message.size()) {
    const std::size_t count =
        std::min(chunk_size, message.size() - offset);
    chunked.update(std::string_view(message).substr(offset, count));
    offset += count;
    chunk_size = (chunk_size * 7U) % 113U + 1U;
  }
  CHECK(chunked.finalize() == one_shot);
  CHECK(chunked.byte_count() == message.size());
}

void test_binary_span_and_non_destructive_finalize() {
  const std::array<std::byte, 7> binary = {
      std::byte{0x00},
      std::byte{0x01},
      std::byte{0x7F},
      std::byte{0x80},
      std::byte{0xFE},
      std::byte{0xFF},
      std::byte{0x00},
  };
  Sha256 incremental;
  incremental.update(std::span(binary).first(3));
  const Sha256Digest prefix = incremental.finalize();
  incremental.update(std::span(binary).subspan(3));
  CHECK(incremental.finalize() == sha256(binary));
  CHECK(prefix == sha256(std::span(binary).first(3)));

  Sha256 text;
  text.update("abc");
  CHECK(
      sha256_hex(text.finalize()) ==
      "ba7816bf8f01cfea414140de5dae2223"
      "b00361a396177a9cb410ff61f20015ad");
  text.update("def");
  CHECK(
      sha256_hex(text.finalize()) ==
      "bef57ec7f53a6d40beb640a780a639c8"
      "3bc29ac8a9816f1fc6c5c6dcd93c4721");
}

} // namespace

int main() {
  try {
    test_fips_and_nist_vectors();
    test_million_a_vector_and_arbitrary_chunks();
    test_binary_span_and_non_destructive_finalize();
    std::cout << "Sha256Test passed (" << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Sha256Test failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
