#pragma once

#include "replay/Sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace realmz::replay {

inline constexpr std::array<std::string_view, 10> kReplayOutputFileNames = {
    "Data A1",
    "Data B1",
    "Data C1",
    "Data D1",
    "Data E1",
    "Data F1",
    "Data G1",
    "Data H1",
    "Data I1",
    "Data TD3",
};

inline constexpr std::uint64_t kDefaultMaximumReplayOutputFileBytes =
    64U * 1024U * 1024U;
inline constexpr std::uint64_t kDefaultMaximumReplayOutputTotalBytes =
    256U * 1024U * 1024U;
inline constexpr std::uint64_t kReplayOutputDataI1Bytes = 0x398BU;

struct ReplayOutputLimits final {
  std::uint64_t maximum_file_bytes =
      kDefaultMaximumReplayOutputFileBytes;
  std::uint64_t maximum_total_bytes =
      kDefaultMaximumReplayOutputTotalBytes;
};

struct ReplayOutputFile final {
  std::string relative_name;
  std::uint64_t byte_size = 0;
  Sha256Digest content_sha256{};

  bool operator==(const ReplayOutputFile&) const = default;
};

struct VerifiedReplayOutput final {
  std::array<ReplayOutputFile, kReplayOutputFileNames.size()> files;
  std::uint64_t total_bytes = 0;
  Sha256Digest tree_sha256{};

  bool operator==(const VerifiedReplayOutput&) const = default;
};

class ReplayOutputError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Returns user_data_root / "Save" / "Game X" after validating that the root
// is absolute and lexically normalized and that X is an uppercase slot A-J.
// This function performs no filesystem access.
[[nodiscard]] std::filesystem::path replay_output_slot_path(
    const std::filesystem::path& user_data_root,
    char output_slot);

// Verifies and hashes a completed Classic save in the configured output slot.
// The user-data root is the trust anchor: the implementation opens that root
// without following its final component, then opens the exact physical Save
// and Game X directories and their ten direct children without following
// symlinks on POSIX. It does not recursively resolve or police ancestors of
// the already-validated absolute user-data root. Windows rejects reparse
// points for the root, Save directory, slot directory, and files, denies
// write/delete sharing on its open handles, and reopens names to compare their
// identities where descriptor-relative traversal is unavailable.
//
// The slot must be a directory containing exactly kReplayOutputFileNames as
// regular, singly-linked files, with byte-exact case and no other entries.
// Data I1 must have the retail save size 0x398B. Each file is read
// through one handle and its identity, size, modification time, and change
// time are compared before and after streaming. Directory identity and entry
// names are also checked again after hashing. A limit violation or any
// missing, extra, linked, special, replaced, or detectably mutated object
// throws ReplayOutputError.
//
// tree_sha256 uses this exact v1 framing, independent of host byte order:
//
//   ASCII "realmz.semantic-replay.output-tree.v1", then byte 00
//   uint32_be file_count (always 10)
//   for each file in unsigned UTF-8 byte order:
//     uint16_be relative_name_byte_count
//     relative_name UTF-8 bytes (no terminator; currently all ASCII)
//     uint64_be content_byte_count
//     32 raw bytes of that file's SHA-256 digest
//
// No path separators, aggregate size, padding, or trailing bytes are framed.
[[nodiscard]] VerifiedReplayOutput verify_replay_output_slot(
    const std::filesystem::path& user_data_root,
    char output_slot,
    ReplayOutputLimits limits = {});

} // namespace realmz::replay
