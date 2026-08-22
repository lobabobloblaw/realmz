#pragma once

#include "replay/ReplayChildConfig.hpp"
#include "replay/Sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace realmz::replay {

struct ReplayCompletedResult final {
  std::uint64_t process_id = 0;
  std::string engine_identity;
  std::size_t settled_action_count = 0;
  Sha256Digest state_sha256{};
  Sha256Digest save_tree_sha256{};
  std::uint64_t rng_draw_count = 0;
};

class ReplayResultError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Produces the exact closed v1 child-result object followed by one newline.
// Configuration identity and RNG tokens are echoed from the already-validated
// child config. Engine identity is deliberately limited to normalized
// printable ASCII, a strict subset of the parent protocol's NFC text domain.
[[nodiscard]] std::string encode_replay_child_result_v1(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result);

// Exclusively creates config.result_path() as one private regular file. POSIX
// publication enforces current-user ownership and mode 0600; Windows uses a
// no-sharing exclusive create but still relies on the containing workspace's
// ACL until an explicit DACL verifier lands. The function never replaces or
// follows an existing leaf and returns only after the complete payload has
// been written, its metadata rechecked, and the file closed successfully. A
// failure can leave a partial file and, if it occurs during final
// close/metadata work, that file can contain complete JSON. The caller must
// therefore treat every exception as terminal and exit nonzero; the parent
// rejects nonzero children before it considers any result file.
void write_replay_child_result_v1(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result);

// V2 retains the closed field order and publication guarantees while binding
// the result to a schema-v2 child config.
[[nodiscard]] std::string encode_replay_child_result_v2(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result);

void write_replay_child_result_v2(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result);

// V3 retains the closed field order and publication guarantees while binding
// the result to a schema-v3 child config.
[[nodiscard]] std::string encode_replay_child_result_v3(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result);

void write_replay_child_result_v3(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result);

} // namespace realmz::replay
