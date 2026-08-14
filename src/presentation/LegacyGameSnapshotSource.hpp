#pragma once

#include "GameSnapshot.hpp"

namespace realmz::presentation {

// Read-only adapter over the preserved engine globals. It copies values into
// detached DTOs and never exposes a pointer or reference to save-compatible
// legacy structures.
class LegacyGameSnapshotSource final : public GameSnapshotSource {
public:
  [[nodiscard]] GameSnapshot capture() const override;
};

} // namespace realmz::presentation
