#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "presentation/RuntimeLegacyCommandBridge.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

static_assert(std::is_same_v<
    RuntimeLegacyGuardCombatantSink,
    RuntimeLegacyFinishCombatantSink>);
static_assert(std::is_same_v<
    RuntimeLegacyFinishCombatantSink,
    RuntimeLegacyDelayCombatantSink>);
static_assert(std::is_same_v<
    RuntimeLegacyDelayCombatantSink,
    RuntimeLegacyCenterActiveCombatantSink>);
static_assert(std::is_same_v<
    RuntimeLegacyCenterActiveCombatantSink,
    RuntimeLegacySwitchWeaponSink>);
static_assert(std::is_aggregate_v<RuntimeLegacyCombatActionSinks>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::guard_combatant),
    std::optional<RuntimeLegacyGuardCombatantSink>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::finish_combatant),
    std::optional<RuntimeLegacyFinishCombatantSink>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::delay_combatant),
    std::optional<RuntimeLegacyDelayCombatantSink>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::center_active_combatant),
    std::optional<RuntimeLegacyCenterActiveCombatantSink>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::switch_weapon),
    std::optional<RuntimeLegacySwitchWeaponSink>>);
static_assert(std::is_same_v<
    RuntimeLegacyCycleCombatFocusSink,
    std::function<bool(
        CombatantId,
        CombatFocusDirection,
        uint32_t,
        const RuntimeLegacyCommandContext&)>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::cycle_combat_focus),
    std::optional<RuntimeLegacyCycleCombatFocusSink>>);
static_assert(std::is_same_v<
    RuntimeLegacyOpenCombatItemsSink,
    std::function<bool(
        CombatantId,
        PartyMemberId,
        uint32_t,
        const RuntimeLegacyCommandContext&)>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::open_combat_items),
    std::optional<RuntimeLegacyOpenCombatItemsSink>>);
static_assert(std::numeric_limits<PartyMemberId>::min() == 0);
static_assert(std::numeric_limits<PartyMemberId>::max() == 0xFF);

struct ExpectedMovement {
  MovementCommand command;
  uint32_t message;
};

void test_outdoor_mapping_and_dispatch() {
  constexpr std::array expected{
      ExpectedMovement{MovementCommand::north, 0x00007E1EU},
      ExpectedMovement{MovementCommand::northeast, 0x00005C39U},
      ExpectedMovement{MovementCommand::east, 0x00007C1DU},
      ExpectedMovement{MovementCommand::southeast, 0x00005533U},
      ExpectedMovement{MovementCommand::south, 0x00007D1FU},
      ExpectedMovement{MovementCommand::southwest, 0x00005331U},
      ExpectedMovement{MovementCommand::west, 0x00007B1CU},
      ExpectedMovement{MovementCommand::northwest, 0x00005937U},
  };
  const RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  std::vector<uint32_t> queued;
  RuntimeLegacyCommandBridge bridge(
      [context] { return context; },
      [&queued](uint32_t message) {
        queued.emplace_back(message);
        return true;
      });

  ActionSequence sequence = 1;
  for (const auto& item : expected) {
    CHECK(legacy_key_message_for_movement(item.command, context) ==
        item.message);
    const auto result = bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = MovePartyAction{item.command},
    });
    CHECK(result.status == DispatchStatus::handled);
    CHECK(result.events.empty());
    CHECK(queued.back() == item.message);
  }
  CHECK(queued.size() == expected.size());

  for (const auto unsupported : {
           MovementCommand::step_forward,
           MovementCommand::step_backward,
           MovementCommand::turn_left,
           MovementCommand::turn_right,
       }) {
    CHECK(!legacy_key_message_for_movement(unsupported, context));
    const auto result = bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = MovePartyAction{unsupported},
    });
    CHECK(result.status == DispatchStatus::rejected);
  }
  CHECK(queued.size() == expected.size());
}

void test_dungeon_mapping() {
  constexpr std::array expected{
      ExpectedMovement{MovementCommand::step_forward, 0x00007E1EU},
      ExpectedMovement{MovementCommand::step_backward, 0x00007D1FU},
      ExpectedMovement{MovementCommand::turn_left, 0x00007B1CU},
      ExpectedMovement{MovementCommand::turn_right, 0x00007C1DU},
  };
  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    const RuntimeLegacyCommandContext context{
        .screen = ScreenContext::dungeon,
        .world_presentation = presentation,
        .adaptive_eligible = true,
    };
    for (const auto& item : expected) {
      CHECK(legacy_key_message_for_movement(item.command, context) ==
          item.message);
    }
    for (const auto unsupported : {
             MovementCommand::north,
             MovementCommand::northeast,
             MovementCommand::east,
             MovementCommand::southeast,
             MovementCommand::south,
             MovementCommand::southwest,
             MovementCommand::west,
             MovementCommand::northwest,
         }) {
      CHECK(!legacy_key_message_for_movement(unsupported, context));
    }
  }
}

void test_fail_closed_context_and_sink() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = false,
  };
  int sink_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [&sink_calls](uint32_t) {
        ++sink_calls;
        return true;
      });
  auto result = bridge.dispatch(UIAction{
      .sequence = 1,
      .payload = MovePartyAction{MovementCommand::north},
  });
  CHECK(result.status == DispatchStatus::rejected);
  CHECK(sink_calls == 0);

  context.adaptive_eligible = true;
  context.screen = ScreenContext::combat;
  result = bridge.dispatch(UIAction{
      .sequence = 2,
      .payload = MovePartyAction{MovementCommand::north},
  });
  CHECK(result.status == DispatchStatus::rejected);
  CHECK(sink_calls == 0);

  context.screen = ScreenContext::exploration;
  context.world_presentation = WorldPresentation::outdoor;
  RuntimeLegacyCommandBridge rejecting_sink(
      [&context] { return context; },
      [](uint32_t) { return false; });
  result = rejecting_sink.dispatch(UIAction{
      .sequence = 3,
      .payload = MovePartyAction{MovementCommand::south},
  });
  CHECK(result.status == DispatchStatus::failed);

  result = bridge.dispatch(UIAction{
      .sequence = 4,
      .payload = SelectPartyMemberAction{0},
  });
  CHECK(result.status == DispatchStatus::unsupported);
  CHECK(sink_calls == 0);
}

void test_typed_movement_sink_preserves_context() {
  const RuntimeLegacyCommandContext context{
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .adaptive_eligible = true,
  };
  int sink_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [context] { return context; },
      [&sink_calls, &context](
          MovementCommand command,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++sink_calls;
        CHECK(command == MovementCommand::turn_left);
        CHECK(message == 0x00007B1CU);
        CHECK(captured_context == context);
        return true;
      });
  const auto result = bridge.dispatch(UIAction{
      .sequence = 17,
      .payload = MovePartyAction{MovementCommand::turn_left},
  });
  CHECK(result.status == DispatchStatus::handled);
  CHECK(sink_calls == 1);
}

void test_typed_party_selection_sink_preserves_id_and_context() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  std::vector<std::pair<PartyMemberId, RuntimeLegacyCommandContext>> selected;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](uint32_t) { return true; },
      [&selected](
          PartyMemberId member,
          const RuntimeLegacyCommandContext& captured_context) {
        selected.emplace_back(member, captured_context);
        return true;
      });

  auto result = bridge.dispatch(UIAction{
      .sequence = 31,
      .payload = SelectPartyMemberAction{0},
  });
  CHECK(result.status == DispatchStatus::handled);
  CHECK(result.events.empty());
  CHECK(selected.size() == 1);
  CHECK(selected.back().first == 0);
  CHECK(selected.back().second == context);

  // The bridge deliberately does not infer membership or lifecycle validity.
  // Even the full PartyMemberId range is forwarded unchanged to the sink.
  context = {
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  result = bridge.dispatch(UIAction{
      .sequence = 32,
      .payload = SelectPartyMemberAction{
          std::numeric_limits<PartyMemberId>::max()},
  });
  CHECK(result.status == DispatchStatus::handled);
  CHECK(selected.size() == 2);
  CHECK(selected.back().first == std::numeric_limits<PartyMemberId>::max());
  CHECK(selected.back().second == context);
}

