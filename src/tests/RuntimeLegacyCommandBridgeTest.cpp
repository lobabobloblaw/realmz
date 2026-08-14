#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
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
