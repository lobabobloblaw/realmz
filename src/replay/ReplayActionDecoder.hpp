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

// V2 preserves the complete v1 movement vocabulary and adds the idempotent
// first-click party-selection semantic action. Member IDs are validated in
// the protocol integer domain before conversion to PartyMemberId.
[[nodiscard]] presentation::UIAction decode_replay_action_v2(
    const ReplayAction& action);

[[nodiscard]] std::vector<presentation::UIAction> decode_replay_actions_v2(
    const std::vector<ReplayAction>& actions);

// V3 preserves the complete v2 vocabulary and adds one combat-only relative
// weapon-set toggle. Combatant IDs are validated in the protocol integer
// domain before conversion to CombatantId.
[[nodiscard]] presentation::UIAction decode_replay_action_v3(
    const ReplayAction& action);

[[nodiscard]] std::vector<presentation::UIAction> decode_replay_actions_v3(
    const std::vector<ReplayAction>& actions);

} // namespace realmz::replay