void test_party_selection_fail_closed_boundaries() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = false,
  };
  int selection_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [&selection_calls](
          PartyMemberId,
          const RuntimeLegacyCommandContext&) {
        ++selection_calls;
        return true;
      });

  auto select = [&bridge](ActionSequence sequence) {
    return bridge.dispatch(UIAction{
        .sequence = sequence,
        .payload = SelectPartyMemberAction{7},
    });
  };
  CHECK(select(40).status == DispatchStatus::rejected);
  CHECK(selection_calls == 0);

  context.adaptive_eligible = true;
  ActionSequence sequence = 41;
  for (const auto screen : {
           ScreenContext::title,
           ScreenContext::party_selection,
           ScreenContext::party_creation,
           ScreenContext::combat,
           ScreenContext::inventory,
           ScreenContext::shop,
           ScreenContext::encounter,
           ScreenContext::ending,
       }) {
    context.screen = screen;
    CHECK(select(sequence++).status == DispatchStatus::rejected);
    CHECK(selection_calls == 0);
  }

  context.screen = ScreenContext::exploration;
  RuntimeLegacyCommandBridge rejecting_sink(
      [&context] { return context; },
      [](uint32_t) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) {
        return false;
      });
  CHECK(rejecting_sink.dispatch(UIAction{
      .sequence = sequence++,
      .payload = SelectPartyMemberAction{2},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge missing_provider(
      {},
      RuntimeLegacyKeySink{[](uint32_t) { return true; }},
      RuntimeLegacyPartySelectionSink{
          [](PartyMemberId, const RuntimeLegacyCommandContext&) {
            return true;
          }});
  CHECK(missing_provider.dispatch(UIAction{
      .sequence = sequence++,
      .payload = SelectPartyMemberAction{2},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge missing_sink(
      [&context] { return context; },
      RuntimeLegacyKeySink{[](uint32_t) { return true; }},
      RuntimeLegacyPartySelectionSink{});
  CHECK(missing_sink.dispatch(UIAction{
      .sequence = sequence++,
      .payload = SelectPartyMemberAction{2},
  }).status == DispatchStatus::failed);
}

void test_party_selection_is_opt_in_and_preserves_movement() {
  const RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  int movement_calls = 0;
  int selection_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [context] { return context; },
      [&movement_calls](
          MovementCommand command,
          uint32_t message,
          const RuntimeLegacyCommandContext&) {
        ++movement_calls;
        CHECK(command == MovementCommand::west);
        CHECK(message == 0x00007B1CU);
        return true;
      },
      [&selection_calls](
          PartyMemberId,
          const RuntimeLegacyCommandContext&) {
        ++selection_calls;
        return true;
      });
  CHECK(bridge.dispatch(UIAction{
      .sequence = 50,
      .payload = MovePartyAction{MovementCommand::west},
  }).status == DispatchStatus::handled);
  CHECK(movement_calls == 1);
  CHECK(selection_calls == 0);

  RuntimeLegacyCommandBridge movement_only(
      [context] { return context; },
      [](uint32_t) { return true; });
  CHECK(movement_only.dispatch(UIAction{
      .sequence = 51,
      .payload = SelectPartyMemberAction{1},
  }).status == DispatchStatus::unsupported);
}

void test_open_inventory_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  int inventory_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [&inventory_calls, &context](
          PartyMemberId member,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++inventory_calls;
        CHECK(member == 2);
        CHECK(message == 0x00002269U);
        CHECK(captured_context == context);
        return true;
      });

  ActionSequence sequence = 60;
  CHECK(legacy_key_message_for_open_inventory(context) == 0x00002269U);
  CHECK(bridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenInventoryAction{2},
  }).status == DispatchStatus::handled);
  CHECK(inventory_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.screen = ScreenContext::dungeon;
    context.world_presentation = presentation;
    CHECK(legacy_key_message_for_open_inventory(context) == 0x00002269U);
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenInventoryAction{2},
    }).status == DispatchStatus::handled);
  }
  CHECK(inventory_calls == 3);

  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_open_inventory(context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenInventoryAction{2},
  }).status == DispatchStatus::rejected);
  CHECK(inventory_calls == 3);

  context.adaptive_eligible = true;
  for (const auto invalid : {
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::exploration,
               .world_presentation = WorldPresentation::dungeon_map,
               .adaptive_eligible = true,
           },
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::dungeon,
               .world_presentation = WorldPresentation::outdoor,
               .adaptive_eligible = true,
           },
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::combat,
               .world_presentation = WorldPresentation::none,
               .adaptive_eligible = true,
           },
       }) {
    context = invalid;
    CHECK(!legacy_key_message_for_open_inventory(context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenInventoryAction{2},
    }).status == DispatchStatus::rejected);
  }
  CHECK(inventory_calls == 3);

  context = {
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  RuntimeLegacyCommandBridge rejecting_sink(
      [&context] { return context; },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return false; });
  CHECK(rejecting_sink.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenInventoryAction{2},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge movement_only(
      [&context] { return context; },
      [](uint32_t) { return true; });
  CHECK(movement_only.dispatch(UIAction{
      .sequence = sequence,
      .payload = OpenInventoryAction{2},
  }).status == DispatchStatus::unsupported);
}

void test_open_spellbook_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  int spellbook_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [&spellbook_calls, &context](
          PartyMemberId member,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++spellbook_calls;
        CHECK(member == 2);
        CHECK(message == 0x00000173U);
        CHECK(captured_context == context);
        return true;
      });

  ActionSequence sequence = 80;
  CHECK(legacy_key_message_for_open_spellbook(context) == 0x00000173U);
  CHECK(bridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSpellbookAction{2},
  }).status == DispatchStatus::handled);
  CHECK(spellbook_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.screen = ScreenContext::dungeon;
    context.world_presentation = presentation;
    CHECK(legacy_key_message_for_open_spellbook(context) == 0x00000173U);
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenSpellbookAction{2},
    }).status == DispatchStatus::handled);
  }
  CHECK(spellbook_calls == 3);

  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_open_spellbook(context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSpellbookAction{2},
  }).status == DispatchStatus::rejected);
  CHECK(spellbook_calls == 3);

  context.adaptive_eligible = true;
  for (const auto invalid : {
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::exploration,
               .world_presentation = WorldPresentation::dungeon_map,
               .adaptive_eligible = true,
           },
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::dungeon,
               .world_presentation = WorldPresentation::outdoor,
               .adaptive_eligible = true,
           },
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::combat,
               .world_presentation = WorldPresentation::none,
               .adaptive_eligible = true,
           },
       }) {
    context = invalid;
    CHECK(!legacy_key_message_for_open_spellbook(context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenSpellbookAction{2},
    }).status == DispatchStatus::rejected);
  }
  CHECK(spellbook_calls == 3);

  context = {
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  RuntimeLegacyCommandBridge rejecting_sink(
      [&context] { return context; },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return false; });
  CHECK(rejecting_sink.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSpellbookAction{2},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge movement_only(
      [&context] { return context; },
      [](uint32_t) { return true; });
  CHECK(movement_only.dispatch(UIAction{
      .sequence = sequence,
      .payload = OpenSpellbookAction{2},
  }).status == DispatchStatus::unsupported);
}

