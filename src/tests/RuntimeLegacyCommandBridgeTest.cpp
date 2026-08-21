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
static_assert(std::is_same_v<
    RuntimeLegacySwitchWeaponSink,
    RuntimeLegacyAutoCombatantSink>);
static_assert(std::is_same_v<
    RuntimeLegacyAutoCombatantSink,
    RuntimeLegacyShowCombatRangeSink>);
static_assert(std::is_same_v<
    RuntimeLegacyShowCombatRangeSink,
    RuntimeLegacyBandageCombatantSink>);
static_assert(std::is_same_v<
    RuntimeLegacyBandageCombatantSink,
    RuntimeLegacyUndoCombatantSink>);
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
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::auto_combatant),
    std::optional<RuntimeLegacyAutoCombatantSink>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::show_combat_range),
    std::optional<RuntimeLegacyShowCombatRangeSink>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::bandage_combatant),
    std::optional<RuntimeLegacyBandageCombatantSink>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::undo_combatant),
    std::optional<RuntimeLegacyUndoCombatantSink>>);
static_assert(std::is_same_v<
    RuntimeLegacyOpenCombatSpellbookSink,
    std::function<bool(
        CombatantId,
        uint32_t,
        const RuntimeLegacyCommandContext&)>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::open_combat_spellbook),
    std::optional<RuntimeLegacyOpenCombatSpellbookSink>>);
static_assert(std::is_same_v<
    RuntimeLegacyOpenCombatTargetingSink,
    std::function<bool(
        CombatantId,
        uint32_t,
        const RuntimeLegacyCommandContext&)>>);
static_assert(std::is_same_v<
    decltype(RuntimeLegacyCombatActionSinks::open_combat_targeting),
    std::optional<RuntimeLegacyOpenCombatTargetingSink>>);
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
  int auto_combatant_calls = 0;
  int show_combat_range_calls = 0;
  int bandage_combatant_calls = 0;
  bool accept_center = true;
  bool accept_switch_weapon = true;
  CombatantId received_guard_combatant = -1;
  CombatantId received_finish_combatant = -1;
  CombatantId received_delay_combatant = -1;
  CombatantId received_center_combatant = -1;
  CombatantId received_switch_weapon_combatant = -1;
  CombatantId received_cycle_focus_combatant = -1;
  CombatantId received_items_combatant = -1;
  CombatantId received_auto_combatant = -1;
  CombatantId received_range_combatant = -1;
  CombatantId received_bandage_combatant = -1;
  PartyMemberId received_items_member = 0;
  uint32_t received_guard_message = 0;
  uint32_t received_finish_message = 0;
  uint32_t received_delay_message = 0;
  uint32_t received_center_message = 0;
  uint32_t received_switch_weapon_message = 0;
  uint32_t received_cycle_focus_message = 0;
  uint32_t received_items_message = 0;
  uint32_t received_auto_message = 0;
  uint32_t received_range_message = 0;
  uint32_t received_bandage_message = 0;
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
  RuntimeLegacyAutoCombatantSink auto_combatant_sink =
      [&auto_combatant_calls, &received_auto_combatant,
          &received_auto_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++auto_combatant_calls;
        received_auto_combatant = combatant;
        received_auto_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyShowCombatRangeSink show_combat_range_sink =
      [&show_combat_range_calls, &received_range_combatant,
          &received_range_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++show_combat_range_calls;
        received_range_combatant = combatant;
        received_range_message = message;
        CHECK(captured_context == context);
        return true;
      };
  RuntimeLegacyBandageCombatantSink bandage_combatant_sink =
      [&bandage_combatant_calls, &received_bandage_combatant,
          &received_bandage_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++bandage_combatant_calls;
        received_bandage_combatant = combatant;
        received_bandage_message = message;
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
          .auto_combatant = auto_combatant_sink,
          .show_combat_range = show_combat_range_sink,
          .bandage_combatant = bandage_combatant_sink,
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
  CHECK(auto_combatant_calls == 0);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 368,
      .payload = AutoCombatantAction{14},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 1);
  CHECK(cycle_focus_calls == 2);
  CHECK(open_combat_items_calls == 1);
  CHECK(auto_combatant_calls == 1);
  CHECK(received_auto_combatant == 14);
  CHECK(received_auto_message == 0x00000061U);
  CHECK(show_combat_range_calls == 0);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 369,
      .payload = ShowCombatRangeAction{15},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 1);
  CHECK(cycle_focus_calls == 2);
  CHECK(open_combat_items_calls == 1);
  CHECK(auto_combatant_calls == 1);
  CHECK(show_combat_range_calls == 1);
  CHECK(received_range_combatant == 15);
  CHECK(received_range_message == 0x00000F72U);
  CHECK(bandage_combatant_calls == 0);

  CHECK(bridge.dispatch(UIAction{
      .sequence = 370,
      .payload = BandageCombatantAction{16},
  }).status == DispatchStatus::handled);
  CHECK(guard_calls == 1);
  CHECK(finish_calls == 1);
  CHECK(delay_calls == 1);
  CHECK(center_calls == 1);
  CHECK(switch_weapon_calls == 1);
  CHECK(cycle_focus_calls == 2);
  CHECK(open_combat_items_calls == 1);
  CHECK(auto_combatant_calls == 1);
  CHECK(show_combat_range_calls == 1);
  CHECK(bandage_combatant_calls == 1);
  CHECK(received_bandage_combatant == 16);
  CHECK(received_bandage_message == 0x00000B62U);

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

