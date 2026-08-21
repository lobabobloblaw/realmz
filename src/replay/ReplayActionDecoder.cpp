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

} // namespace realmz::replay