void test_open_save_game_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  constexpr RuntimeLegacyMenuCommand expected{
      .menu_id = 129,
      .item_id = 3,
  };
  int save_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [&save_calls, &context, expected](
          RuntimeLegacyMenuCommand command,
          const RuntimeLegacyCommandContext& captured_context) {
        ++save_calls;
        CHECK(command == expected);
        CHECK(captured_context == context);
        return true;
      });

  ActionSequence sequence = 100;
  CHECK(legacy_menu_command_for_open_save_game(context) == expected);
  CHECK(bridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSaveGameAction{},
  }).status == DispatchStatus::handled);
  CHECK(save_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.screen = ScreenContext::dungeon;
    context.world_presentation = presentation;
    CHECK(legacy_menu_command_for_open_save_game(context) == expected);
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenSaveGameAction{},
    }).status == DispatchStatus::handled);
  }
  CHECK(save_calls == 3);

  context.adaptive_eligible = false;
  CHECK(!legacy_menu_command_for_open_save_game(context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSaveGameAction{},
  }).status == DispatchStatus::rejected);
  CHECK(save_calls == 3);

  context.adaptive_eligible = true;
  for (const auto invalid : {
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::exploration,
               .world_presentation = WorldPresentation::dungeon_map,
               .adaptive_eligible = true,
           },
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::dungeon,
               .world_presentation = WorldPresentation::outdoor,
               .adaptive_eligible = true,
           },
           RuntimeLegacyCommandContext{
               .screen = ScreenContext::combat,
               .world_presentation = WorldPresentation::none,
               .adaptive_eligible = true,
           },
       }) {
    context = invalid;
    CHECK(!legacy_menu_command_for_open_save_game(context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenSaveGameAction{},
    }).status == DispatchStatus::rejected);
  }
  CHECK(save_calls == 3);

  context = {
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  RuntimeLegacyCommandBridge rejecting_sink(
      [&context] { return context; },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](RuntimeLegacyMenuCommand,
          const RuntimeLegacyCommandContext&) { return false; });
  CHECK(rejecting_sink.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSaveGameAction{},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge movement_only(
      [&context] { return context; },
      [](uint32_t) { return true; });
  CHECK(movement_only.dispatch(UIAction{
      .sequence = sequence,
      .payload = OpenSaveGameAction{},
  }).status == DispatchStatus::unsupported);
}

void test_open_load_game_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .adaptive_eligible = true,
  };
  constexpr RuntimeLegacyMenuCommand expected_save{129, 3};
  constexpr RuntimeLegacyMenuCommand expected_load{129, 2};
  int load_calls = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [expected_save](RuntimeLegacyMenuCommand command,
          const RuntimeLegacyCommandContext&) {
        CHECK(command == expected_save);
        return true;
      },
      [&load_calls, &context, expected_load](
          RuntimeLegacyMenuCommand command,
          const RuntimeLegacyCommandContext& captured_context) {
        ++load_calls;
        CHECK(command == expected_load);
        CHECK(captured_context == context);
        return true;
      });

  ActionSequence sequence = 200;
  for (const auto valid : {
           RuntimeLegacyCommandContext{
               ScreenContext::exploration,
               WorldPresentation::outdoor,
               true},
           RuntimeLegacyCommandContext{
               ScreenContext::dungeon,
               WorldPresentation::dungeon_map,
               true},
           RuntimeLegacyCommandContext{
               ScreenContext::dungeon,
               WorldPresentation::dungeon_first_person,
               true},
       }) {
    context = valid;
    CHECK(legacy_menu_command_for_open_load_game(context) == expected_load);
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenLoadGameAction{},
    }).status == DispatchStatus::handled);
  }
  CHECK(load_calls == 3);

  for (const auto invalid : {
           RuntimeLegacyCommandContext{
               ScreenContext::exploration,
               WorldPresentation::dungeon_map,
               true},
           RuntimeLegacyCommandContext{
               ScreenContext::dungeon,
               WorldPresentation::outdoor,
               true},
           RuntimeLegacyCommandContext{
               ScreenContext::combat,
               WorldPresentation::none,
               true},
           RuntimeLegacyCommandContext{
               ScreenContext::exploration,
               WorldPresentation::outdoor,
               false},
       }) {
    context = invalid;
    CHECK(!legacy_menu_command_for_open_load_game(context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = sequence++,
        .payload = OpenLoadGameAction{},
    }).status == DispatchStatus::rejected);
  }
  CHECK(load_calls == 3);

  context = {ScreenContext::exploration, WorldPresentation::outdoor, true};
  RuntimeLegacyCommandBridge rejecting_sink(
      [&context] { return context; },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return false;
      });
  CHECK(rejecting_sink.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenLoadGameAction{},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge movement_only(
      [&context] { return context; }, [](uint32_t) { return true; });
  CHECK(movement_only.dispatch(UIAction{
      .sequence = sequence,
      .payload = OpenLoadGameAction{},
  }).status == DispatchStatus::unsupported);
}

void test_guard_combatant_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int guard_calls = 0;
  bool accept_guard = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [&guard_calls, &accept_guard, &received_combatant, &received_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++guard_calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept_guard;
      });

  CHECK(legacy_key_message_for_guard_combatant(2, context) == 0x00000567U);
  CHECK(bridge.dispatch(UIAction{
      .sequence = 300,
      .payload = GuardCombatantAction{2},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(received_combatant == 2);
  CHECK(received_message == 0x00000567U);

  context.screen = ScreenContext::exploration;
  context.world_presentation = WorldPresentation::outdoor;
  CHECK(!legacy_key_message_for_guard_combatant(2, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 301,
      .payload = GuardCombatantAction{2},
  }).status == DispatchStatus::rejected);
  CHECK(guard_calls == 1);

  context = {ScreenContext::combat, WorldPresentation::none, false};
  CHECK(!legacy_key_message_for_guard_combatant(2, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 302,
      .payload = GuardCombatantAction{2},
  }).status == DispatchStatus::rejected);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {-1, 256}) {
    CHECK(!legacy_key_message_for_guard_combatant(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 303,
        .payload = GuardCombatantAction{invalid},
    }).status == DispatchStatus::rejected);
  }

  accept_guard = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 304,
      .payload = GuardCombatantAction{3},
  }).status == DispatchStatus::failed);
  CHECK(guard_calls == 2);

  RuntimeLegacyCommandBridge without_guard(
      [&context] { return context; },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      });
  CHECK(without_guard.dispatch(UIAction{
      .sequence = 305,
      .payload = GuardCombatantAction{2},
  }).status == DispatchStatus::unsupported);
}

void test_finish_combatant_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int guard_calls = 0;
  CombatantId received_guard_combatant = -1;
  uint32_t received_guard_message = 0;
  int finish_calls = 0;
  bool accept_finish = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [&guard_calls, &received_guard_combatant, &received_guard_message](
          CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext&) {
        ++guard_calls;
        received_guard_combatant = combatant;
        received_guard_message = message;
        return true;
      },
      [&finish_calls, &accept_finish, &received_combatant, &received_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++finish_calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept_finish;
      });

  CHECK(legacy_key_message_for_finish_combatant(2, context) ==
      0x00000366U);
  CHECK(bridge.dispatch(UIAction{
      .sequence = 319,
      .payload = GuardCombatantAction{7},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(received_guard_combatant == 7);
  CHECK(received_guard_message == 0x00000567U);
  CHECK(finish_calls == 0);
  CHECK(bridge.dispatch(UIAction{
      .sequence = 320,
      .payload = FinishCombatantAction{2},
  }).status == DispatchStatus::handled);
  CHECK(finish_calls == 1);
  CHECK(received_combatant == 2);
  CHECK(received_message == 0x00000366U);
  CHECK(guard_calls == 1);

  context.screen = ScreenContext::exploration;
  context.world_presentation = WorldPresentation::outdoor;
  CHECK(!legacy_key_message_for_finish_combatant(2, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 321,
      .payload = FinishCombatantAction{2},
  }).status == DispatchStatus::rejected);
  CHECK(finish_calls == 1);

  context = {ScreenContext::combat, WorldPresentation::none, false};
  CHECK(!legacy_key_message_for_finish_combatant(2, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 322,
      .payload = FinishCombatantAction{2},
  }).status == DispatchStatus::rejected);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {-1, 256}) {
    CHECK(!legacy_key_message_for_finish_combatant(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 323,
        .payload = FinishCombatantAction{invalid},
    }).status == DispatchStatus::rejected);
  }

  accept_finish = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 324,
      .payload = FinishCombatantAction{3},
  }).status == DispatchStatus::failed);
  CHECK(finish_calls == 2);

  RuntimeLegacyCommandBridge missing_finish_sink(
      [&context] { return context; },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](CombatantId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      RuntimeLegacyFinishCombatantSink{});
  const auto missing_sink = missing_finish_sink.dispatch(UIAction{
      .sequence = 325,
      .payload = FinishCombatantAction{2},
  });
  CHECK(missing_sink.status == DispatchStatus::failed);
  CHECK(missing_sink.detail.find("finish-combatant sink") !=
      std::string::npos);

  RuntimeLegacyCommandBridge without_finish(
      [&context] { return context; },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](CombatantId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      });
  CHECK(without_finish.dispatch(UIAction{
      .sequence = 326,
      .payload = FinishCombatantAction{2},
  }).status == DispatchStatus::unsupported);
}

