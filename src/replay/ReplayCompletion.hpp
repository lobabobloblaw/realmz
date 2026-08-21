#pragma once

#include "replay/ReplayOutputOracle.hpp"
#include "replay/ReplayResultWriter.hpp"
#include "replay/ReplayRuntime.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

namespace realmz::replay {

class ReplayCompletionError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Engine-specific effects are injected so their ordering and failure
// semantics can be verified without entering a legacy gameplay loop or
// terminating the test process. Production supplies the explicit legacy save,
// strict output oracle, and exclusive result writer.
struct ReplayCompletionOperations final {
  std::string engine_identity;
  std::uint64_t process_id = 0;
  std::function<bool(char)> save_slot;
  std::function<VerifiedReplayOutput(
      const std::filesystem::path&, char)> verify_output_slot;
  std::function<void(
      const ReplayChildConfig&, const ReplayCompletedResult&)> publish_result;
};

// Requires a finalizing driver and a complete state trace. It then performs
// exactly one explicit save, verifies that fresh output, captures the RNG draw
// count after saving, and publishes the completed result. Exceptions propagate
// to the child terminal boundary, which must exit nonzero.
[[nodiscard]] ReplayCompletedResult complete_replay(
    ReplayRuntime& runtime,
    const ReplayCompletionOperations& operations);

} // namespace realmz::replay
