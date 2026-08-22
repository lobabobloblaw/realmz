#include "replay/ReplayActionDecoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace realmz::replay {
namespace {

using presentation::MovementCommand;

struct MovementName final {
  std::string_view name;
  MovementCommand command;
};

constexpr std::array<MovementName, 12> kMovementNames = {{
    {"step_forward", MovementCommand::step_forward},
    {"step_backward", MovementCommand::step_backward},
    {"turn_left", MovementCommand::turn_left},
    {"turn_right", MovementCommand::turn_right},
    {"north", MovementCommand::north},
    {"northeast", MovementCommand::northeast},
    {"east", MovementCommand::east},
    {"southeast", MovementCommand::southeast},
    {"south", MovementCommand::south},
    {"southwest", MovementCommand::southwest},
    {"west", MovementCommand::west},
    {"northwest", MovementCommand::northwest},
}};

[[nodiscard]] MovementCommand decode_movement(std::string_view value) {
  for (const auto& candidate : kMovementNames) {
    if (candidate.name == value) {
      return candidate.command;
    }
  }
  throw ReplayActionDecodeError(
      "move_party command is not a supported v1 movement value");
}

[[nodiscard]] std::string action_prefix(const ReplayAction& action) {
  return "replay action " + std::to_string(action.ordinal) + ": ";
}

} // namespace

presentation::UIAction decode_replay_action_v1(
    const ReplayAction& action) {
  if (action.ordinal >= kMaximumReplayActions) {
    throw ReplayActionDecodeError(
        "replay action ordinal exceeds the v1 4095 maximum");
  }
  if (action.kind != "move_party") {
    throw ReplayActionDecodeError(
        action_prefix(action) + "unsupported v1 action kind: " +
        action.kind);
  }
  if (action.arguments.size() != 1 ||
      !action.arguments.contains("command")) {
    throw ReplayActionDecodeError(
        action_prefix(action) +
        "move_party requires exactly the command argument");
  }
  const auto* command =
      std::get_if<std::string>(&action.arguments.at("command"));
  if (!command) {
    throw ReplayActionDecodeError(
        action_prefix(action) + "move_party command must be a string");
  }

  const auto sequence =
      static_cast<presentation::ActionSequence>(action.ordinal) + 1U;
  return presentation::UIAction{
      .sequence = sequence,
      .payload = presentation::MovePartyAction{
          decode_movement(*command),
      },
  };
}

std::vector<presentation::UIAction> decode_replay_actions_v1(
    const std::vector<ReplayAction>& actions) {
  if (actions.size() > kMaximumReplayActions) {
    throw ReplayActionDecodeError(
        "replay action list exceeds the v1 4096-action limit");
  }

  std::vector<presentation::UIAction> decoded;
  decoded.reserve(actions.size());
  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (actions[index].ordinal != index) {
      throw ReplayActionDecodeError(
          "replay actions must have contiguous zero-based ordinals");
    }
    decoded.emplace_back(decode_replay_action_v1(actions[index]));
  }
  return decoded;
}

presentation::UIAction decode_replay_action_v2(
    const ReplayAction& action) {
  if (action.kind == "move_party") {
    // Delegation pins every v2 movement value to the exact v1 decoding.
    return decode_replay_action_v1(action);
  }
  if (action.ordinal >= kMaximumReplayActions) {
    throw ReplayActionDecodeError(
        "replay action ordinal exceeds the v2 4095 maximum");
  }
  if (action.kind != "select_party_member") {
    throw ReplayActionDecodeError(
        action_prefix(action) + "unsupported v2 action kind: " +
        action.kind);
  }
  if (action.arguments.size() != 1 ||
      !action.arguments.contains("member")) {
    throw ReplayActionDecodeError(
        action_prefix(action) +
        "select_party_member requires exactly the member argument");
  }
  const auto* member =
      std::get_if<std::int64_t>(&action.arguments.at("member"));
  if (!member) {
    throw ReplayActionDecodeError(
        action_prefix(action) +
        "select_party_member member must be an integer");
  }
  if (*member < 0 || *member > 5) {
    throw ReplayActionDecodeError(
        action_prefix(action) +
        "select_party_member member must be in the range 0..5");
  }

  const auto sequence =
      static_cast<presentation::ActionSequence>(action.ordinal) + 1U;
  return presentation::UIAction{
      .sequence = sequence,
      .payload = presentation::SelectPartyMemberAction{
          static_cast<presentation::PartyMemberId>(*member),
      },
  };
}

std::vector<presentation::UIAction> decode_replay_actions_v2(
    const std::vector<ReplayAction>& actions) {
  if (actions.size() > kMaximumReplayActions) {
    throw ReplayActionDecodeError(
        "replay action list exceeds the v2 4096-action limit");
  }

  std::vector<presentation::UIAction> decoded;
  decoded.reserve(actions.size());
  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (actions[index].ordinal != index) {
      throw ReplayActionDecodeError(
          "replay actions must have contiguous zero-based ordinals");
    }
    decoded.emplace_back(decode_replay_action_v2(actions[index]));
  }
  return decoded;
}

presentation::UIAction decode_replay_action_v3(
    const ReplayAction& action) {
  if (action.kind == "move_party" ||
      action.kind == "select_party_member") {
    // Delegation pins the complete inherited vocabulary to exact v2 decoding.
    return decode_replay_action_v2(action);
  }
  if (action.ordinal >= kMaximumReplayActions) {
    throw ReplayActionDecodeError(
        "replay action ordinal exceeds the v3 4095 maximum");
  }
  if (action.kind != "switch_weapon_set") {
    throw ReplayActionDecodeError(
        action_prefix(action) + "unsupported v3 action kind: " +
        action.kind);
  }
  if (action.arguments.size() != 1 ||
      !action.arguments.contains("combatant")) {
    throw ReplayActionDecodeError(
        action_prefix(action) +
        "switch_weapon_set requires exactly the combatant argument");
  }
  const auto* combatant =
      std::get_if<std::int64_t>(&action.arguments.at("combatant"));
  if (!combatant) {
    throw ReplayActionDecodeError(
        action_prefix(action) +
        "switch_weapon_set combatant must be an integer");
  }
  if (*combatant < 0 || *combatant > 5) {
    throw ReplayActionDecodeError(
        action_prefix(action) +
        "switch_weapon_set combatant must be in the range 0..5");
  }

  const auto sequence =
      static_cast<presentation::ActionSequence>(action.ordinal) + 1U;
  return presentation::UIAction{
      .sequence = sequence,
      .payload = presentation::SwitchWeaponSetAction{
          static_cast<presentation::CombatantId>(*combatant),
      },
  };
}

std::vector<presentation::UIAction> decode_replay_actions_v3(
    const std::vector<ReplayAction>& actions) {
  if (actions.size() > kMaximumReplayActions) {
    throw ReplayActionDecodeError(
        "replay action list exceeds the v3 4096-action limit");
  }

  std::vector<presentation::UIAction> decoded;
  decoded.reserve(actions.size());
  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (actions[index].ordinal != index) {
      throw ReplayActionDecodeError(
          "replay actions must have contiguous zero-based ordinals");
    }
    decoded.emplace_back(decode_replay_action_v3(actions[index]));
  }
  return decoded;
}

} // namespace realmz::replay