void test_delay_combatant_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int guard_calls = 0;
  int finish_calls = 0;
  int delay_calls = 0;
  bool accept_delay = true;
  CombatantId received_guard_combatant = -1;
  CombatantId received_finish_combatant = -1;
  CombatantId received_delay_combatant = -1;
  uint32_t received_guard_message = 0;
  uint32_t received_finish_message = 0;
  uint32_t received_delay_message = 0;

  RuntimeLegacyGuardCombatantSink guard_sink =
      [&guard_calls, &received_guard_combatant, &received_guard_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++guard_calls;
        received_guard_combatant = combatant;
        received_guard_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyFinishCombatantSink finish_sink =
      [&finish_calls, &received_finish_combatant, &received_finish_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++finish_calls;
        received_finish_combatant = combatant;
        received_finish_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyDelayCombatantSink delay_sink =
      [&delay_calls, &accept_delay, &received_delay_combatant,
          &received_delay_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++delay_calls;
        received_delay_combatant = combatant;
        received_delay_message = message;
        CHECK(captured_context == context);
        return accept_delay;
      };

  const RuntimeLegacyMovementSink movement_sink =
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyPartySelectionSink party_selection_sink =
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; };
  const RuntimeLegacyOpenInventorySink inventory_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSpellbookSink spellbook_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSaveGameSink save_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenLoadGameSink load_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };

  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 340,
      .payload = GuardCombatantAction{7},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 0);
  CHECK(delay_calls == 0);
  CHECK(received_guard_combatant == 7);
  CHECK(received_guard_message == 0x00000567U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 341,
      .payload = FinishCombatantAction{8},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 0);
  CHECK(received_finish_combatant == 8);
  CHECK(received_finish_message == 0x00000366U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 342,
      .payload = DelayCombatantAction{9},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(received_delay_combatant == 9);
  CHECK(received_delay_message == 0x00000264U);

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_delay_combatant(0, context) ==
        0x00000264U);
    CHECK(legacy_key_message_for_delay_combatant(255, context) ==
        0x00000264U);
  }
  context.world_presentation = WorldPresentation::none;
  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 343,
        .payload = DelayCombatantAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(delay_calls == 3);
  CHECK(received_delay_combatant == 255);
  CHECK(received_delay_message == 0x00000264U);

  constexpr std::array non_combat_screens{
      ScreenContext::title,
      ScreenContext::party_selection,
      ScreenContext::party_creation,
      ScreenContext::exploration,
      ScreenContext::dungeon,
      ScreenContext::inventory,
      ScreenContext::shop,
      ScreenContext::encounter,
      ScreenContext::ending,
  };
  for (const auto screen : non_combat_screens) {
    context.screen = screen;
    CHECK(!legacy_key_message_for_delay_combatant(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 344,
        .payload = DelayCombatantAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(delay_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_delay_combatant(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 345,
      .payload = DelayCombatantAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(delay_calls == 3);

  context.adaptive_eligible = true;
  constexpr std::array invalid_combatants{
      std::numeric_limits<CombatantId>::min(),
      CombatantId{-1},
      CombatantId{256},
      std::numeric_limits<CombatantId>::max(),
  };
  for (const auto invalid : invalid_combatants) {
    CHECK(!legacy_key_message_for_delay_combatant(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 346,
        .payload = DelayCombatantAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(delay_calls == 3);

  accept_delay = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 347,
      .payload = DelayCombatantAction{3},
  }).status == DispatchStatus::failed);
  CHECK(delay_calls == 4);
  accept_delay = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink);
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 348,
      .payload = DelayCombatantAction{3},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(delay_calls == 4);

  RuntimeLegacyCommandBridge missing_delay_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      RuntimeLegacyDelayCombatantSink{});
  const auto no_sink = missing_delay_sink.dispatch(UIAction{
      .sequence = 349,
      .payload = DelayCombatantAction{3},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("delay-combatant sink") != std::string::npos);
  CHECK(delay_calls == 4);

  RuntimeLegacyCommandBridge without_delay(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink);
  CHECK(without_delay.dispatch(UIAction{
      .sequence = 350,
      .payload = DelayCombatantAction{3},
  }).status == DispatchStatus::unsupported);
  CHECK(delay_calls == 4);

  RuntimeLegacyDelayCombatantSink throwing_delay_sink =
      [](CombatantId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("delay sink failure");
      };
  RuntimeLegacyCommandBridge delay_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      throwing_delay_sink);
  const auto thrown = delay_sink_throws.dispatch(UIAction{
      .sequence = 351,
      .payload = DelayCombatantAction{3},
  });
  CHECK(thrown.status == DispatchStatus::failed);
  CHECK(thrown.detail.find("delay sink failure") != std::string::npos);
}

void test_named_combat_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int guard_calls = 0;
  int finish_calls = 0;
  int delay_calls = 0;
  int center_calls = 0;
  int switch_weapon_calls = 0;
  int cycle_focus_calls = 0;
  int open_combat_items_calls = 0;
  bool accept_center = true;
  bool accept_switch_weapon = true;
  CombatantId received_guard_combatant = -1;
  CombatantId received_finish_combatant = -1;
  CombatantId received_delay_combatant = -1;
  CombatantId received_center_combatant = -1;
  CombatantId received_switch_weapon_combatant = -1;
  CombatantId received_cycle_focus_combatant = -1;
  CombatantId received_items_combatant = -1;
  PartyMemberId received_items_member = 0;
  uint32_t received_guard_message = 0;
  uint32_t received_finish_message = 0;
  uint32_t received_delay_message = 0;
  uint32_t received_center_message = 0;
  uint32_t received_switch_weapon_message = 0;
  uint32_t received_cycle_focus_message = 0;
  uint32_t received_items_message = 0;
  CombatFocusDirection received_cycle_focus_direction =
      CombatFocusDirection::next;

  RuntimeLegacyGuardCombatantSink guard_sink =
      [&guard_calls, &received_guard_combatant, &received_guard_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++guard_calls;
        received_guard_combatant = combatant;
        received_guard_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyFinishCombatantSink finish_sink =
      [&finish_calls, &received_finish_combatant, &received_finish_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++finish_calls;
        received_finish_combatant = combatant;
        received_finish_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyDelayCombatantSink delay_sink =
      [&delay_calls, &received_delay_combatant, &received_delay_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++delay_calls;
        received_delay_combatant = combatant;
        received_delay_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyCenterActiveCombatantSink center_sink =
      [&center_calls, &accept_center, &received_center_combatant,
          &received_center_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++center_calls;
        received_center_combatant = combatant;
        received_center_message = message;
        CHECK(captured_context == context);
        return accept_center;
      };
  RuntimeLegacySwitchWeaponSink switch_weapon_sink =
      [&switch_weapon_calls, &accept_switch_weapon,
          &received_switch_weapon_combatant,
          &received_switch_weapon_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++switch_weapon_calls;
        received_switch_weapon_combatant = combatant;
        received_switch_weapon_message = message;
        CHECK(captured_context == context);
        return accept_switch_weapon;
      };
  RuntimeLegacyCycleCombatFocusSink cycle_focus_sink =
      [&cycle_focus_calls, &received_cycle_focus_combatant,
          &received_cycle_focus_direction, &received_cycle_focus_message,
          &context](CombatantId combatant,
          CombatFocusDirection direction,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++cycle_focus_calls;
        received_cycle_focus_combatant = combatant;
        received_cycle_focus_direction = direction;
        received_cycle_focus_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyOpenCombatItemsSink open_combat_items_sink =
      [&open_combat_items_calls, &received_items_combatant,
          &received_items_member, &received_items_message,
          &context](CombatantId combatant,
          PartyMemberId member,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++open_combat_items_calls;
        received_items_combatant = combatant;
        received_items_member = member;
        received_items_message = message;
        CHECK(captured_context == context);
        return true;
      };

  const RuntimeLegacyMovementSink movement_sink =
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyPartySelectionSink party_selection_sink =
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; };
  const RuntimeLegacyOpenInventorySink inventory_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSpellbookSink spellbook_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSaveGameSink save_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenLoadGameSink load_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };

  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .guard_combatant = guard_sink,
          .finish_combatant = finish_sink,
          .delay_combatant = delay_sink,
          .center_active_combatant = center_sink,
          .switch_weapon = switch_weapon_sink,
          .cycle_combat_focus = cycle_focus_sink,
          .open_combat_items = open_combat_items_sink,
      });

  CHECK(bridge.dispatch(UIAction{
      .sequence = 360,
      .payload = GuardCombatantAction{6},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 0);
  CHECK(delay_calls == 0);
  CHECK(center_calls == 0);
  CHECK(switch_weapon_calls == 0);
  CHECK(received_guard_combatant == 6);
  CHECK(received_guard_message == 0x00000567U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 361,
      .payload = FinishCombatantAction{7},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 0);
  CHECK(center_calls == 0);
  CHECK(switch_weapon_calls == 0);
  CHECK(received_finish_combatant == 7);
  CHECK(received_finish_message == 0x00000366U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 362,
      .payload = DelayCombatantAction{8},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 0);
  CHECK(switch_weapon_calls == 0);
  CHECK(received_delay_combatant == 8);
  CHECK(received_delay_message == 0x00000264U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 363,
      .payload = CenterActiveCombatantAction{9},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 0);
  CHECK(received_center_combatant == 9);
  CHECK(received_center_message == 0x00000863U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 364,
      .payload = SwitchWeaponSetAction{10},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 1);
  CHECK(cycle_focus_calls == 0);
  CHECK(received_switch_weapon_combatant == 10);
  CHECK(received_switch_weapon_message == 0x00000D77U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 365,
      .payload = CycleCombatFocusAction{
          .combatant = 11,
          .direction = CombatFocusDirection::previous,
      },
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 1);
  CHECK(cycle_focus_calls == 1);
  CHECK(received_cycle_focus_combatant == 11);
  CHECK(received_cycle_focus_direction == CombatFocusDirection::previous);
  CHECK(received_cycle_focus_message == 0x00002370U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 366,
      .payload = CycleCombatFocusAction{
          .combatant = 12,
          .direction = CombatFocusDirection::next,
      },
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 1);
  CHECK(cycle_focus_calls == 2);
  CHECK(received_cycle_focus_combatant == 12);
  CHECK(received_cycle_focus_direction == CombatFocusDirection::next);
  CHECK(received_cycle_focus_message == 0x00002D6EU);
  CHECK(open_combat_items_calls == 0);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 367,
      .payload = OpenCombatItemsAction{
          .combatant = 13,
          .member = 4,
      },
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 1);
  CHECK(cycle_focus_calls == 2);
  CHECK(open_combat_items_calls == 1);
  CHECK(received_items_combatant == 13);
  CHECK(received_items_member == 4);
  CHECK(received_items_message == 0x00002269U);

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_center_active_combatant(0, context) ==
        0x00000863U);
    CHECK(legacy_key_message_for_center_active_combatant(255, context) ==
        0x00000863U);
  }
  context.world_presentation = WorldPresentation::none;
  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 365,
        .payload = CenterActiveCombatantAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(center_calls == 3);
  CHECK(received_center_combatant == 255);
  CHECK(received_center_message == 0x00000863U);

  constexpr std::array non_combat_screens{
      ScreenContext::title,
      ScreenContext::party_selection,
      ScreenContext::party_creation,
      ScreenContext::exploration,
      ScreenContext::dungeon,
      ScreenContext::inventory,
      ScreenContext::shop,
      ScreenContext::encounter,
      ScreenContext::ending,
  };
  for (const auto screen : non_combat_screens) {
    context.screen = screen;
    CHECK(!legacy_key_message_for_center_active_combatant(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 366,
        .payload = CenterActiveCombatantAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(center_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_center_active_combatant(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 367,
      .payload = CenterActiveCombatantAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(center_calls == 3);

  context.adaptive_eligible = true;
  constexpr std::array invalid_combatants{
      std::numeric_limits<CombatantId>::min(),
      CombatantId{-1},
      CombatantId{256},
      std::numeric_limits<CombatantId>::max(),
  };
  for (const auto invalid : invalid_combatants) {
    CHECK(!legacy_key_message_for_center_active_combatant(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 368,
        .payload = CenterActiveCombatantAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(center_calls == 3);

  accept_center = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 369,
      .payload = CenterActiveCombatantAction{3},
  }).status == DispatchStatus::failed);
  CHECK(center_calls == 4);
  accept_center = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink,
      center_sink);
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 370,
      .payload = CenterActiveCombatantAction{3},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(center_calls == 4);

  RuntimeLegacyCommandBridge missing_center_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink,
      RuntimeLegacyCenterActiveCombatantSink{});
  const auto no_sink = missing_center_sink.dispatch(UIAction{
      .sequence = 371,
      .payload = CenterActiveCombatantAction{3},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("center-active-combatant sink") !=
      std::string::npos);
  CHECK(center_calls == 4);

  RuntimeLegacyCommandBridge without_center(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink);
  CHECK(without_center.dispatch(UIAction{
      .sequence = 372,
      .payload = CenterActiveCombatantAction{3},
  }).status == DispatchStatus::unsupported);
  CHECK(center_calls == 4);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("center provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink,
      center_sink);
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 373,
      .payload = CenterActiveCombatantAction{3},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("center provider failure") !=
      std::string::npos);
  CHECK(center_calls == 4);

  RuntimeLegacyCenterActiveCombatantSink throwing_center_sink =
      [](CombatantId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("center sink failure");
      };
  RuntimeLegacyCommandBridge center_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink,
      throwing_center_sink);
  const auto sink_thrown = center_sink_throws.dispatch(UIAction{
      .sequence = 374,
      .payload = CenterActiveCombatantAction{3},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("center sink failure") != std::string::npos);
}

void test_switch_weapon_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int switch_weapon_calls = 0;
  bool accept_switch_weapon = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  RuntimeLegacySwitchWeaponSink switch_weapon_sink =
      [&switch_weapon_calls, &accept_switch_weapon, &received_combatant,
          &received_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++switch_weapon_calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept_switch_weapon;
      };
  const RuntimeLegacyMovementSink movement_sink =
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyPartySelectionSink party_selection_sink =
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; };
  const RuntimeLegacyOpenInventorySink inventory_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSpellbookSink spellbook_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSaveGameSink save_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenLoadGameSink load_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };

  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .switch_weapon = switch_weapon_sink,
      });

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_switch_weapon(0, context) == 0x00000D77U);
    CHECK(legacy_key_message_for_switch_weapon(255, context) == 0x00000D77U);
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 375,
      .payload = SwitchWeaponSetAction{9},
  }).status == DispatchStatus::handled);
  CHECK(switch_weapon_calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_message == 0x00000D77U);

  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 376,
        .payload = SwitchWeaponSetAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(switch_weapon_calls == 3);
  CHECK(received_combatant == 255);
  CHECK(received_message == 0x00000D77U);

  constexpr std::array non_combat_screens{
      ScreenContext::title,
      ScreenContext::party_selection,
      ScreenContext::party_creation,
      ScreenContext::exploration,
      ScreenContext::dungeon,
      ScreenContext::inventory,
      ScreenContext::shop,
      ScreenContext::encounter,
      ScreenContext::ending,
  };
  for (const auto screen : non_combat_screens) {
    context.screen = screen;
    CHECK(!legacy_key_message_for_switch_weapon(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 377,
        .payload = SwitchWeaponSetAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(switch_weapon_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_switch_weapon(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 378,
      .payload = SwitchWeaponSetAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(switch_weapon_calls == 3);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    CHECK(!legacy_key_message_for_switch_weapon(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 379,
        .payload = SwitchWeaponSetAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(switch_weapon_calls == 3);

  accept_switch_weapon = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 380,
      .payload = SwitchWeaponSetAction{3},
  }).status == DispatchStatus::failed);
  CHECK(switch_weapon_calls == 4);
  accept_switch_weapon = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .switch_weapon = switch_weapon_sink,
      });
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 381,
      .payload = SwitchWeaponSetAction{3},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(switch_weapon_calls == 4);

  RuntimeLegacyCommandBridge missing_switch_weapon_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .switch_weapon = RuntimeLegacySwitchWeaponSink{},
      });
  const auto no_sink = missing_switch_weapon_sink.dispatch(UIAction{
      .sequence = 382,
      .payload = SwitchWeaponSetAction{3},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("switch-weapon sink") != std::string::npos);
  CHECK(switch_weapon_calls == 4);

  RuntimeLegacyCommandBridge without_switch_weapon(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(without_switch_weapon.dispatch(UIAction{
      .sequence = 383,
      .payload = SwitchWeaponSetAction{3},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("switch weapon provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .switch_weapon = switch_weapon_sink,
      });
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 384,
      .payload = SwitchWeaponSetAction{3},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("switch weapon provider failure") !=
      std::string::npos);
  CHECK(switch_weapon_calls == 4);

  RuntimeLegacySwitchWeaponSink throwing_switch_weapon_sink =
      [](CombatantId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("switch weapon sink failure");
      };
  RuntimeLegacyCommandBridge switch_weapon_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .switch_weapon = throwing_switch_weapon_sink,
      });
  const auto sink_thrown = switch_weapon_sink_throws.dispatch(UIAction{
      .sequence = 385,
      .payload = SwitchWeaponSetAction{3},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("switch weapon sink failure") !=
      std::string::npos);
}

void test_cycle_combat_focus_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int cycle_calls = 0;
  bool accept_cycle = true;
  CombatantId received_combatant = -1;
  CombatFocusDirection received_direction = CombatFocusDirection::next;
  uint32_t received_message = 0;
  const RuntimeLegacyCycleCombatFocusSink cycle_sink =
      [&cycle_calls, &accept_cycle, &received_combatant, &received_direction,
          &received_message, &context](CombatantId combatant,
          CombatFocusDirection direction,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++cycle_calls;
        received_combatant = combatant;
        received_direction = direction;
        received_message = message;
        CHECK(captured_context == context);
        return accept_cycle;
      };
  const RuntimeLegacyMovementSink movement_sink =
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyPartySelectionSink party_selection_sink =
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; };
  const RuntimeLegacyOpenInventorySink inventory_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSpellbookSink spellbook_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSaveGameSink save_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenLoadGameSink load_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .cycle_combat_focus = cycle_sink,
      });

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    for (const CombatantId boundary : {0, 255}) {
      CHECK(legacy_key_message_for_cycle_combat_focus(
                boundary, CombatFocusDirection::previous, context) ==
          0x00002370U);
      CHECK(legacy_key_message_for_cycle_combat_focus(
                boundary, CombatFocusDirection::next, context) ==
          0x00002D6EU);
    }
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 386,
      .payload = CycleCombatFocusAction{
          .combatant = 3,
          .direction = CombatFocusDirection::previous,
      },
  }).status == DispatchStatus::handled);
  CHECK(cycle_calls == 1);
  CHECK(received_combatant == 3);
  CHECK(received_direction == CombatFocusDirection::previous);
  CHECK(received_message == 0x00002370U);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 387,
      .payload = CycleCombatFocusAction{
          .combatant = 4,
          .direction = CombatFocusDirection::next,
      },
  }).status == DispatchStatus::handled);
  CHECK(cycle_calls == 2);
  CHECK(received_combatant == 4);
  CHECK(received_direction == CombatFocusDirection::next);
  CHECK(received_message == 0x00002D6EU);

  constexpr std::array non_combat_screens{
      ScreenContext::title,
      ScreenContext::party_selection,
      ScreenContext::party_creation,
      ScreenContext::exploration,
      ScreenContext::dungeon,
      ScreenContext::inventory,
      ScreenContext::shop,
      ScreenContext::encounter,
      ScreenContext::ending,
  };
  for (const auto screen : non_combat_screens) {
    context.screen = screen;
    for (const auto direction : {
             CombatFocusDirection::previous,
             CombatFocusDirection::next,
         }) {
      CHECK(!legacy_key_message_for_cycle_combat_focus(
          4, direction, context));
      CHECK(bridge.dispatch(UIAction{
          .sequence = 388,
          .payload = CycleCombatFocusAction{
              .combatant = 4,
              .direction = direction,
          },
      }).status == DispatchStatus::rejected);
    }
  }
  CHECK(cycle_calls == 2);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  for (const auto direction : {
           CombatFocusDirection::previous,
           CombatFocusDirection::next,
       }) {
    CHECK(!legacy_key_message_for_cycle_combat_focus(4, direction, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 389,
        .payload = CycleCombatFocusAction{
            .combatant = 4,
            .direction = direction,
        },
    }).status == DispatchStatus::rejected);
  }
  CHECK(cycle_calls == 2);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    for (const auto direction : {
             CombatFocusDirection::previous,
             CombatFocusDirection::next,
         }) {
      CHECK(!legacy_key_message_for_cycle_combat_focus(
          invalid, direction, context));
      CHECK(bridge.dispatch(UIAction{
          .sequence = 390,
          .payload = CycleCombatFocusAction{
              .combatant = invalid,
              .direction = direction,
          },
      }).status == DispatchStatus::rejected);
    }
  }
  CHECK(cycle_calls == 2);

  for (const auto invalid_direction : {
           static_cast<CombatFocusDirection>(-1),
           static_cast<CombatFocusDirection>(2),
           static_cast<CombatFocusDirection>(255),
       }) {
    CHECK(!legacy_key_message_for_cycle_combat_focus(
        4, invalid_direction, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 391,
        .payload = CycleCombatFocusAction{
            .combatant = 4,
            .direction = invalid_direction,
        },
    }).status == DispatchStatus::rejected);
  }
  CHECK(cycle_calls == 2);

  accept_cycle = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 392,
      .payload = CycleCombatFocusAction{
          .combatant = 5,
          .direction = CombatFocusDirection::previous,
      },
  }).status == DispatchStatus::failed);
  CHECK(cycle_calls == 3);
  accept_cycle = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .cycle_combat_focus = cycle_sink,
      });
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 393,
      .payload = CycleCombatFocusAction{
          .combatant = 5,
          .direction = CombatFocusDirection::next,
      },
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(cycle_calls == 3);

  RuntimeLegacyCommandBridge empty_cycle_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .cycle_combat_focus = RuntimeLegacyCycleCombatFocusSink{},
      });
  const auto no_sink = empty_cycle_sink.dispatch(UIAction{
      .sequence = 394,
      .payload = CycleCombatFocusAction{
          .combatant = 5,
          .direction = CombatFocusDirection::next,
      },
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("cycle-combat-focus sink") != std::string::npos);
  CHECK(cycle_calls == 3);

  RuntimeLegacyCommandBridge without_cycle_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(without_cycle_sink.dispatch(UIAction{
      .sequence = 395,
      .payload = CycleCombatFocusAction{
          .combatant = 5,
          .direction = CombatFocusDirection::next,
      },
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("cycle focus provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .cycle_combat_focus = cycle_sink,
      });
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 396,
      .payload = CycleCombatFocusAction{
          .combatant = 5,
          .direction = CombatFocusDirection::previous,
      },
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("cycle focus provider failure") !=
      std::string::npos);
  CHECK(cycle_calls == 3);

  const RuntimeLegacyCycleCombatFocusSink throwing_cycle_sink =
      [](CombatantId,
          CombatFocusDirection,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("cycle focus sink failure");
      };
  RuntimeLegacyCommandBridge cycle_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .cycle_combat_focus = throwing_cycle_sink,
      });
  const auto sink_thrown = cycle_sink_throws.dispatch(UIAction{
      .sequence = 397,
      .payload = CycleCombatFocusAction{
          .combatant = 5,
          .direction = CombatFocusDirection::previous,
      },
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("cycle focus sink failure") !=
      std::string::npos);
}