void test_auto_combatant_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int auto_calls = 0;
  bool accept_auto = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  const RuntimeLegacyAutoCombatantSink auto_sink =
      [&auto_calls, &accept_auto, &received_combatant, &received_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++auto_calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept_auto;
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
          .auto_combatant = auto_sink,
      });

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_auto_combatant(0, context) == 0x00000061U);
    CHECK(legacy_key_message_for_auto_combatant(255, context) == 0x00000061U);
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 409,
      .payload = AutoCombatantAction{9},
  }).status == DispatchStatus::handled);
  CHECK(auto_calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_message == 0x00000061U);

  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 410,
        .payload = AutoCombatantAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(auto_calls == 3);
  CHECK(received_combatant == 255);
  CHECK(received_message == 0x00000061U);

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
    CHECK(!legacy_key_message_for_auto_combatant(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 411,
        .payload = AutoCombatantAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(auto_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_auto_combatant(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 412,
      .payload = AutoCombatantAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(auto_calls == 3);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    CHECK(!legacy_key_message_for_auto_combatant(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 413,
        .payload = AutoCombatantAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(auto_calls == 3);

  accept_auto = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 414,
      .payload = AutoCombatantAction{9},
  }).status == DispatchStatus::failed);
  CHECK(auto_calls == 4);
  accept_auto = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .auto_combatant = auto_sink,
      });
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 415,
      .payload = AutoCombatantAction{9},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(auto_calls == 4);

  RuntimeLegacyCommandBridge empty_auto_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .auto_combatant = RuntimeLegacyAutoCombatantSink{},
      });
  const auto no_sink = empty_auto_sink.dispatch(UIAction{
      .sequence = 416,
      .payload = AutoCombatantAction{9},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("auto-combatant sink") != std::string::npos);
  CHECK(auto_calls == 4);

  RuntimeLegacyCommandBridge without_auto_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(without_auto_sink.dispatch(UIAction{
      .sequence = 417,
      .payload = AutoCombatantAction{9},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("auto provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .auto_combatant = auto_sink,
      });
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 418,
      .payload = AutoCombatantAction{9},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("auto provider failure") !=
      std::string::npos);
  CHECK(auto_calls == 4);

  const RuntimeLegacyAutoCombatantSink throwing_auto_sink =
      [](CombatantId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("auto sink failure");
      };
  RuntimeLegacyCommandBridge auto_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .auto_combatant = throwing_auto_sink,
      });
  const auto sink_thrown = auto_sink_throws.dispatch(UIAction{
      .sequence = 419,
      .payload = AutoCombatantAction{9},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("auto sink failure") != std::string::npos);
}

