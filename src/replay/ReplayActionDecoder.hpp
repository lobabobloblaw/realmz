#pragma once

#include "presentation/UIAction.hpp"
#include "replay/ReplayChildConfig.hpp"

#include <stdexcept>
#include <vector>

namespace realmz::replay {

// Stable validation failure for the native v1 replay vocabulary. The generic
// JSON protocol deliberately accepts normalized action records; this decoder
// is the narrower engine capability gate and must run before loading a save.
class ReplayActionDecodeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// The first live replay vertical is intentionally movement-only. Each decoded
// action retains the protocol ordinal through sequence = ordinal + 1; sequence
// zero remains reserved for non-action engine events.
[[nodiscard]] presentation::UIAction decode_replay_action_v1(
    const ReplayAction& action);

// Revalidates the complete ordinal sequence as a defensive boundary for
// programmatically constructed actions, then decodes every record atomically.
[[nodiscard]] std::vector<presentation::UIAction> decode_replay_actions_v1(
    const std::vector<ReplayAction>& actions);

} // namespace realmz::replay