void test_open_combat_items_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int items_calls = 0;
  bool accept_items = true;
  CombatantId received_combatant = -1;
  PartyMemberId received_member = 0;
  uint32_t received_message = 0;
  const RuntimeLegacyOpenCombatItemsSink items_sink =
      [&items_calls, &accept_items, &received_combatant, &received_member,
          &received_message, &context](CombatantId combatant,
          PartyMemberId member,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++items_calls;
        received_combatant = combatant;
        received_member = member;
        received_message = message;
        CHECK(captured_context == context);
        return accept_items;
      };
  const RuntimeLegacyMovementSink movement_sink =
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyPartySelectionSink party_selection_sink =
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; };
  const RuntimeLegacyOpenInventorySink inventory_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSpellbookSink spellbook_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSaveGameSink save_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenLoadGameSink load_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  RuntimeLegacyCommandBridge bridge(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .open_combat_items = items_sink,
      });

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    for (const CombatantId combatant : {0, 255}) {
      for (const PartyMemberId member : {PartyMemberId{0}, PartyMemberId{255}}) {
        CHECK(legacy_key_message_for_open_combat_items(
                  combatant, member, context) == 0x00002269U);
      }
    }
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 398,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  }).status == DispatchStatus::handled);
  CHECK(items_calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_member == 3);
  CHECK(received_message == 0x00002269U);

  for (const auto boundary : {
           OpenCombatItemsAction{.combatant = 0, .member = PartyMemberId{255}},
           OpenCombatItemsAction{.combatant = 255, .member = PartyMemberId{0}},
       }) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 399,
        .payload = boundary,
    }).status == DispatchStatus::handled);
  }
  CHECK(items_calls == 3);
  CHECK(received_combatant == 255);
  CHECK(received_member == 0);
  CHECK(received_message == 0x00002269U);

  constexpr std::array non_combat_screens{
      ScreenContext::title,
      ScreenContext::party_selection,
      ScreenContext::party_creation,
      ScreenContext::exploration,
      ScreenContext::dungeon,
      ScreenContext::inventory,
      ScreenContext::shop,
      ScreenContext::encounter,
      ScreenContext::ending,
  };
  for (const auto screen : non_combat_screens) {
    context.screen = screen;
    CHECK(!legacy_key_message_for_open_combat_items(9, 3, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 400,
        .payload = OpenCombatItemsAction{
            .combatant = 9,
            .member = 3,
        },
    }).status == DispatchStatus::rejected);
  }
  CHECK(items_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_open_combat_items(9, 3, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 401,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  }).status == DispatchStatus::rejected);
  CHECK(items_calls == 3);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    for (const PartyMemberId member : {PartyMemberId{0}, PartyMemberId{255}}) {
      CHECK(!legacy_key_message_for_open_combat_items(invalid, member, context));
      CHECK(bridge.dispatch(UIAction{
          .sequence = 402,
          .payload = OpenCombatItemsAction{
              .combatant = invalid,
              .member = member,
          },
      }).status == DispatchStatus::rejected);
    }
  }
  CHECK(items_calls == 3);

  accept_items = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 403,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  }).status == DispatchStatus::failed);
  CHECK(items_calls == 4);
  accept_items = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .open_combat_items = items_sink,
      });
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 404,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(items_calls == 4);

  RuntimeLegacyCommandBridge empty_items_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .open_combat_items = RuntimeLegacyOpenCombatItemsSink{},
      });
  const auto no_sink = empty_items_sink.dispatch(UIAction{
      .sequence = 405,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("open-combat-items sink") != std::string::npos);
  CHECK(items_calls == 4);

  RuntimeLegacyCommandBridge without_items_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(without_items_sink.dispatch(UIAction{
      .sequence = 406,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("combat items provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .open_combat_items = items_sink,
      });
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 407,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("combat items provider failure") !=
      std::string::npos);
  CHECK(items_calls == 4);

  const RuntimeLegacyOpenCombatItemsSink throwing_items_sink =
      [](CombatantId,
          PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("combat items sink failure");
      };
  RuntimeLegacyCommandBridge items_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .open_combat_items = throwing_items_sink,
      });
  const auto sink_thrown = items_sink_throws.dispatch(UIAction{
      .sequence = 408,
      .payload = OpenCombatItemsAction{
          .combatant = 9,
          .member = 3,
      },
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("combat items sink failure") !=
      std::string::npos);
}