void test_show_combat_range_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int range_calls = 0;
  bool accept_range = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  const RuntimeLegacyShowCombatRangeSink range_sink =
      [&range_calls, &accept_range, &received_combatant, &received_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++range_calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept_range;
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
          .show_combat_range = range_sink,
      });

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_show_combat_range(0, context) ==
        0x00000F72U);
    CHECK(legacy_key_message_for_show_combat_range(255, context) ==
        0x00000F72U);
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 420,
      .payload = ShowCombatRangeAction{9},
  }).status == DispatchStatus::handled);
  CHECK(range_calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_message == 0x00000F72U);

  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 421,
        .payload = ShowCombatRangeAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(range_calls == 3);
  CHECK(received_combatant == 255);
  CHECK(received_message == 0x00000F72U);

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
    CHECK(!legacy_key_message_for_show_combat_range(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 422,
        .payload = ShowCombatRangeAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(range_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_show_combat_range(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 423,
      .payload = ShowCombatRangeAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(range_calls == 3);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    CHECK(!legacy_key_message_for_show_combat_range(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 424,
        .payload = ShowCombatRangeAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(range_calls == 3);

  accept_range = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 425,
      .payload = ShowCombatRangeAction{9},
  }).status == DispatchStatus::failed);
  CHECK(range_calls == 4);
  accept_range = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .show_combat_range = range_sink,
      });
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 426,
      .payload = ShowCombatRangeAction{9},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(range_calls == 4);

  RuntimeLegacyCommandBridge empty_range_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .show_combat_range = RuntimeLegacyShowCombatRangeSink{},
      });
  const auto no_sink = empty_range_sink.dispatch(UIAction{
      .sequence = 427,
      .payload = ShowCombatRangeAction{9},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("show-combat-range sink") != std::string::npos);
  CHECK(range_calls == 4);

  RuntimeLegacyCommandBridge without_range_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(without_range_sink.dispatch(UIAction{
      .sequence = 428,
      .payload = ShowCombatRangeAction{9},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("range provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .show_combat_range = range_sink,
      });
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 429,
      .payload = ShowCombatRangeAction{9},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("range provider failure") !=
      std::string::npos);
  CHECK(range_calls == 4);

  const RuntimeLegacyShowCombatRangeSink throwing_range_sink =
      [](CombatantId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("range sink failure");
      };
  RuntimeLegacyCommandBridge range_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .show_combat_range = throwing_range_sink,
      });
  const auto sink_thrown = range_sink_throws.dispatch(UIAction{
      .sequence = 430,
      .payload = ShowCombatRangeAction{9},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("range sink failure") != std::string::npos);
}

void test_bandage_combatant_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int bandage_calls = 0;
  bool accept_bandage = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  const RuntimeLegacyBandageCombatantSink bandage_sink =
      [&bandage_calls, &accept_bandage, &received_combatant,
          &received_message, &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++bandage_calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept_bandage;
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
          .bandage_combatant = bandage_sink,
      });

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_bandage_combatant(0, context) ==
        0x00000B62U);
    CHECK(legacy_key_message_for_bandage_combatant(255, context) ==
        0x00000B62U);
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 431,
      .payload = BandageCombatantAction{9},
  }).status == DispatchStatus::handled);
  CHECK(bandage_calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_message == 0x00000B62U);

  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 432,
        .payload = BandageCombatantAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(bandage_calls == 3);
  CHECK(received_combatant == 255);
  CHECK(received_message == 0x00000B62U);

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
    CHECK(!legacy_key_message_for_bandage_combatant(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 433,
        .payload = BandageCombatantAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(bandage_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_bandage_combatant(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 434,
      .payload = BandageCombatantAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(bandage_calls == 3);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    CHECK(!legacy_key_message_for_bandage_combatant(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 435,
        .payload = BandageCombatantAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(bandage_calls == 3);

  accept_bandage = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 436,
      .payload = BandageCombatantAction{9},
  }).status == DispatchStatus::failed);
  CHECK(bandage_calls == 4);
  accept_bandage = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .bandage_combatant = bandage_sink,
      });
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 437,
      .payload = BandageCombatantAction{9},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(bandage_calls == 4);

  RuntimeLegacyCommandBridge empty_bandage_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .bandage_combatant = RuntimeLegacyBandageCombatantSink{},
      });
  const auto no_sink = empty_bandage_sink.dispatch(UIAction{
      .sequence = 438,
      .payload = BandageCombatantAction{9},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("bandage-combatant sink") != std::string::npos);
  CHECK(bandage_calls == 4);

  RuntimeLegacyCommandBridge without_bandage_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(without_bandage_sink.dispatch(UIAction{
      .sequence = 439,
      .payload = BandageCombatantAction{9},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("bandage provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .bandage_combatant = bandage_sink,
      });
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 440,
      .payload = BandageCombatantAction{9},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("bandage provider failure") !=
      std::string::npos);
  CHECK(bandage_calls == 4);

  const RuntimeLegacyBandageCombatantSink throwing_bandage_sink =
      [](CombatantId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("bandage sink failure");
      };
  RuntimeLegacyCommandBridge bandage_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .bandage_combatant = throwing_bandage_sink,
      });
  const auto sink_thrown = bandage_sink_throws.dispatch(UIAction{
      .sequence = 441,
      .payload = BandageCombatantAction{9},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("bandage sink failure") != std::string::npos);
}

