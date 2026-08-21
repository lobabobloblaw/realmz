#include "replay/ReplayActionDecoder.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace realmz::presentation;
using namespace realmz::replay;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

template <typename Function>
void check_decode_error(Function&& function, std::string_view message) {
  ++checks_run;
  try {
    function();
  } catch (const ReplayActionDecodeError& error) {
    if (std::string_view(error.what()).find(message) != std::string_view::npos) {
      return;
    }
    throw std::runtime_error(
        "unexpected decode error: " + std::string(error.what()));
  }
  throw std::runtime_error("expected ReplayActionDecodeError");
}

[[nodiscard]] ReplayAction movement_action(
    std::uint16_t ordinal,
    ReplayArgumentValue command,
    std::map<std::string, ReplayArgumentValue> extra = {}) {
  extra.emplace("command", std::move(command));
  return ReplayAction{
      .ordinal = ordinal,
      .kind = "move_party",
      .arguments = std::move(extra),
  };
}

void test_all_movement_values() {
  constexpr std::array<std::pair<std::string_view, MovementCommand>, 12>
      cases = {{
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

  for (std::size_t index = 0; index < cases.size(); ++index) {
    const auto decoded = decode_replay_action_v1(movement_action(
        static_cast<std::uint16_t>(index), std::string(cases[index].first)));
    CHECK(decoded.sequence == index + 1U);
    CHECK(action_name(decoded.payload) == "move_party");
    const auto* movement = std::get_if<MovePartyAction>(&decoded.payload);
    CHECK(movement != nullptr);
    CHECK(movement->command == cases[index].second);
  }
}

void test_exact_vocabulary() {
  ReplayAction unknown = movement_action(0, std::string("north"));
  unknown.kind = "select_party_member";
  check_decode_error(
      [&] { static_cast<void>(decode_replay_action_v1(unknown)); },
      "unsupported v1 action kind");

  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v1(ReplayAction{
            .ordinal = 0,
            .kind = "move_party",
            .arguments = {},
        }));
      },
      "exactly the command argument");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v1(movement_action(
            0,
            std::string("north"),
            {{"speed", std::int64_t{1}}})));
      },
      "exactly the command argument");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v1(
            movement_action(0, std::int64_t{4})));
      },
      "command must be a string");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v1(
            movement_action(0, true)));
      },
      "command must be a string");
  for (const std::string_view invalid : {
           "North", "north_east", "forward", "", "north ",
       }) {
    check_decode_error(
        [invalid] {
          static_cast<void>(decode_replay_action_v1(
              movement_action(0, std::string(invalid))));
        },
        "not a supported v1 movement value");
  }
}

void test_complete_sequence_validation() {
  CHECK(decode_replay_actions_v1({}).empty());
  const auto decoded = decode_replay_actions_v1({
      movement_action(0, std::string("north")),
      movement_action(1, std::string("south")),
  });
  CHECK(decoded.size() == 2);
  CHECK(decoded[0].sequence == 1);
  CHECK(decoded[1].sequence == 2);

  check_decode_error(
      [] {
        static_cast<void>(decode_replay_actions_v1({
            movement_action(1, std::string("north")),
        }));
      },
      "contiguous zero-based ordinals");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_actions_v1({
            movement_action(0, std::string("north")),
            movement_action(0, std::string("south")),
        }));
      },
      "contiguous zero-based ordinals");

  const auto maximum_ordinal = decode_replay_action_v1(
      movement_action(4095, std::string("north")));
  CHECK(maximum_ordinal.sequence == 4096);
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v1(
            movement_action(4096, std::string("north"))));
      },
      "4095 maximum");

  std::vector<ReplayAction> maximum_plan;
  maximum_plan.reserve(kMaximumReplayActions);
  for (std::size_t index = 0; index < kMaximumReplayActions; ++index) {
    maximum_plan.emplace_back(movement_action(
        static_cast<std::uint16_t>(index), std::string("north")));
  }
  CHECK(decode_replay_actions_v1(maximum_plan).size() ==
      kMaximumReplayActions);
  maximum_plan.emplace_back(movement_action(0, std::string("north")));
  check_decode_error(
      [&] {
        static_cast<void>(decode_replay_actions_v1(maximum_plan));
      },
      "4096-action limit");
}

} // namespace

int main() {
  try {
    test_all_movement_values();
    test_exact_vocabulary();
    test_complete_sequence_validation();
    std::cout << "ReplayActionDecoderTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayActionDecoderTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