void test_named_combat_sink_registration_semantics() {
  const RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  const RuntimeLegacyMovementSink movement_sink =
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyPartySelectionSink party_selection_sink =
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; };
  const RuntimeLegacyOpenInventorySink inventory_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSpellbookSink spellbook_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSaveGameSink save_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenLoadGameSink load_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const std::array actions{
      UIAction{
          .sequence = 380,
          .payload = GuardCombatantAction{1},
      },
      UIAction{
          .sequence = 381,
          .payload = FinishCombatantAction{2},
      },
      UIAction{
          .sequence = 382,
          .payload = DelayCombatantAction{3},
      },
      UIAction{
          .sequence = 383,
          .payload = CenterActiveCombatantAction{4},
      },
      UIAction{
          .sequence = 384,
          .payload = SwitchWeaponSetAction{5},
      },
      UIAction{
          .sequence = 385,
          .payload = CycleCombatFocusAction{
              .combatant = 6,
              .direction = CombatFocusDirection::previous,
          },
      },
      UIAction{
          .sequence = 386,
          .payload = OpenCombatItemsAction{
              .combatant = 7,
              .member = 3,
          },
      },
  };

  RuntimeLegacyCommandBridge no_combat_sinks(
      [context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  for (const auto& action : actions) {
    CHECK(no_combat_sinks.dispatch(action).status ==
        DispatchStatus::unsupported);
  }

  RuntimeLegacyCommandBridge empty_combat_sinks(
      [context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .guard_combatant = RuntimeLegacyGuardCombatantSink{},
          .finish_combatant = RuntimeLegacyFinishCombatantSink{},
          .delay_combatant = RuntimeLegacyDelayCombatantSink{},
          .center_active_combatant =
              RuntimeLegacyCenterActiveCombatantSink{},
          .switch_weapon = RuntimeLegacySwitchWeaponSink{},
          .cycle_combat_focus = RuntimeLegacyCycleCombatFocusSink{},
          .open_combat_items = RuntimeLegacyOpenCombatItemsSink{},
      });
  for (const auto& action : actions) {
    const auto result = empty_combat_sinks.dispatch(action);
    CHECK(result.status == DispatchStatus::failed);
    CHECK(result.detail.find("sink is not available") != std::string::npos);
  }

  int guard_calls = 0;
  int center_calls = 0;
  RuntimeLegacyCommandBridge sparse_combat_sinks(
      [context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .guard_combatant =
              [&guard_calls](CombatantId combatant,
                  uint32_t message,
                  const RuntimeLegacyCommandContext&) {
                ++guard_calls;
                CHECK(combatant == 5);
                CHECK(message == 0x00000567U);
                return true;
              },
          .center_active_combatant =
              [&center_calls](CombatantId combatant,
                  uint32_t message,
                  const RuntimeLegacyCommandContext&) {
                ++center_calls;
                CHECK(combatant == 6);
                CHECK(message == 0x00000863U);
                return true;
              },
      });
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 384,
      .payload = GuardCombatantAction{5},
  }).status == DispatchStatus::handled);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 385,
      .payload = FinishCombatantAction{5},
  }).status == DispatchStatus::unsupported);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 386,
      .payload = DelayCombatantAction{5},
  }).status == DispatchStatus::unsupported);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 387,
      .payload = CenterActiveCombatantAction{6},
  }).status == DispatchStatus::handled);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 388,
      .payload = SwitchWeaponSetAction{6},
  }).status == DispatchStatus::unsupported);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 389,
      .payload = CycleCombatFocusAction{
          .combatant = 6,
          .direction = CombatFocusDirection::next,
      },
  }).status == DispatchStatus::unsupported);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 390,
      .payload = OpenCombatItemsAction{
          .combatant = 6,
          .member = 2,
      },
  }).status == DispatchStatus::unsupported);
  CHECK(guard_calls == 1);
  CHECK(center_calls == 1);
}