void test_undo_combatant_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int undo_calls = 0;
  bool accept_undo = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  const RuntimeLegacyUndoCombatantSink undo_sink =
      [&undo_calls, &accept_undo, &received_combatant, &received_message,
          &context](CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++undo_calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept_undo;
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
          .undo_combatant = undo_sink,
      });

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_undo_combatant(0, context) ==
        0x00002075U);
    CHECK(legacy_key_message_for_undo_combatant(255, context) ==
        0x00002075U);
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 442,
      .payload = UndoCombatantAction{9},
  }).status == DispatchStatus::handled);
  CHECK(undo_calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_message == 0x00002075U);

  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 443,
        .payload = UndoCombatantAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(undo_calls == 3);
  CHECK(received_combatant == 255);

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
    CHECK(!legacy_key_message_for_undo_combatant(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 444,
        .payload = UndoCombatantAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(undo_calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_undo_combatant(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 445,
      .payload = UndoCombatantAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(undo_calls == 3);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    CHECK(!legacy_key_message_for_undo_combatant(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 446,
        .payload = UndoCombatantAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(undo_calls == 3);

  accept_undo = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 447,
      .payload = UndoCombatantAction{9},
  }).status == DispatchStatus::failed);
  CHECK(undo_calls == 4);
  accept_undo = true;

  RuntimeLegacyCommandBridge missing_provider(
      RuntimeLegacyContextProvider{},
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .undo_combatant = undo_sink,
      });
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 448,
      .payload = UndoCombatantAction{9},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(undo_calls == 4);

  RuntimeLegacyCommandBridge empty_undo_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .undo_combatant = RuntimeLegacyUndoCombatantSink{},
      });
  const auto no_sink = empty_undo_sink.dispatch(UIAction{
      .sequence = 449,
      .payload = UndoCombatantAction{9},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("undo-combatant sink") != std::string::npos);
  CHECK(undo_calls == 4);

  RuntimeLegacyCommandBridge without_undo_sink(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(without_undo_sink.dispatch(UIAction{
      .sequence = 450,
      .payload = UndoCombatantAction{9},
  }).status == DispatchStatus::unsupported);

  RuntimeLegacyCommandBridge provider_throws(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("undo provider failure");
      },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .undo_combatant = undo_sink,
      });
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 451,
      .payload = UndoCombatantAction{9},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("undo provider failure") !=
      std::string::npos);
  CHECK(undo_calls == 4);

  const RuntimeLegacyUndoCombatantSink throwing_undo_sink =
      [](CombatantId,
          uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("undo sink failure");
      };
  RuntimeLegacyCommandBridge undo_sink_throws(
      [&context] { return context; },
      movement_sink,
      party_selection_sink,
      inventory_sink,
      spellbook_sink,
      save_sink,
      load_sink,
      RuntimeLegacyCombatActionSinks{
          .undo_combatant = throwing_undo_sink,
      });
  const auto sink_thrown = undo_sink_throws.dispatch(UIAction{
      .sequence = 452,
      .payload = UndoCombatantAction{9},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("undo sink failure") != std::string::npos);
}

