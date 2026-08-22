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

[[nodiscard]] ReplayAction selection_action(
    std::uint16_t ordinal,
    ReplayArgumentValue member,
    std::map<std::string, ReplayArgumentValue> extra = {}) {
  extra.emplace("member", std::move(member));
  return ReplayAction{
      .ordinal = ordinal,
      .kind = "select_party_member",
      .arguments = std::move(extra),
  };
}

[[nodiscard]] ReplayAction switch_weapon_action(
    std::uint16_t ordinal,
    ReplayArgumentValue combatant,
    std::map<std::string, ReplayArgumentValue> extra = {}) {
  extra.emplace("combatant", std::move(combatant));
  return ReplayAction{
      .ordinal = ordinal,
      .kind = "switch_weapon_set",
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

void test_v2_preserves_movement_and_adds_selection() {
  for (const std::string_view command : {
           "step_forward", "step_backward", "turn_left", "turn_right",
           "north", "northeast", "east", "southeast", "south",
           "southwest", "west", "northwest",
       }) {
    const auto action = movement_action(0, std::string(command));
    CHECK(decode_replay_action_v2(action) ==
        decode_replay_action_v1(action));
  }

  for (const std::int64_t member : {0, 5}) {
    const auto decoded = decode_replay_action_v2(
        selection_action(0, member));
    CHECK(decoded.sequence == 1U);
    const auto* selection =
        std::get_if<SelectPartyMemberAction>(&decoded.payload);
    CHECK(selection != nullptr);
    CHECK(selection->member == member);
  }

  const auto mixed = decode_replay_actions_v2({
      selection_action(0, std::int64_t{2}),
      movement_action(1, std::string("north")),
  });
  CHECK(mixed.size() == 2U);
  CHECK(std::holds_alternative<SelectPartyMemberAction>(mixed[0].payload));
  CHECK(std::holds_alternative<MovePartyAction>(mixed[1].payload));
}

void test_v2_selection_validation_is_exact() {
  for (const std::int64_t member : {-1, 6, 256}) {
    check_decode_error(
        [member] {
          static_cast<void>(decode_replay_action_v2(
              selection_action(0, member)));
        },
        "range 0..5");
  }
  for (const ReplayArgumentValue& member : {
           ReplayArgumentValue{true},
           ReplayArgumentValue{std::string("2")},
       }) {
    check_decode_error(
        [&member] {
          static_cast<void>(decode_replay_action_v2(
              selection_action(0, member)));
        },
        "must be an integer");
  }
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v2(ReplayAction{
            .ordinal = 0,
            .kind = "select_party_member",
            .arguments = {},
        }));
      },
      "exactly the member argument");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v2(selection_action(
            0, std::int64_t{2}, {{"extra", false}})));
      },
      "exactly the member argument");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v2(ReplayAction{
            .ordinal = 0,
            .kind = "open_inventory",
            .arguments = {},
        }));
      },
      "unsupported v2 action kind");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_actions_v2({
            selection_action(1, std::int64_t{2}),
        }));
      },
      "contiguous zero-based ordinals");
}

void test_v3_preserves_v2_and_adds_weapon_switching() {
  const auto movement = movement_action(0, std::string("north"));
  const auto selection = selection_action(0, std::int64_t{3});
  CHECK(decode_replay_action_v3(movement) ==
      decode_replay_action_v2(movement));
  CHECK(decode_replay_action_v3(selection) ==
      decode_replay_action_v2(selection));

  for (const std::int64_t combatant : {0, 5}) {
    const auto decoded = decode_replay_action_v3(
        switch_weapon_action(0, combatant));
    CHECK(decoded.sequence == 1U);
    const auto* switch_weapon =
        std::get_if<SwitchWeaponSetAction>(&decoded.payload);
    CHECK(switch_weapon != nullptr);
    CHECK(switch_weapon->combatant == combatant);
  }

  const auto mixed = decode_replay_actions_v3({
      movement_action(0, std::string("east")),
      selection_action(1, std::int64_t{2}),
      switch_weapon_action(2, std::int64_t{2}),
  });
  CHECK(mixed.size() == 3U);
  CHECK(std::holds_alternative<MovePartyAction>(mixed[0].payload));
  CHECK(std::holds_alternative<SelectPartyMemberAction>(mixed[1].payload));
  CHECK(std::holds_alternative<SwitchWeaponSetAction>(mixed[2].payload));

  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v2(
            switch_weapon_action(0, std::int64_t{0})));
      },
      "unsupported v2 action kind");
}

void test_v3_weapon_switch_validation_is_exact() {
  for (const std::int64_t combatant : {-1, 6, 256}) {
    check_decode_error(
        [combatant] {
          static_cast<void>(decode_replay_action_v3(
              switch_weapon_action(0, combatant)));
        },
        "range 0..5");
  }
  for (const ReplayArgumentValue& combatant : {
           ReplayArgumentValue{true},
           ReplayArgumentValue{std::string("2")},
       }) {
    check_decode_error(
        [&combatant] {
          static_cast<void>(decode_replay_action_v3(
              switch_weapon_action(0, combatant)));
        },
        "must be an integer");
  }
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v3(ReplayAction{
            .ordinal = 0,
            .kind = "switch_weapon_set",
            .arguments = {},
        }));
      },
      "exactly the combatant argument");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v3(switch_weapon_action(
            0, std::int64_t{2}, {{"extra", false}})));
      },
      "exactly the combatant argument");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v3(ReplayAction{
            .ordinal = 0,
            .kind = "guard_combatant",
            .arguments = {{"combatant", std::int64_t{2}}},
        }));
      },
      "unsupported v3 action kind");
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_actions_v3({
            switch_weapon_action(1, std::int64_t{2}),
        }));
      },
      "contiguous zero-based ordinals");
  CHECK(decode_replay_action_v3(
            switch_weapon_action(4095, std::int64_t{2})).sequence == 4096U);
  check_decode_error(
      [] {
        static_cast<void>(decode_replay_action_v3(
            switch_weapon_action(4096, std::int64_t{2})));
      },
      "v3 4095 maximum");
}

} // namespace

int main() {
  try {
    test_all_movement_values();
    test_exact_vocabulary();
    test_complete_sequence_validation();
    test_v2_preserves_movement_and_adds_selection();
    test_v2_selection_validation_is_exact();
    test_v3_preserves_v2_and_adds_weapon_switching();
    test_v3_weapon_switch_validation_is_exact();
    std::cout << "ReplayActionDecoderTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayActionDecoderTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