void test_positional_combat_constructor_compatibility() {
  const RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  const RuntimeLegacyMovementSink movement_sink =
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyPartySelectionSink party_selection_sink =
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; };
  const RuntimeLegacyOpenInventorySink inventory_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSpellbookSink spellbook_sink =
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenSaveGameSink save_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };
  const RuntimeLegacyOpenLoadGameSink load_sink =
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      };

  int guard_calls = 0;
  int finish_calls = 0;
  int delay_calls = 0;
  int center_calls = 0;
  const RuntimeLegacyGuardCombatantSink guard_sink =
      [&guard_calls](CombatantId, uint32_t, const RuntimeLegacyCommandContext&) {
        ++guard_calls;
        return true;
      };
  const RuntimeLegacyFinishCombatantSink finish_sink =
      [&finish_calls](
          CombatantId, uint32_t, const RuntimeLegacyCommandContext&) {
        ++finish_calls;
        return true;
      };
  const RuntimeLegacyDelayCombatantSink delay_sink =
      [&delay_calls](CombatantId, uint32_t, const RuntimeLegacyCommandContext&) {
        ++delay_calls;
        return true;
      };
  const RuntimeLegacyCenterActiveCombatantSink center_sink =
      [&center_calls](
          CombatantId, uint32_t, const RuntimeLegacyCommandContext&) {
        ++center_calls;
        return true;
      };

  RuntimeLegacyCommandBridge guard_only(
      [context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink);
  CHECK(guard_only.dispatch(UIAction{
      .sequence = 390,
      .payload = GuardCombatantAction{1},
  }).status == DispatchStatus::handled);
  CHECK(guard_only.dispatch(UIAction{
      .sequence = 391,
      .payload = FinishCombatantAction{1},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge through_finish(
      [context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink);
  CHECK(through_finish.dispatch(UIAction{
      .sequence = 392,
      .payload = FinishCombatantAction{2},
  }).status == DispatchStatus::handled);
  CHECK(through_finish.dispatch(UIAction{
      .sequence = 393,
      .payload = DelayCombatantAction{2},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge through_delay(
      [context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink);
  CHECK(through_delay.dispatch(UIAction{
      .sequence = 394,
      .payload = DelayCombatantAction{3},
  }).status == DispatchStatus::handled);
  CHECK(through_delay.dispatch(UIAction{
      .sequence = 395,
      .payload = CenterActiveCombatantAction{3},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge through_center(
      [context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      guard_sink,
      finish_sink,
      delay_sink,
      center_sink);
  CHECK(through_center.dispatch(UIAction{
      .sequence = 396,
      .payload = CenterActiveCombatantAction{4},
  }).status == DispatchStatus::handled);
  CHECK(through_center.dispatch(UIAction{
      .sequence = 397,
      .payload = SwitchWeaponSetAction{4},
  }).status == DispatchStatus::unsupported);
  CHECK(through_center.dispatch(UIAction{
      .sequence = 398,
      .payload = CycleCombatFocusAction{
          .combatant = 4,
          .direction = CombatFocusDirection::next,
      },
  }).status == DispatchStatus::unsupported);
  CHECK(through_center.dispatch(UIAction{
      .sequence = 399,
      .payload = OpenCombatItemsAction{
          .combatant = 4,
          .member = 2,
      },
  }).status == DispatchStatus::unsupported);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
}

void test_exception_boundary() {
  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("context failure");
      },
      [](uint32_t) { return true; });
  CHECK(provider_throws.dispatch(UIAction{
      .sequence = 1,
      .payload = MovePartyAction{MovementCommand::north},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge sink_throws(
      [] {
        return RuntimeLegacyCommandContext{
            .screen = ScreenContext::exploration,
            .world_presentation = WorldPresentation::outdoor,
            .adaptive_eligible = true,
        };
      },
      [](uint32_t) -> bool { throw std::runtime_error("sink failure"); });
  CHECK(sink_throws.dispatch(UIAction{
      .sequence = 2,
      .payload = MovePartyAction{MovementCommand::north},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge selection_provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("selection context failure");
      },
      RuntimeLegacyKeySink{[](uint32_t) { return true; }},
      RuntimeLegacyPartySelectionSink{
          [](PartyMemberId, const RuntimeLegacyCommandContext&) {
            return true;
          }});
  CHECK(selection_provider_throws.dispatch(UIAction{
      .sequence = 3,
      .payload = SelectPartyMemberAction{1},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge selection_sink_throws(
      [] {
        return RuntimeLegacyCommandContext{
            .screen = ScreenContext::dungeon,
            .world_presentation = WorldPresentation::dungeon_map,
            .adaptive_eligible = true,
        };
      },
      RuntimeLegacyKeySink{[](uint32_t) { return true; }},
      RuntimeLegacyPartySelectionSink{
          [](PartyMemberId,
              const RuntimeLegacyCommandContext&) -> bool {
            throw std::runtime_error("selection sink failure");
          }});
  CHECK(selection_sink_throws.dispatch(UIAction{
      .sequence = 4,
      .payload = SelectPartyMemberAction{1},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge inventory_sink_throws(
      [] {
        return RuntimeLegacyCommandContext{
            .screen = ScreenContext::exploration,
            .world_presentation = WorldPresentation::outdoor,
            .adaptive_eligible = true,
        };
      },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("inventory sink failure");
      });
  CHECK(inventory_sink_throws.dispatch(UIAction{
      .sequence = 5,
      .payload = OpenInventoryAction{1},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge spellbook_sink_throws(
      [] {
        return RuntimeLegacyCommandContext{
            .screen = ScreenContext::exploration,
            .world_presentation = WorldPresentation::outdoor,
            .adaptive_eligible = true,
        };
      },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("spellbook sink failure");
      });
  CHECK(spellbook_sink_throws.dispatch(UIAction{
      .sequence = 6,
      .payload = OpenSpellbookAction{1},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge save_sink_throws(
      [] {
        return RuntimeLegacyCommandContext{
            .screen = ScreenContext::exploration,
            .world_presentation = WorldPresentation::outdoor,
            .adaptive_eligible = true,
        };
      },
      [](MovementCommand,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId,
          uint32_t,
          const RuntimeLegacyCommandContext&) { return true; },
      [](RuntimeLegacyMenuCommand,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("save chooser sink failure");
      });
  CHECK(save_sink_throws.dispatch(UIAction{
      .sequence = 7,
      .payload = OpenSaveGameAction{},
  }).status == DispatchStatus::failed);

  RuntimeLegacyCommandBridge load_sink_throws(
      [] {
        return RuntimeLegacyCommandContext{
            .screen = ScreenContext::exploration,
            .world_presentation = WorldPresentation::outdoor,
            .adaptive_eligible = true,
        };
      },
      [](MovementCommand, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, const RuntimeLegacyCommandContext&) { return true; },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](PartyMemberId, uint32_t, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand, const RuntimeLegacyCommandContext&) {
        return true;
      },
      [](RuntimeLegacyMenuCommand,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("load chooser sink failure");
      });
  CHECK(load_sink_throws.dispatch(UIAction{
      .sequence = 8,
      .payload = OpenLoadGameAction{},
  }).status == DispatchStatus::failed);
}

} // namespace

int main() {
  try {
    test_outdoor_mapping_and_dispatch();
    test_dungeon_mapping();
    test_fail_closed_context_and_sink();
    test_typed_movement_sink_preserves_context();
    test_typed_party_selection_sink_preserves_id_and_context();
    test_party_selection_fail_closed_boundaries();
    test_party_selection_is_opt_in_and_preserves_movement();
    test_open_inventory_mapping_and_dispatch();
    test_open_spellbook_mapping_and_dispatch();
    test_open_save_game_mapping_and_dispatch();
    test_open_load_game_mapping_and_dispatch();
    test_guard_combatant_mapping_and_dispatch();
    test_finish_combatant_mapping_and_dispatch();
    test_delay_combatant_mapping_and_dispatch();
    test_named_combat_mapping_and_dispatch();
    test_switch_weapon_mapping_and_dispatch();
    test_cycle_combat_focus_mapping_and_dispatch();
    test_open_combat_items_mapping_and_dispatch();
    test_named_combat_sink_registration_semantics();
    test_positional_combat_constructor_compatibility();
    test_exception_boundary();
    std::cout << "RuntimeLegacyCommandBridgeTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "RuntimeLegacyCommandBridgeTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