void test_open_combat_spellbook_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int calls = 0;
  bool accept = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  const RuntimeLegacyOpenCombatSpellbookSink sink =
      [&calls, &accept, &received_combatant, &received_message, &context](
          CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept;
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
  const auto make_bridge = [&](RuntimeLegacyContextProvider provider,
                               RuntimeLegacyOpenCombatSpellbookSink value) {
    return RuntimeLegacyCommandBridge(
        std::move(provider), movement_sink, party_selection_sink,
        inventory_sink, spellbook_sink, save_sink, load_sink,
        RuntimeLegacyCombatActionSinks{
            .open_combat_spellbook = std::move(value),
        });
  };
  auto bridge = make_bridge([&context] { return context; }, sink);

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_open_combat_spellbook(0, context) ==
        0x00000173U);
    CHECK(legacy_key_message_for_open_combat_spellbook(255, context) ==
        0x00000173U);
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 453,
      .payload = OpenCombatSpellbookAction{9},
  }).status == DispatchStatus::handled);
  CHECK(calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_message == 0x00000173U);

  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 454,
        .payload = OpenCombatSpellbookAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(calls == 3);

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
    CHECK(!legacy_key_message_for_open_combat_spellbook(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 455,
        .payload = OpenCombatSpellbookAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_open_combat_spellbook(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 456,
      .payload = OpenCombatSpellbookAction{9},
  }).status == DispatchStatus::rejected);
  CHECK(calls == 3);

  context.adaptive_eligible = true;
  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(),
           CombatantId{-1},
           CombatantId{256},
           std::numeric_limits<CombatantId>::max(),
       }) {
    CHECK(!legacy_key_message_for_open_combat_spellbook(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 457,
        .payload = OpenCombatSpellbookAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(calls == 3);

  accept = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 458,
      .payload = OpenCombatSpellbookAction{9},
  }).status == DispatchStatus::failed);
  CHECK(calls == 4);
  accept = true;

  auto missing_provider = make_bridge(RuntimeLegacyContextProvider{}, sink);
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 459,
      .payload = OpenCombatSpellbookAction{9},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);
  CHECK(calls == 4);

  auto empty_sink = make_bridge(
      [&context] { return context; }, RuntimeLegacyOpenCombatSpellbookSink{});
  const auto no_sink = empty_sink.dispatch(UIAction{
      .sequence = 460,
      .payload = OpenCombatSpellbookAction{9},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("open-combat-spellbook sink") !=
      std::string::npos);

  RuntimeLegacyCommandBridge absent_sink(
      [&context] { return context; }, movement_sink, party_selection_sink,
      inventory_sink, spellbook_sink, save_sink, load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(absent_sink.dispatch(UIAction{
      .sequence = 461,
      .payload = OpenCombatSpellbookAction{9},
  }).status == DispatchStatus::unsupported);

  auto provider_throws = make_bridge(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("combat spellbook provider failure");
      },
      sink);
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 462,
      .payload = OpenCombatSpellbookAction{9},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("combat spellbook provider failure") !=
      std::string::npos);

  const RuntimeLegacyOpenCombatSpellbookSink throwing_sink =
      [](CombatantId, uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("combat spellbook sink failure");
      };
  auto sink_throws = make_bridge([&context] { return context; }, throwing_sink);
  const auto sink_thrown = sink_throws.dispatch(UIAction{
      .sequence = 463,
      .payload = OpenCombatSpellbookAction{9},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("combat spellbook sink failure") !=
      std::string::npos);
}

