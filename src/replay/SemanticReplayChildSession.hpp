#pragma once

#include "replay/ReplayDriver.hpp"

#include <string_view>

namespace realmz::replay {

class ReplayRuntime;

// Captures the current legacy globals into the canonical trace at the exact
// checkpoint requested by the poll driver. This is main-thread-only.
void record_live_replay_checkpoint(
    ReplayRuntime& runtime,
    const ReplayCheckpoint& checkpoint);

// Terminal child boundaries. Completion closes the save, verifier, and result
// writer synchronously before exiting zero. Every failure flushes one bounded
// diagnostic and exits nonzero without unwinding through preserved C frames.
[[noreturn]] void complete_semantic_replay_child(
    ReplayRuntime& runtime) noexcept;
[[noreturn]] void fail_semantic_replay_child(
    std::string_view detail) noexcept;

} // namespace realmz::replay
