#pragma once

#include "replay/ReplayStateOracle.hpp"

#include <stdexcept>

namespace realmz::replay {

class LegacyReplayStateCaptureError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Read-only leaf adapter over Realmz's preserved C globals. The returned
// snapshot is detached from the engine and contains no legacy pointers or
// object representations. Capture is intended for the engine's main thread at
// a settled semantic-gameplay poll.
class LegacyReplayStateSource final {
public:
  [[nodiscard]] ReplayStateSnapshot capture() const;
};

} // namespace realmz::replay