void test_open_combat_targeting_mapping_and_dispatch() {
  RuntimeLegacyCommandContext context{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .adaptive_eligible = true,
  };
  int calls = 0;
  bool accept = true;
  CombatantId received_combatant = -1;
  uint32_t received_message = 0;
  const RuntimeLegacyOpenCombatTargetingSink sink =
      [&calls, &accept, &received_combatant, &received_message, &context](
          CombatantId combatant,
          uint32_t message,
          const RuntimeLegacyCommandContext& captured_context) {
        ++calls;
        received_combatant = combatant;
        received_message = message;
        CHECK(captured_context == context);
        return accept;
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
  const auto make_bridge = [&](RuntimeLegacyContextProvider provider,
                               RuntimeLegacyOpenCombatTargetingSink value) {
    return RuntimeLegacyCommandBridge(
        std::move(provider), movement_sink, party_selection_sink,
        inventory_sink, spellbook_sink, save_sink, load_sink,
        RuntimeLegacyCombatActionSinks{
            .open_combat_targeting = std::move(value),
        });
  };
  auto bridge = make_bridge([&context] { return context; }, sink);

  for (const auto world : {
           WorldPresentation::none,
           WorldPresentation::outdoor,
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    context.world_presentation = world;
    CHECK(legacy_key_message_for_open_combat_targeting(0, context) ==
        0x00001174U);
    CHECK(legacy_key_message_for_open_combat_targeting(255, context) ==
        0x00001174U);
  }
  context.world_presentation = WorldPresentation::none;

  CHECK(bridge.dispatch(UIAction{
      .sequence = 464,
      .payload = OpenCombatTargetingAction{9},
  }).status == DispatchStatus::handled);
  CHECK(calls == 1);
  CHECK(received_combatant == 9);
  CHECK(received_message == 0x00001174U);

  for (const CombatantId boundary : {0, 255}) {
    CHECK(bridge.dispatch(UIAction{
        .sequence = 465,
        .payload = OpenCombatTargetingAction{boundary},
    }).status == DispatchStatus::handled);
  }
  CHECK(calls == 3);

  constexpr std::array non_combat_screens{
      ScreenContext::title, ScreenContext::party_selection,
      ScreenContext::party_creation, ScreenContext::exploration,
      ScreenContext::dungeon, ScreenContext::inventory, ScreenContext::shop,
      ScreenContext::encounter, ScreenContext::ending,
  };
  for (const auto screen : non_combat_screens) {
    context.screen = screen;
    CHECK(!legacy_key_message_for_open_combat_targeting(9, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 466,
        .payload = OpenCombatTargetingAction{9},
    }).status == DispatchStatus::rejected);
  }
  CHECK(calls == 3);

  context.screen = ScreenContext::combat;
  context.adaptive_eligible = false;
  CHECK(!legacy_key_message_for_open_combat_targeting(9, context));
  CHECK(bridge.dispatch(UIAction{
      .sequence = 467,
      .payload = OpenCombatTargetingAction{9},
  }).status == DispatchStatus::rejected);
  context.adaptive_eligible = true;

  for (const CombatantId invalid : {
           std::numeric_limits<CombatantId>::min(), CombatantId{-1},
           CombatantId{256}, std::numeric_limits<CombatantId>::max(),
       }) {
    CHECK(!legacy_key_message_for_open_combat_targeting(invalid, context));
    CHECK(bridge.dispatch(UIAction{
        .sequence = 468,
        .payload = OpenCombatTargetingAction{invalid},
    }).status == DispatchStatus::rejected);
  }
  CHECK(calls == 3);

  accept = false;
  CHECK(bridge.dispatch(UIAction{
      .sequence = 469,
      .payload = OpenCombatTargetingAction{9},
  }).status == DispatchStatus::failed);
  CHECK(calls == 4);
  accept = true;

  auto missing_provider = make_bridge(RuntimeLegacyContextProvider{}, sink);
  const auto no_provider = missing_provider.dispatch(UIAction{
      .sequence = 470,
      .payload = OpenCombatTargetingAction{9},
  });
  CHECK(no_provider.status == DispatchStatus::failed);
  CHECK(no_provider.detail.find("context provider") != std::string::npos);

  auto empty_sink = make_bridge(
      [&context] { return context; }, RuntimeLegacyOpenCombatTargetingSink{});
  const auto no_sink = empty_sink.dispatch(UIAction{
      .sequence = 471,
      .payload = OpenCombatTargetingAction{9},
  });
  CHECK(no_sink.status == DispatchStatus::failed);
  CHECK(no_sink.detail.find("open-combat-targeting sink") !=
      std::string::npos);

  RuntimeLegacyCommandBridge absent_sink(
      [&context] { return context; }, movement_sink, party_selection_sink,
      inventory_sink, spellbook_sink, save_sink, load_sink,
      RuntimeLegacyCombatActionSinks{});
  CHECK(absent_sink.dispatch(UIAction{
      .sequence = 472,
      .payload = OpenCombatTargetingAction{9},
  }).status == DispatchStatus::unsupported);

  auto provider_throws = make_bridge(
      []() -> RuntimeLegacyCommandContext {
        throw std::runtime_error("combat targeting provider failure");
      },
      sink);
  const auto provider_thrown = provider_throws.dispatch(UIAction{
      .sequence = 473,
      .payload = OpenCombatTargetingAction{9},
  });
  CHECK(provider_thrown.status == DispatchStatus::failed);
  CHECK(provider_thrown.detail.find("combat targeting provider failure") !=
      std::string::npos);

  const RuntimeLegacyOpenCombatTargetingSink throwing_sink =
      [](CombatantId, uint32_t,
          const RuntimeLegacyCommandContext&) -> bool {
        throw std::runtime_error("combat targeting sink failure");
      };
  auto sink_throws = make_bridge([&context] { return context; }, throwing_sink);
  const auto sink_thrown = sink_throws.dispatch(UIAction{
      .sequence = 474,
      .payload = OpenCombatTargetingAction{9},
  });
  CHECK(sink_thrown.status == DispatchStatus::failed);
  CHECK(sink_thrown.detail.find("combat targeting sink failure") !=
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
      UIAction{
          .sequence = 387,
          .payload = AutoCombatantAction{8},
      },
      UIAction{
          .sequence = 388,
          .payload = ShowCombatRangeAction{9},
      },
      UIAction{
          .sequence = 389,
          .payload = BandageCombatantAction{10},
      },
      UIAction{
          .sequence = 390,
          .payload = UndoCombatantAction{11},
      },
      UIAction{
          .sequence = 391,
          .payload = OpenCombatSpellbookAction{12},
      },
      UIAction{
          .sequence = 392,
          .payload = OpenCombatTargetingAction{13},
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
          .auto_combatant = RuntimeLegacyAutoCombatantSink{},
          .show_combat_range = RuntimeLegacyShowCombatRangeSink{},
          .bandage_combatant = RuntimeLegacyBandageCombatantSink{},
          .undo_combatant = RuntimeLegacyUndoCombatantSink{},
          .open_combat_spellbook = RuntimeLegacyOpenCombatSpellbookSink{},
          .open_combat_targeting = RuntimeLegacyOpenCombatTargetingSink{},
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
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 391,
      .payload = AutoCombatantAction{6},
  }).status == DispatchStatus::unsupported);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 392,
      .payload = ShowCombatRangeAction{6},
  }).status == DispatchStatus::unsupported);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 393,
      .payload = BandageCombatantAction{6},
  }).status == DispatchStatus::unsupported);
  CHECK(sparse_combat_sinks.dispatch(UIAction{
      .sequence = 394,
      .payload = UndoCombatantAction{6},
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
  CHECK(through_center.dispatch(UIAction{
      .sequence = 400,
      .payload = AutoCombatantAction{4},
  }).status == DispatchStatus::unsupported);
  CHECK(through_center.dispatch(UIAction{
      .sequence = 401,
      .payload = ShowCombatRangeAction{4},
  }).status == DispatchStatus::unsupported);
  CHECK(through_center.dispatch(UIAction{
      .sequence = 402,
      .payload = BandageCombatantAction{4},
  }).status == DispatchStatus::unsupported);
  CHECK(through_center.dispatch(UIAction{
      .sequence = 403,
      .payload = UndoCombatantAction{4},
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
    test_auto_combatant_mapping_and_dispatch();
    test_show_combat_range_mapping_and_dispatch();
    test_bandage_combatant_mapping_and_dispatch();
    test_undo_combatant_mapping_and_dispatch();
    test_open_combat_spellbook_mapping_and_dispatch();
    test_open_combat_targeting_mapping_and_dispatch();
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
