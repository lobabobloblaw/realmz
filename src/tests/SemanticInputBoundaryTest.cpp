#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "presentation/LegacyGameSnapshotSource.hpp"
#include "presentation/LegacyPresentationContext.h"
#include "presentation/SemanticInputBoundary.h"
#include "presentation/UIAction.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;
RealmzLegacyPresentationContext captured_legacy_context{};
GameSnapshot captured_snapshot{};
bool snapshot_capture_throws = false;
int legacy_capture_calls = 0;
int snapshot_capture_calls = 0;
constexpr uint32_t kUnchangedClassicMessage = 0xA5A5A5A5U;

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
  uint32_t classic_message;
};

constexpr std::array kOutdoorMovements{
    ExpectedMovement{MovementCommand::north, 0x00007E1EU},
    ExpectedMovement{MovementCommand::northeast, 0x00005C39U},
    ExpectedMovement{MovementCommand::east, 0x00007C1DU},
    ExpectedMovement{MovementCommand::southeast, 0x00005533U},
    ExpectedMovement{MovementCommand::south, 0x00007D1FU},
    ExpectedMovement{MovementCommand::southwest, 0x00005331U},
    ExpectedMovement{MovementCommand::west, 0x00007B1CU},
    ExpectedMovement{MovementCommand::northwest, 0x00005937U},
};

constexpr std::array kDungeonMovements{
    ExpectedMovement{MovementCommand::step_forward, 0x00007E1EU},
    ExpectedMovement{MovementCommand::step_backward, 0x00007D1FU},
    ExpectedMovement{MovementCommand::turn_left, 0x00007B1CU},
    ExpectedMovement{MovementCommand::turn_right, 0x00007C1DU},
};

void reset_capture(
    RealmzLegacyScreenContext legacy_screen,
    ScreenContext snapshot_screen,
    WorldPresentation world_presentation,
    bool adaptive_eligible = true) {
  RealmzEndSemanticInputSurface();
  captured_legacy_context = {
      .screen = legacy_screen,
      .gameplay_window_active = 1,
      .adaptive_eligible = static_cast<uint8_t>(adaptive_eligible),
      .requires_full_frame = static_cast<uint8_t>(!adaptive_eligible),
  };
  captured_snapshot = {};
  captured_snapshot.screen = snapshot_screen;
  captured_snapshot.party.members = {
      PartyMemberView{
          .id = 0,
          .name = "Arin",
          .spell_points = {8, 12},
          .selected = true,
      },
      PartyMemberView{.id = 1, .name = "Bryn", .spell_points = {5, 10}},
      PartyMemberView{.id = 2, .name = "Cerys", .spell_points = {3, 6}},
  };
  captured_snapshot.party.selected_member = 0;
  captured_snapshot.party.members[0].use_scroll_available = true;
  captured_snapshot.world.presentation = world_presentation;
  snapshot_capture_throws = false;
  legacy_capture_calls = 0;
  snapshot_capture_calls = 0;
}

bool consume(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticMovementEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_selection(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint8_t& output) {
  return RealmzConsumeSemanticPartySelectionEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_character_sheet(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint8_t& output) {
  return RealmzConsumeSemanticOpenCharacterSheetEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_inventory(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticOpenInventoryEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_spellbook(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticOpenSpellbookEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_scroll_case(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticOpenScrollCaseEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_save_game(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    int16_t& menu_id,
    int16_t& item_id) {
  return RealmzConsumeSemanticOpenSaveGameEvent(
             expected_surface, tag, &menu_id, &item_id) != 0;
}

bool consume_load_game(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    int16_t& menu_id,
    int16_t& item_id) {
  return RealmzConsumeSemanticOpenLoadGameEvent(
             expected_surface, tag, &menu_id, &item_id) != 0;
}

bool consume_rest_party(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticRestPartyEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_guard(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticGuardCombatantEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_finish(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticFinishCombatantEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_delay(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticDelayCombatantEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_center_active(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticCenterActiveCombatantEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_switch_weapon(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticSwitchWeaponEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_cycle_focus(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticCycleCombatFocusEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_combat_items(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticOpenCombatItemsEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_auto_combatant(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticAutoCombatantEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_show_combat_range(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticShowCombatRangeEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_bandage_combatant(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticBandageCombatantEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_undo_combatant(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticUndoCombatantEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_open_combat_spellbook(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticOpenCombatSpellbookEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_open_combat_targeting(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticOpenCombatTargetingEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_escape_combat(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticEscapeCombatEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_open_combat_scroll_case(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  return RealmzConsumeSemanticOpenCombatScrollCaseEvent(
             expected_surface, tag, &output) != 0;
}

bool consume_center_combat_cursor(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output,
    uint8_t& absolute_x,
    uint8_t& absolute_y) {
  return RealmzConsumeSemanticCenterCombatCursorEvent(
             expected_surface,
             tag,
             &output,
             &absolute_x,
             &absolute_y) != 0;
}

uint32_t semantic_cycle_previous_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  return semantic_cycle_combat_focus_tag(
      combatant, CombatFocusDirection::previous, surface);
}

uint32_t semantic_cycle_next_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  return semantic_cycle_combat_focus_tag(
      combatant, CombatFocusDirection::next, surface);
}

using CombatTagFactory = uint32_t (*)(
    CombatantId,
    RealmzSemanticInputSurface) noexcept;
using CombatConsumer = bool (*)(
    RealmzSemanticInputSurface,
    uint32_t,
    uint32_t&);

struct CombatActionCase {
  CombatTagFactory tag = nullptr;
  CombatConsumer consume = nullptr;
  uint32_t classic_message = 0;
};

constexpr std::array kCombatActionCases{
    CombatActionCase{
        .tag = semantic_guard_combatant_tag,
        .consume = consume_guard,
        .classic_message = 0x00000567U,
    },
    CombatActionCase{
        .tag = semantic_finish_combatant_tag,
        .consume = consume_finish,
        .classic_message = 0x00000366U,
    },
    CombatActionCase{
        .tag = semantic_delay_combatant_tag,
        .consume = consume_delay,
        .classic_message = 0x00000264U,
    },
    CombatActionCase{
        .tag = semantic_center_active_combatant_tag,
        .consume = consume_center_active,
        .classic_message = 0x00000863U,
    },
    CombatActionCase{
        .tag = semantic_switch_weapon_tag,
        .consume = consume_switch_weapon,
        .classic_message = 0x00000D77U,
    },
    CombatActionCase{
        .tag = semantic_cycle_previous_tag,
        .consume = consume_cycle_focus,
        .classic_message = 0x00002370U,
    },
    CombatActionCase{
        .tag = semantic_cycle_next_tag,
        .consume = consume_cycle_focus,
        .classic_message = 0x00002D6EU,
    },
    CombatActionCase{
        .tag = semantic_auto_combatant_tag,
        .consume = consume_auto_combatant,
        .classic_message = 0x00000061U,
    },
    CombatActionCase{
        .tag = semantic_show_combat_range_tag,
        .consume = consume_show_combat_range,
        .classic_message = 0x00000F72U,
    },
    CombatActionCase{
        .tag = semantic_bandage_combatant_tag,
        .consume = consume_bandage_combatant,
        .classic_message = 0x00000B62U,
    },
    CombatActionCase{
        .tag = semantic_undo_combatant_tag,
        .consume = consume_undo_combatant,
        .classic_message = 0x00002075U,
    },
    CombatActionCase{
        .tag = semantic_open_combat_spellbook_tag,
        .consume = consume_open_combat_spellbook,
        .classic_message = 0x00000173U,
    },
    CombatActionCase{
        .tag = semantic_open_combat_targeting_tag,
        .consume = consume_open_combat_targeting,
        .classic_message = 0x00001174U,
    },
    CombatActionCase{
        .tag = semantic_escape_combat_tag,
        .consume = consume_escape_combat,
        .classic_message = 0x00000E65U,
    },
    CombatActionCase{
        .tag = semantic_open_combat_scroll_case_tag,
        .consume = consume_open_combat_scroll_case,
        .classic_message = 0x0000256CU,
    },
};

enum class SharedCombatRejection {
  acting_combatant_changed,
  combat_missing,
  combat_inactive,
  combatant_missing,
  non_party_combatant,
  combatant_inactive,
  combatant_untargetable,
  combatant_without_stamina,
  party_member_missing,
};

constexpr std::array kSharedCombatRejections{
    SharedCombatRejection::acting_combatant_changed,
    SharedCombatRejection::combat_missing,
    SharedCombatRejection::combat_inactive,
    SharedCombatRejection::combatant_missing,
    SharedCombatRejection::non_party_combatant,
    SharedCombatRejection::combatant_inactive,
    SharedCombatRejection::combatant_untargetable,
    SharedCombatRejection::combatant_without_stamina,
    SharedCombatRejection::party_member_missing,
};

void complete_top_level_scope(RealmzSemanticInputSurface surface) {
  RealmzBeginSemanticInputSurface(surface);
  CHECK(RealmzCurrentSemanticInputSurface() == surface);
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);
}

void configure_valid_shared_combat() {
  captured_snapshot.party.members[1].movement = 9;
  captured_snapshot.party.members[1].movement_maximum = 9;
  captured_snapshot.combat = CombatView{
      .active = true,
      .bandage_available = true,
      .undo_available = true,
      .cast_spell_available = true,
      .target_available = true,
      .use_scroll_available = true,
      .round = 3,
      .field_origin_x = 20,
      .field_origin_y = 30,
      .visible_columns = 15,
      .visible_rows = 13,
      .acting_combatant = 1,
      .combatants = {
          CombatantView{
              .id = 1,
              .kind = CombatantKind::party_member,
              .name = "Bryn",
              .stamina = {14, 20},
              .active = true,
              .targetable = true,
          },
          CombatantView{
              .id = 10,
              .kind = CombatantKind::monster,
              .name = "Goblin",
              .stamina = {8, 8},
              .targetable = true,
          },
      },
  };
}

void reset_valid_shared_combat() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_shared_combat();
}

void apply_shared_combat_rejection(SharedCombatRejection rejection) {
  switch (rejection) {
    case SharedCombatRejection::acting_combatant_changed:
      captured_snapshot.combat->acting_combatant = 2;
      return;
    case SharedCombatRejection::combat_missing:
      captured_snapshot.combat.reset();
      return;
    case SharedCombatRejection::combat_inactive:
      captured_snapshot.combat->active = false;
      return;
    case SharedCombatRejection::combatant_missing:
      captured_snapshot.combat->combatants.erase(
          captured_snapshot.combat->combatants.begin());
      return;
    case SharedCombatRejection::non_party_combatant:
      captured_snapshot.combat->combatants[0].kind = CombatantKind::monster;
      return;
    case SharedCombatRejection::combatant_inactive:
      captured_snapshot.combat->combatants[0].active = false;
      return;
    case SharedCombatRejection::combatant_untargetable:
      captured_snapshot.combat->combatants[0].targetable = false;
      return;
    case SharedCombatRejection::combatant_without_stamina:
      captured_snapshot.combat->combatants[0].stamina.current = 0;
      return;
    case SharedCombatRejection::party_member_missing:
      captured_snapshot.party.members.erase(
          captured_snapshot.party.members.begin() + 1);
      return;
  }
}

bool consume_after_top_level_scope(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag,
    uint32_t& output) {
  complete_top_level_scope(expected_surface);
  return consume(expected_surface, tag, output);
}

void test_tag_encoding_and_validation() {
  std::set<uint32_t> tags;
  constexpr std::array surfaces{
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      REALMZ_SEMANTIC_INPUT_DUNGEON,
  };
  constexpr std::array commands{
      MovementCommand::step_forward,
      MovementCommand::step_backward,
      MovementCommand::turn_left,
      MovementCommand::turn_right,
      MovementCommand::north,
      MovementCommand::northeast,
      MovementCommand::east,
      MovementCommand::southeast,
      MovementCommand::south,
      MovementCommand::southwest,
      MovementCommand::west,
      MovementCommand::northwest,
  };

  for (const auto surface : surfaces) {
    for (const auto command : commands) {
      const uint32_t tag = semantic_movement_tag(command, surface);
      CHECK((tag & 0xFFFF0000U) == 0x524D0000U);
      CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(command));
      CHECK(RealmzIsSemanticMovementTag(tag) != 0);
      CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
      CHECK(tags.emplace(tag).second);
    }
  }
  CHECK(tags.size() == surfaces.size() * commands.size());

  CHECK(semantic_movement_tag(
            MovementCommand::north, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_movement_tag(
            MovementCommand::north,
            REALMZ_SEMANTIC_INPUT_COMBAT) == 0);

  constexpr std::array malformed_tags{
      0U,
      0x524C0104U,
      0x524E0104U,
      0x524D0004U,
      0x524D0304U,
      0x524DFF04U,
      0x524D010CU,
      0x524D01FFU,
      0xFFFFFFFFU,
  };
  for (const auto tag : malformed_tags) {
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
  }
  const uint32_t invalid_command_tag = semantic_movement_tag(
      static_cast<MovementCommand>(12),
      REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzIsSemanticMovementTag(invalid_command_tag) == 0);

  std::set<uint32_t> selection_tags;
  for (const auto surface : surfaces) {
    for (const PartyMemberId member :
         std::array<PartyMemberId, 4>{0, 1, 5, 0xFF}) {
      const uint32_t tag = semantic_party_selection_tag(member, surface);
      CHECK((tag & 0xFFFF0000U) == 0x52530000U);
      CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(member));
      CHECK(RealmzIsSemanticPartySelectionTag(tag) != 0);
      CHECK(RealmzIsSemanticMovementTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticPartySelectionTagSurface(tag) == surface);
      CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
      CHECK(selection_tags.emplace(tag).second);
      CHECK(!tags.contains(tag));
    }
  }
  CHECK(selection_tags.size() == 8);
  CHECK(semantic_party_selection_tag(
            0, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_party_selection_tag(
            0, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52540000U,
           0x52530000U,
           0x52530300U,
           0x5253FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticPartySelectionTag(malformed) == 0);
    CHECK(RealmzSemanticPartySelectionTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    if (RealmzIsSemanticMovementTag(malformed) == 0) {
      CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
      CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
          REALMZ_SEMANTIC_INPUT_NONE);
    }
  }

  std::set<uint32_t> inventory_tags;
  for (const auto surface : surfaces) {
    for (const PartyMemberId member :
         std::array<PartyMemberId, 4>{0, 1, 5, 0xFF}) {
      const uint32_t tag = semantic_open_inventory_tag(member, surface);
      CHECK((tag & 0xFFFF0000U) == 0x52490000U);
      CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(member));
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) != 0);
      CHECK(RealmzIsSemanticMovementTag(tag) == 0);
      CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticOpenInventoryTagSurface(tag) == surface);
      CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
      CHECK(inventory_tags.emplace(tag).second);
      CHECK(!tags.contains(tag));
      CHECK(!selection_tags.contains(tag));
    }
  }
  CHECK(inventory_tags.size() == 8);
  CHECK(semantic_open_inventory_tag(
            0, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_inventory_tag(
            0, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52480000U,
           0x52490000U,
           0x52490300U,
           0x5249FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenInventoryTag(malformed) == 0);
    CHECK(RealmzSemanticOpenInventoryTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> spellbook_tags;
  for (const auto surface : surfaces) {
    for (const PartyMemberId member :
         std::array<PartyMemberId, 4>{0, 1, 5, 0xFF}) {
      const uint32_t tag = semantic_open_spellbook_tag(member, surface);
      CHECK((tag & 0xFFFF0000U) == 0x52500000U);
      CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(member));
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) != 0);
      CHECK(RealmzIsSemanticMovementTag(tag) == 0);
      CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticOpenSpellbookTagSurface(tag) == surface);
      CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
      CHECK(spellbook_tags.emplace(tag).second);
      CHECK(!tags.contains(tag));
      CHECK(!selection_tags.contains(tag));
      CHECK(!inventory_tags.contains(tag));
    }
  }
  CHECK(spellbook_tags.size() == 8);
  CHECK(semantic_open_spellbook_tag(
            0, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_spellbook_tag(
            0, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  for (const uint32_t malformed : {
           0U,
           0x524F0000U,
           0x52500000U,
           0x52500300U,
           0x5250FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenSpellbookTag(malformed) == 0);
    CHECK(RealmzSemanticOpenSpellbookTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> save_game_tags;
  for (const auto surface : surfaces) {
    const uint32_t tag = semantic_open_save_game_tag(surface);
    CHECK((tag & 0xFFFF0000U) == 0x52560000U);
    CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
    CHECK((tag & 0xFFU) == 0U);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) != 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticOpenSaveGameTagSurface(tag) == surface);
    CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
    CHECK(save_game_tags.emplace(tag).second);
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
  }
  CHECK(save_game_tags.size() == surfaces.size());
  CHECK(semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_save_game_tag(
            REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52550000U,
           0x52560000U,
           0x52560300U,
           0x5256FF00U,
           0x52560101U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenSaveGameTag(malformed) == 0);
    CHECK(RealmzSemanticOpenSaveGameTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> load_game_tags;
  for (const auto surface : surfaces) {
    const uint32_t tag = semantic_open_load_game_tag(surface);
    CHECK((tag & 0xFFFF0000U) == 0x524C0000U);
    CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
    CHECK((tag & 0xFFU) == 0U);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) != 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticOpenLoadGameTagSurface(tag) == surface);
    CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
    CHECK(load_game_tags.emplace(tag).second);
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
  }
  CHECK(load_game_tags.size() == surfaces.size());
  CHECK(semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_load_game_tag(
            REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  for (const uint32_t malformed : {
           0U,
           0x524B0000U,
           0x524C0000U,
           0x524C0300U,
           0x524CFF00U,
           0x524C0104U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenLoadGameTag(malformed) == 0);
    CHECK(RealmzSemanticOpenLoadGameTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> guard_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_guard_combatant_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52470000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) != 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGuardCombatantTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(guard_tags.emplace(tag).second);
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
    CHECK(!load_game_tags.contains(tag));
  }
  CHECK(semantic_guard_combatant_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_guard_combatant_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_guard_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_guard_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_guard_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52460000U,
           0x52470000U,
           0x52470101U,
           0x52470201U,
           0x52470401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticGuardCombatantTag(malformed) == 0);
    CHECK(RealmzSemanticGuardCombatantTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> finish_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_finish_combatant_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52460000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) != 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticFinishCombatantTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(finish_tags.emplace(tag).second);
    CHECK(!guard_tags.contains(tag));
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
    CHECK(!load_game_tags.contains(tag));
  }
  CHECK(semantic_finish_combatant_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_finish_combatant_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_finish_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_finish_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_finish_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52450000U,
           0x52460000U,
           0x52460101U,
           0x52460201U,
           0x52460401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticFinishCombatantTag(malformed) == 0);
    CHECK(RealmzSemanticFinishCombatantTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> delay_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_delay_combatant_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52440000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) != 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticDelayCombatantTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(delay_tags.emplace(tag).second);
    CHECK(!guard_tags.contains(tag));
    CHECK(!finish_tags.contains(tag));
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
    CHECK(!load_game_tags.contains(tag));
  }
  CHECK(semantic_delay_combatant_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_delay_combatant_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_delay_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_delay_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_delay_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52430000U,
           0x52440000U,
           0x52440101U,
           0x52440201U,
           0x52440401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticDelayCombatantTag(malformed) == 0);
    CHECK(RealmzSemanticDelayCombatantTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> center_active_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_center_active_combatant_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52430000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) != 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticCenterActiveCombatantTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(center_active_tags.emplace(tag).second);
    CHECK(!guard_tags.contains(tag));
    CHECK(!finish_tags.contains(tag));
    CHECK(!delay_tags.contains(tag));
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
    CHECK(!load_game_tags.contains(tag));
  }
  CHECK(semantic_center_active_combatant_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_center_active_combatant_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_center_active_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_center_active_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_center_active_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52420000U,
           0x52430000U,
           0x52430101U,
           0x52430201U,
           0x52430401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(malformed) == 0);
    CHECK(RealmzSemanticCenterActiveCombatantTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> switch_weapon_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_switch_weapon_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52570000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) != 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticSwitchWeaponTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(switch_weapon_tags.emplace(tag).second);
    CHECK(!guard_tags.contains(tag));
    CHECK(!finish_tags.contains(tag));
    CHECK(!delay_tags.contains(tag));
    CHECK(!center_active_tags.contains(tag));
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
    CHECK(!load_game_tags.contains(tag));
  }
  CHECK(semantic_switch_weapon_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_switch_weapon_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_switch_weapon_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_switch_weapon_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_switch_weapon_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52560000U,
           0x52570000U,
           0x52570101U,
           0x52570201U,
           0x52570401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticSwitchWeaponTag(malformed) == 0);
    CHECK(RealmzSemanticSwitchWeaponTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> cycle_focus_tags;
  for (const auto direction : {
           CombatFocusDirection::previous,
           CombatFocusDirection::next,
       }) {
    const uint32_t expected_signature =
        (direction == CombatFocusDirection::previous)
        ? 0x52420000U
        : 0x524E0000U;
    for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
      const uint32_t tag = semantic_cycle_combat_focus_tag(
          combatant, direction, REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK((tag & 0xFFFF0000U) == expected_signature);
      CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
      CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) != 0);
      CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
      CHECK(RealmzIsSemanticMovementTag(tag) == 0);
      CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticCycleCombatFocusTagSurface(tag) ==
          REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK(RealmzSemanticGameplayTagSurface(tag) ==
          REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK(cycle_focus_tags.emplace(tag).second);
      CHECK(!guard_tags.contains(tag));
      CHECK(!finish_tags.contains(tag));
      CHECK(!delay_tags.contains(tag));
      CHECK(!center_active_tags.contains(tag));
      CHECK(!switch_weapon_tags.contains(tag));
      CHECK(!tags.contains(tag));
      CHECK(!selection_tags.contains(tag));
      CHECK(!inventory_tags.contains(tag));
      CHECK(!spellbook_tags.contains(tag));
      CHECK(!save_game_tags.contains(tag));
      CHECK(!load_game_tags.contains(tag));
    }
  }
  CHECK(cycle_focus_tags.size() == 10);
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t previous = semantic_cycle_combat_focus_tag(
        combatant,
        CombatFocusDirection::previous,
        REALMZ_SEMANTIC_INPUT_COMBAT);
    const uint32_t next = semantic_cycle_combat_focus_tag(
        combatant,
        CombatFocusDirection::next,
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(previous != next);
    CHECK((previous & 0xFFFF0000U) == 0x52420000U);
    CHECK((next & 0xFFFF0000U) == 0x524E0000U);
  }
  for (const auto direction : {
           CombatFocusDirection::previous,
           CombatFocusDirection::next,
       }) {
    CHECK(semantic_cycle_combat_focus_tag(
              -1, direction, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
    CHECK(semantic_cycle_combat_focus_tag(
              256, direction, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
    CHECK(semantic_cycle_combat_focus_tag(
              1, direction, REALMZ_SEMANTIC_INPUT_NONE) == 0);
    CHECK(semantic_cycle_combat_focus_tag(
              1, direction, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
    CHECK(semantic_cycle_combat_focus_tag(
              1, direction, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  }
  CHECK(semantic_cycle_combat_focus_tag(
            1,
            static_cast<CombatFocusDirection>(-1),
            REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52410000U,
           0x52420000U,
           0x52420101U,
           0x52420201U,
           0x52420401U,
           0x524E0000U,
           0x524E0101U,
           0x524E0201U,
           0x524E0401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticCycleCombatFocusTag(malformed) == 0);
    CHECK(RealmzSemanticCycleCombatFocusTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  std::set<uint32_t> combat_items_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    for (const PartyMemberId member :
         std::array<PartyMemberId, 4>{0, 1, 5, 0xFF}) {
      const uint32_t tag = semantic_open_combat_items_tag(
          combatant, member, REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK((tag & 0xFF000000U) == 0x49000000U);
      CHECK(((tag >> 16U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK(((tag >> 8U) & 0xFFU) ==
          static_cast<uint32_t>(combatant));
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(member));
      CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) != 0);
      CHECK(RealmzIsSemanticMovementTag(tag) == 0);
      CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
      CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticOpenCombatItemsTagSurface(tag) ==
          REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK(RealmzSemanticGameplayTagSurface(tag) ==
          REALMZ_SEMANTIC_INPUT_COMBAT);
      CHECK(combat_items_tags.emplace(tag).second);
      CHECK(!tags.contains(tag));
      CHECK(!selection_tags.contains(tag));
      CHECK(!inventory_tags.contains(tag));
      CHECK(!spellbook_tags.contains(tag));
      CHECK(!save_game_tags.contains(tag));
      CHECK(!load_game_tags.contains(tag));
      CHECK(!guard_tags.contains(tag));
      CHECK(!finish_tags.contains(tag));
      CHECK(!delay_tags.contains(tag));
      CHECK(!center_active_tags.contains(tag));
      CHECK(!switch_weapon_tags.contains(tag));
      CHECK(!cycle_focus_tags.contains(tag));
    }
  }
  CHECK(combat_items_tags.size() == 20);
  CHECK(semantic_open_combat_items_tag(
            -1, 0, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_items_tag(
            256, 0, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_items_tag(
            1, 0, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_combat_items_tag(
            1, 0, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_open_combat_items_tag(
            1, 0, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x48030101U,
           0x49000101U,
           0x49010101U,
           0x49020101U,
           0x49040101U,
           0x49FF0101U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenCombatItemsTag(malformed) == 0);
    CHECK(RealmzSemanticOpenCombatItemsTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
  for (const uint32_t other_tag : {
           semantic_movement_tag(
               MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_party_selection_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_inventory_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_spellbook_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_center_active_combatant_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_cycle_combat_focus_tag(
               1,
               CombatFocusDirection::next,
               REALMZ_SEMANTIC_INPUT_COMBAT),
       }) {
    CHECK(RealmzIsSemanticOpenCombatItemsTag(other_tag) == 0);
  }

  std::set<uint32_t> auto_combatant_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_auto_combatant_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52410000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) != 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticAutoCombatantTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(auto_combatant_tags.emplace(tag).second);
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
    CHECK(!load_game_tags.contains(tag));
    CHECK(!guard_tags.contains(tag));
    CHECK(!finish_tags.contains(tag));
    CHECK(!delay_tags.contains(tag));
    CHECK(!center_active_tags.contains(tag));
    CHECK(!switch_weapon_tags.contains(tag));
    CHECK(!cycle_focus_tags.contains(tag));
    CHECK(!combat_items_tags.contains(tag));
  }
  CHECK(auto_combatant_tags.size() == 5);
  CHECK(semantic_auto_combatant_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_auto_combatant_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_auto_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_auto_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_auto_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52400000U,
           0x52410000U,
           0x52410101U,
           0x52410201U,
           0x52410401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticAutoCombatantTag(malformed) == 0);
    CHECK(RealmzSemanticAutoCombatantTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
  for (const uint32_t other_tag : {
           semantic_movement_tag(
               MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_party_selection_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_inventory_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_spellbook_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_center_active_combatant_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_cycle_combat_focus_tag(
               1,
               CombatFocusDirection::next,
               REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_open_combat_items_tag(
               1, 0, REALMZ_SEMANTIC_INPUT_COMBAT),
       }) {
    CHECK(RealmzIsSemanticAutoCombatantTag(other_tag) == 0);
  }

  std::set<uint32_t> show_combat_range_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_show_combat_range_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52520000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) != 0);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticShowCombatRangeTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(show_combat_range_tags.emplace(tag).second);
    CHECK(!tags.contains(tag));
    CHECK(!selection_tags.contains(tag));
    CHECK(!inventory_tags.contains(tag));
    CHECK(!spellbook_tags.contains(tag));
    CHECK(!save_game_tags.contains(tag));
    CHECK(!load_game_tags.contains(tag));
    CHECK(!guard_tags.contains(tag));
    CHECK(!finish_tags.contains(tag));
    CHECK(!delay_tags.contains(tag));
    CHECK(!center_active_tags.contains(tag));
    CHECK(!switch_weapon_tags.contains(tag));
    CHECK(!cycle_focus_tags.contains(tag));
    CHECK(!combat_items_tags.contains(tag));
    CHECK(!auto_combatant_tags.contains(tag));
  }
  CHECK(show_combat_range_tags.size() == 5);
  CHECK(semantic_show_combat_range_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_show_combat_range_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_show_combat_range_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_show_combat_range_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_show_combat_range_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);
  for (const uint32_t malformed : {
           0U,
           0x52510000U,
           0x52520000U,
           0x52520101U,
           0x52520201U,
           0x52520401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticShowCombatRangeTag(malformed) == 0);
    CHECK(RealmzSemanticShowCombatRangeTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
  for (const uint32_t other_tag : {
           semantic_movement_tag(
               MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_party_selection_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_inventory_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_spellbook_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_center_active_combatant_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_cycle_combat_focus_tag(
               1,
               CombatFocusDirection::next,
               REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_open_combat_items_tag(
               1, 0, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
       }) {
    CHECK(RealmzIsSemanticShowCombatRangeTag(other_tag) == 0);
  }
}

void test_open_character_sheet_tag_encoding_and_collisions() {
  constexpr std::array surfaces{
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      REALMZ_SEMANTIC_INPUT_DUNGEON,
  };
  std::set<uint32_t> character_sheet_tags;
  for (const auto surface : surfaces) {
    for (const PartyMemberId member :
         std::array<PartyMemberId, 4>{0, 1, 5, 0xFF}) {
      const uint32_t tag = semantic_open_character_sheet_tag(member, surface);
      CHECK((tag & 0xFFFF0000U) == 0x43530000U);
      CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(member));
      CHECK(tag == (0x43530000U |
          (static_cast<uint32_t>(surface) << 8U) |
          static_cast<uint32_t>(member)));
      CHECK(RealmzIsSemanticOpenCharacterSheetTag(tag) != 0);
      CHECK(RealmzSemanticOpenCharacterSheetTagSurface(tag) == surface);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
      CHECK(RealmzIsSemanticMovementTag(tag) == 0);
      CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenScrollCaseTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
      CHECK(character_sheet_tags.emplace(tag).second);
    }
  }
  CHECK(character_sheet_tags.size() == 8);

  CHECK(semantic_open_character_sheet_tag(
            0, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_character_sheet_tag(
            0, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_character_sheet_tag(
            0, static_cast<RealmzSemanticInputSurface>(0xFF)) == 0);

  for (const uint32_t malformed : {
           0U,
           0x43520000U,
           0x43530000U,
           0x43530300U,
           0x4353FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenCharacterSheetTag(malformed) == 0);
    CHECK(RealmzSemanticOpenCharacterSheetTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    if (RealmzIsSemanticGameplayTag(malformed) == 0) {
      CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
          REALMZ_SEMANTIC_INPUT_NONE);
    }
  }

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_party_selection_tag(0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_inventory_tag(0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_spellbook_tag(0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_scroll_case_tag(0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_guard_combatant_tag(0, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          0, 0, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(!character_sheet_tags.contains(other_tag));
    CHECK(RealmzIsSemanticOpenCharacterSheetTag(other_tag) == 0);
    CHECK(RealmzSemanticOpenCharacterSheetTagSurface(other_tag) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_rest_party_tag_encoding_and_collisions() {
  constexpr std::array surfaces{
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      REALMZ_SEMANTIC_INPUT_DUNGEON,
  };
  std::set<uint32_t> rest_tags;
  for (const auto surface : surfaces) {
    const uint32_t tag = semantic_rest_party_tag(surface);
    CHECK((tag & 0xFFFF0000U) == 0x57520000U);
    CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
    CHECK((tag & 0xFFU) == 0);
    CHECK(RealmzIsSemanticRestPartyTag(tag) != 0);
    CHECK(RealmzSemanticRestPartyTagSurface(tag) == surface);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCharacterSheetTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenScrollCaseTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
    CHECK(rest_tags.emplace(tag).second);
  }
  CHECK(rest_tags.size() == 2);
  CHECK(semantic_rest_party_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION) ==
      0x57520100U);
  CHECK(semantic_rest_party_tag(REALMZ_SEMANTIC_INPUT_DUNGEON) ==
      0x57520200U);
  CHECK(semantic_rest_party_tag(REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_rest_party_tag(REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_rest_party_tag(
      static_cast<RealmzSemanticInputSurface>(0xFF)) == 0);

  for (const uint32_t malformed : {
           0U,
           0x57510000U,
           0x57520000U,
           0x57520101U,
           0x575202FFU,
           0x57520300U,
           0x5752FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticRestPartyTag(malformed) == 0);
    CHECK(RealmzSemanticRestPartyTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  const uint32_t combat_range = semantic_show_combat_range_tag(
      0, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(combat_range == 0x52520300U);
  CHECK(!rest_tags.contains(combat_range));
  CHECK(RealmzIsSemanticRestPartyTag(combat_range) == 0);
  CHECK(RealmzIsSemanticShowCombatRangeTag(combat_range) != 0);
  for (const uint32_t other_tag : {
           semantic_movement_tag(
               MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_party_selection_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_character_sheet_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_inventory_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_spellbook_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_scroll_case_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           combat_range,
       }) {
    CHECK(other_tag != 0);
    CHECK(RealmzIsSemanticRestPartyTag(other_tag) == 0);
  }
}

void test_open_scroll_case_tag_encoding_and_collisions() {
  constexpr std::array surfaces{
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      REALMZ_SEMANTIC_INPUT_DUNGEON,
  };
  std::set<uint32_t> scroll_case_tags;
  for (const auto surface : surfaces) {
    for (const PartyMemberId member :
         std::array<PartyMemberId, 4>{0, 1, 5, 0xFF}) {
      const uint32_t tag = semantic_open_scroll_case_tag(member, surface);
      CHECK((tag & 0xFFFF0000U) == 0x53550000U);
      CHECK(((tag >> 8U) & 0xFFU) == static_cast<uint32_t>(surface));
      CHECK((tag & 0xFFU) == static_cast<uint32_t>(member));
      CHECK(RealmzIsSemanticOpenScrollCaseTag(tag) != 0);
      CHECK(RealmzSemanticOpenScrollCaseTagSurface(tag) == surface);
      CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
      CHECK(RealmzSemanticGameplayTagSurface(tag) == surface);
      CHECK(RealmzIsSemanticMovementTag(tag) == 0);
      CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
      CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
      CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
      CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
      CHECK(RealmzIsSemanticBandageCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticUndoCombatantTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenCombatSpellbookTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenCombatTargetingTag(tag) == 0);
      CHECK(RealmzIsSemanticEscapeCombatTag(tag) == 0);
      CHECK(RealmzIsSemanticOpenCombatScrollCaseTag(tag) == 0);
      CHECK(RealmzIsSemanticCenterCombatCursorTag(tag) == 0);
      CHECK(scroll_case_tags.emplace(tag).second);
    }
  }
  CHECK(scroll_case_tags.size() == 8);
  CHECK(semantic_open_scroll_case_tag(
            0, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0x53550100U);
  CHECK(semantic_open_scroll_case_tag(
            0xFF, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0x535502FFU);
  CHECK(semantic_open_scroll_case_tag(
            0, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_scroll_case_tag(
            0, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);

  for (const uint32_t malformed : {
           0U,
           0x53540000U,
           0x53550000U,
           0x53550300U,
           0x5355FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenScrollCaseTag(malformed) == 0);
    CHECK(RealmzSemanticOpenScrollCaseTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  for (const uint32_t other_tag : {
           semantic_movement_tag(
               MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_party_selection_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_inventory_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_spellbook_tag(
               0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
           semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_center_active_combatant_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_cycle_combat_focus_tag(
               1,
               CombatFocusDirection::next,
               REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_open_combat_items_tag(
               1, 0, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_show_combat_range_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_bandage_combatant_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_undo_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_open_combat_spellbook_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_open_combat_targeting_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_escape_combat_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_open_combat_scroll_case_tag(
               1, REALMZ_SEMANTIC_INPUT_COMBAT),
           semantic_center_combat_cursor_tag(
               1, {.x = 22, .y = 32}, REALMZ_SEMANTIC_INPUT_COMBAT),
       }) {
    CHECK(RealmzIsSemanticOpenScrollCaseTag(other_tag) == 0);
    CHECK(!scroll_case_tags.contains(other_tag));
  }
}

void test_bandage_tag_encoding_collision_and_malformed_rejection() {
  std::set<uint32_t> bandage_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_bandage_combatant_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52480000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticBandageCombatantTag(tag) != 0);
    CHECK(RealmzSemanticBandageCombatantTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
    CHECK(RealmzIsSemanticUndoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(tag) == 0);
    CHECK(bandage_tags.emplace(tag).second);
  }
  CHECK(bandage_tags.size() == 5);

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_party_selection_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_inventory_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_center_active_combatant_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1,
          CombatFocusDirection::previous,
          REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1,
          CombatFocusDirection::next,
          REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          1, 1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_show_combat_range_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_undo_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(!bandage_tags.contains(other_tag));
    CHECK(RealmzIsSemanticBandageCombatantTag(other_tag) == 0);
  }

  CHECK(semantic_bandage_combatant_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_bandage_combatant_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_bandage_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_bandage_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_bandage_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);

  for (const uint32_t malformed : {
           0U,
           0x52470000U,
           0x52480000U,
           0x52480101U,
           0x52480201U,
           0x52480401U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticBandageCombatantTag(malformed) == 0);
    CHECK(RealmzSemanticBandageCombatantTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
    CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_undo_tag_encoding_collision_and_malformed_rejection() {
  std::set<uint32_t> undo_tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_undo_combatant_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52550000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticUndoCombatantTag(tag) != 0);
    CHECK(RealmzSemanticUndoCombatantTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
    CHECK(RealmzIsSemanticBandageCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(tag) == 0);
    CHECK(undo_tags.emplace(tag).second);
  }
  CHECK(undo_tags.size() == 5);

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_party_selection_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_inventory_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_center_active_combatant_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1,
          CombatFocusDirection::previous,
          REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1,
          CombatFocusDirection::next,
          REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          1, 1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_show_combat_range_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_bandage_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(!undo_tags.contains(other_tag));
    CHECK(RealmzIsSemanticUndoCombatantTag(other_tag) == 0);
  }

  CHECK(semantic_undo_combatant_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_undo_combatant_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_undo_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_undo_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_undo_combatant_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);

  for (const uint32_t malformed : {
           0U,
           0x52540000U,
           0x52550000U,
           0x52550101U,
           0x52550201U,
           0x52550401U,
           0x5255FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticUndoCombatantTag(malformed) == 0);
    CHECK(RealmzSemanticUndoCombatantTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
    CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_open_combat_spellbook_tag_encoding_and_collisions() {
  std::set<uint32_t> tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_open_combat_spellbook_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x53430000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(tag) != 0);
    CHECK(RealmzSemanticOpenCombatSpellbookTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
    CHECK(RealmzIsSemanticBandageCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticUndoCombatantTag(tag) == 0);
    CHECK(tags.emplace(tag).second);
  }
  CHECK(tags.size() == 5);

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_party_selection_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_inventory_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_center_active_combatant_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1, CombatFocusDirection::next, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          1, 1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_show_combat_range_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_bandage_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_undo_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(!tags.contains(other_tag));
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(other_tag) == 0);
  }

  CHECK(semantic_open_combat_spellbook_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_spellbook_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_spellbook_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_combat_spellbook_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_open_combat_spellbook_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);

  for (const uint32_t malformed : {
           0U,
           0x53420000U,
           0x53430000U,
           0x53430101U,
           0x53430201U,
           0x53430401U,
           0x5343FF00U,
           0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(malformed) == 0);
    CHECK(RealmzSemanticOpenCombatSpellbookTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
    CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_open_combat_targeting_tag_encoding_and_collisions() {
  std::set<uint32_t> tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_open_combat_targeting_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52540000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticOpenCombatTargetingTag(tag) != 0);
    CHECK(RealmzSemanticOpenCombatTargetingTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
    CHECK(RealmzIsSemanticBandageCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticUndoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticEscapeCombatTag(tag) == 0);
    CHECK(tags.emplace(tag).second);
  }
  CHECK(tags.size() == 5);

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_party_selection_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_inventory_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_center_active_combatant_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1, CombatFocusDirection::next, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          1, 1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_show_combat_range_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_bandage_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_undo_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_escape_combat_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(!tags.contains(other_tag));
    CHECK(RealmzIsSemanticOpenCombatTargetingTag(other_tag) == 0);
  }

  CHECK(semantic_open_combat_targeting_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_targeting_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_targeting_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_combat_targeting_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_open_combat_targeting_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);

  for (const uint32_t malformed : {
           0U, 0x52530000U, 0x52540000U, 0x52540101U,
           0x52540201U, 0x52540401U, 0x5254FF00U, 0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenCombatTargetingTag(malformed) == 0);
    CHECK(RealmzSemanticOpenCombatTargetingTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
    CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_escape_combat_tag_encoding_and_collisions() {
  std::set<uint32_t> tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_escape_combat_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x52450000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticEscapeCombatTag(tag) != 0);
    CHECK(RealmzSemanticEscapeCombatTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
    CHECK(RealmzIsSemanticBandageCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticUndoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatTargetingTag(tag) == 0);
    CHECK(tags.emplace(tag).second);
  }
  CHECK(tags.size() == 5);

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_party_selection_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_inventory_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_center_active_combatant_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1, CombatFocusDirection::next, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          1, 1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_show_combat_range_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_bandage_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_undo_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_targeting_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(!tags.contains(other_tag));
    CHECK(RealmzIsSemanticEscapeCombatTag(other_tag) == 0);
  }

  CHECK(semantic_escape_combat_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_escape_combat_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_escape_combat_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_escape_combat_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_escape_combat_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);

  for (const uint32_t malformed : {
           0U, 0x52440000U, 0x52450000U, 0x52450101U,
           0x52450201U, 0x52450401U, 0x5245FF00U, 0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticEscapeCombatTag(malformed) == 0);
    CHECK(RealmzSemanticEscapeCombatTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
    CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_open_combat_scroll_case_tag_encoding_and_collisions() {
  std::set<uint32_t> tags;
  for (const CombatantId combatant : {0, 1, 10, 109, 255}) {
    const uint32_t tag = semantic_open_combat_scroll_case_tag(
        combatant, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFFF0000U) == 0x55530000U);
    CHECK(((tag >> 8U) & 0xFFU) == REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK((tag & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(RealmzIsSemanticOpenCombatScrollCaseTag(tag) != 0);
    CHECK(RealmzSemanticOpenCombatScrollCaseTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticMovementTag(tag) == 0);
    CHECK(RealmzIsSemanticPartySelectionTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenInventoryTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenSaveGameTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenLoadGameTag(tag) == 0);
    CHECK(RealmzIsSemanticGuardCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticFinishCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticDelayCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticSwitchWeaponTag(tag) == 0);
    CHECK(RealmzIsSemanticCycleCombatFocusTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatItemsTag(tag) == 0);
    CHECK(RealmzIsSemanticAutoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticShowCombatRangeTag(tag) == 0);
    CHECK(RealmzIsSemanticBandageCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticUndoCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatSpellbookTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatTargetingTag(tag) == 0);
    CHECK(RealmzIsSemanticEscapeCombatTag(tag) == 0);
    CHECK(tags.emplace(tag).second);
  }
  CHECK(tags.size() == 5);

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_party_selection_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_inventory_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_open_load_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_finish_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_delay_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_center_active_combatant_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_switch_weapon_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_cycle_combat_focus_tag(
          1, CombatFocusDirection::next, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          1, 1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_show_combat_range_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_bandage_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_undo_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_targeting_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_escape_combat_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(!tags.contains(other_tag));
    CHECK(RealmzIsSemanticOpenCombatScrollCaseTag(other_tag) == 0);
  }

  CHECK(semantic_open_combat_scroll_case_tag(
            -1, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_scroll_case_tag(
            256, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_open_combat_scroll_case_tag(
            1, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_open_combat_scroll_case_tag(
            1, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_open_combat_scroll_case_tag(
            1, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);

  for (const uint32_t malformed : {
           0U, 0x55520000U, 0x55530000U, 0x55530101U,
           0x55530201U, 0x55530401U, 0x5553FF00U, 0xFFFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticOpenCombatScrollCaseTag(malformed) == 0);
    CHECK(RealmzSemanticOpenCombatScrollCaseTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
    CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_center_combat_cursor_tag_encoding_and_collisions() {
  constexpr CombatFieldCell example_cell{.x = 42, .y = 17};
  const uint32_t example = semantic_center_combat_cursor_tag(
      3, example_cell, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(example == 0x4D032A11U);
  CHECK((example & 0xFF000000U) == 0x4D000000U);
  CHECK(((example >> 16U) & 0xFFU) == 3);
  CHECK(((example >> 8U) & 0xFFU) == 42);
  CHECK((example & 0xFFU) == 17);
  CHECK(RealmzIsSemanticCenterCombatCursorTag(example) != 0);
  CHECK(RealmzSemanticCenterCombatCursorTagSurface(example) ==
      REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzIsSemanticGameplayTag(example) != 0);
  CHECK(RealmzSemanticGameplayTagSurface(example) ==
      REALMZ_SEMANTIC_INPUT_COMBAT);

  std::set<uint32_t> tags;
  for (const auto& [combatant, cell] : {
           std::pair{CombatantId{0}, CombatFieldCell{.x = 0, .y = 0}},
           std::pair{CombatantId{1}, CombatFieldCell{.x = 1, .y = 2}},
           std::pair{CombatantId{109}, CombatFieldCell{.x = 44, .y = 55}},
           std::pair{CombatantId{255}, CombatFieldCell{.x = 89, .y = 89}},
       }) {
    const uint32_t tag = semantic_center_combat_cursor_tag(
        combatant, cell, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(tag != 0);
    CHECK((tag & 0xFF000000U) == 0x4D000000U);
    CHECK(((tag >> 16U) & 0xFFU) == static_cast<uint32_t>(combatant));
    CHECK(((tag >> 8U) & 0xFFU) == cell.x);
    CHECK((tag & 0xFFU) == cell.y);
    CHECK(RealmzIsSemanticCenterCombatCursorTag(tag) != 0);
    CHECK(RealmzSemanticCenterCombatCursorTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticGameplayTag(tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzIsSemanticCenterActiveCombatantTag(tag) == 0);
    CHECK(RealmzIsSemanticOpenCombatScrollCaseTag(tag) == 0);
    CHECK(tags.emplace(tag).second);
  }
  CHECK(tags.size() == 4);

  const std::array other_tags{
      semantic_movement_tag(
          MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      semantic_center_active_combatant_tag(
          3, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_items_tag(
          3, 3, REALMZ_SEMANTIC_INPUT_COMBAT),
      semantic_open_combat_scroll_case_tag(
          3, REALMZ_SEMANTIC_INPUT_COMBAT),
  };
  for (const uint32_t other_tag : other_tags) {
    CHECK(other_tag != 0);
    CHECK(RealmzIsSemanticCenterCombatCursorTag(other_tag) == 0);
    CHECK(RealmzSemanticCenterCombatCursorTagSurface(other_tag) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }

  CHECK(semantic_center_combat_cursor_tag(
            -1, example_cell, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_center_combat_cursor_tag(
            256, example_cell, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_center_combat_cursor_tag(
            3, {.x = 90, .y = 0}, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_center_combat_cursor_tag(
            3, {.x = 0, .y = 90}, REALMZ_SEMANTIC_INPUT_COMBAT) == 0);
  CHECK(semantic_center_combat_cursor_tag(
            3, example_cell, REALMZ_SEMANTIC_INPUT_NONE) == 0);
  CHECK(semantic_center_combat_cursor_tag(
            3, example_cell, REALMZ_SEMANTIC_INPUT_EXPLORATION) == 0);
  CHECK(semantic_center_combat_cursor_tag(
            3, example_cell, REALMZ_SEMANTIC_INPUT_DUNGEON) == 0);

  for (const uint32_t malformed : {
           0U,
           0x4C032A11U,
           0x4D035A11U,
           0x4D032A5AU,
           0x4DFFFFFFU,
       }) {
    CHECK(RealmzIsSemanticCenterCombatCursorTag(malformed) == 0);
    CHECK(RealmzSemanticCenterCombatCursorTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
    CHECK(RealmzIsSemanticGameplayTag(malformed) == 0);
    CHECK(RealmzSemanticGameplayTagSurface(malformed) ==
        REALMZ_SEMANTIC_INPUT_NONE);
  }
}

void test_scope_lifecycle_and_sticky_nested_failure() {
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);

  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzCurrentSemanticInputSurface() ==
      REALMZ_SEMANTIC_INPUT_EXPLORATION);

  // A nested bracket poisons this entire bracket. Further Begin calls cannot
  // accidentally re-enable semantic dispatch before the matching End.
  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_DUNGEON);
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);
  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);

  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);

  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_DUNGEON);
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_DUNGEON);
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);

  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_COMBAT);
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);

  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_NONE);
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);
  RealmzBeginSemanticInputSurface(
      static_cast<RealmzSemanticInputSurface>(255));
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);
  RealmzEndSemanticInputSurface();
  RealmzEndSemanticInputSurface();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
}

void test_explicit_invalidation_is_sticky_through_scope_cleanup() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t north = semantic_movement_tag(
      MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION);

  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzCurrentSemanticInputSurface() ==
      REALMZ_SEMANTIC_INPUT_EXPLORATION);
  RealmzInvalidateSemanticInputBoundary();
  CHECK(RealmzCurrentSemanticInputSurface() == REALMZ_SEMANTIC_INPUT_NONE);
  RealmzEndSemanticInputSurface();
  uint32_t output = 0xA5A5A5A5U;
  CHECK(!consume(REALMZ_SEMANTIC_INPUT_EXPLORATION, north, output));
  CHECK(output == 0xA5A5A5A5U);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  RealmzInvalidateSemanticInputBoundary();
  output = 0xA5A5A5A5U;
  CHECK(!consume(REALMZ_SEMANTIC_INPUT_EXPLORATION, north, output));
  CHECK(output == 0xA5A5A5A5U);
}

void test_top_level_bracket_and_exact_outdoor_translation() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);

  for (const auto& expected : kOutdoorMovements) {
    const int legacy_calls_before_active_consume = legacy_capture_calls;
    const int snapshot_calls_before_active_consume = snapshot_capture_calls;
    RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_EXPLORATION);
    const uint32_t tag = semantic_movement_tag(
        expected.command, RealmzCurrentSemanticInputSurface());
    CHECK(RealmzIsSemanticMovementTag(tag) != 0);

    uint32_t classic_message = 0xA5A5A5A5U;
    CHECK(!consume(
        REALMZ_SEMANTIC_INPUT_EXPLORATION, tag, classic_message));
    CHECK(classic_message == 0xA5A5A5A5U);
    CHECK(legacy_capture_calls == legacy_calls_before_active_consume);
    CHECK(snapshot_capture_calls == snapshot_calls_before_active_consume);

    RealmzEndSemanticInputSurface();
    CHECK(consume(
        REALMZ_SEMANTIC_INPUT_EXPLORATION, tag, classic_message));
    CHECK(classic_message == expected.classic_message);
  }
  CHECK(legacy_capture_calls == static_cast<int>(kOutdoorMovements.size()));
  CHECK(snapshot_capture_calls == static_cast<int>(kOutdoorMovements.size()));
}

void test_exact_dungeon_translation() {
  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    for (const auto& expected : kDungeonMovements) {
      const uint32_t tag = semantic_movement_tag(
          expected.command, REALMZ_SEMANTIC_INPUT_DUNGEON);
      uint32_t classic_message = 0xA5A5A5A5U;
      CHECK(consume_after_top_level_scope(
          REALMZ_SEMANTIC_INPUT_DUNGEON, tag, classic_message));
      CHECK(classic_message == expected.classic_message);
    }
  }
}

void test_party_selection_late_validation_and_single_use() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t select_cerys = semantic_party_selection_tag(
      2, REALMZ_SEMANTIC_INPUT_EXPLORATION);

  uint8_t member = 0xA5;
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(member == 2);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Authorization is single-use even after a successful consume.
  member = 0xA5;
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // An already-selected member remains a valid semantic target. The narrow C
  // adapter, not this read-only boundary, owns the idempotent no-op.
  captured_snapshot.party.selected_member = 2;
  captured_snapshot.party.members[0].selected = false;
  captured_snapshot.party.members[2].selected = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(member == 2);

  // A null destination consumes the completed authorization without reading
  // live legacy state.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticPartySelectionEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            select_cerys,
            nullptr) == 0);
  const int legacy_after_null = legacy_capture_calls;
  const int snapshot_after_null = snapshot_capture_calls;
  member = 0xA5;
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(legacy_capture_calls == legacy_after_null);
  CHECK(snapshot_capture_calls == snapshot_after_null);

  // Cross-surface and cross-action payloads fail before live capture.
  const uint32_t dungeon_selection = semantic_party_selection_tag(
      1, REALMZ_SEMANTIC_INPUT_DUNGEON);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  member = 0xA5;
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, dungeon_selection, member));
  CHECK(legacy_capture_calls == legacy_after_null);
  CHECK(snapshot_capture_calls == snapshot_after_null);

  const uint32_t north = semantic_movement_tag(
      MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, north, member));
  CHECK(legacy_capture_calls == legacy_after_null);
  CHECK(snapshot_capture_calls == snapshot_after_null);
  uint32_t classic_message = 0xA5A5A5A5U;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      select_cerys,
      classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == legacy_after_null);
  CHECK(snapshot_capture_calls == snapshot_after_null);

  // Member existence is checked against a fresh detached snapshot.
  const uint32_t missing_member = semantic_party_selection_tag(
      5, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  member = 0xA5;
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, missing_member, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == legacy_after_null + 1);
  CHECK(snapshot_capture_calls == snapshot_after_null + 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      false);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::dungeon_map);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  member = 0xA5;
  CHECK(!consume_selection(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, select_cerys, member));
  CHECK(member == 0xA5);
  CHECK(snapshot_capture_calls == 1);
}

void test_open_character_sheet_late_validation_and_single_use() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t open_arin = semantic_open_character_sheet_tag(
      0, REALMZ_SEMANTIC_INPUT_EXPLORATION);

  uint8_t member = 0xA5;
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // A completed top-level scope authorizes exactly one late consume.
  member = 0xA5;
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // A null output consumes that authorization before any mutable capture.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticOpenCharacterSheetEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            open_arin,
            nullptr) == 0);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
  member = 0xA5;
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Surface and action identity fail before consulting live state.
  const uint32_t dungeon_arin = semantic_open_character_sheet_tag(
      0, REALMZ_SEMANTIC_INPUT_DUNGEON);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, dungeon_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  const uint32_t inventory_arin = semantic_open_inventory_tag(
      0, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, inventory_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // A syntactically valid member byte outside Classic's six slots reaches
  // late snapshot validation, then fails without altering the destination.
  const uint32_t open_member_six = semantic_open_character_sheet_tag(
      6, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzIsSemanticOpenCharacterSheetTag(open_member_six) != 0);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_member_six, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 2);
  CHECK(snapshot_capture_calls == 2);

  // Both selected markers must identify the encoded member exactly.
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.party.selected_member = 1;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.party.members[0].selected = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.party.selected_member = 1;
  captured_snapshot.party.members[0].selected = false;
  captured_snapshot.party.members[1].selected = true;
  const uint32_t open_bryn = semantic_open_character_sheet_tag(
      1, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_bryn, member));
  CHECK(member == 1);

  // Adaptive, live-screen, snapshot-screen, and snapshot-capture failures all
  // leave the caller's one-shot destination untouched.
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      false);
  member = 0xA5;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::dungeon_map);
  member = 0xA5;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  member = 0xA5;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  member = 0xA5;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(snapshot_capture_calls == 1);

  // The late boundary rechecks the exact world presentation rather than
  // trusting only the broader outdoor/dungeon screen classification.
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::dungeon_map);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  const uint32_t mismatched_dungeon = semantic_open_character_sheet_tag(
      0, REALMZ_SEMANTIC_INPUT_DUNGEON);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  CHECK(!consume_character_sheet(
      REALMZ_SEMANTIC_INPUT_DUNGEON, mismatched_dungeon, member));
  CHECK(member == 0xA5);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Dungeon map and first-person presentations share the same guarded
  // dungeon surface and must preserve the exact selected-member identity.
  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    const uint32_t tag = semantic_open_character_sheet_tag(
        0, REALMZ_SEMANTIC_INPUT_DUNGEON);
    member = 0xA5;
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    CHECK(consume_character_sheet(
        REALMZ_SEMANTIC_INPUT_DUNGEON, tag, member));
    CHECK(member == 0);
    CHECK(legacy_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);
  }
}

void test_open_inventory_late_validation_and_exact_translation() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t open_arin = semantic_open_inventory_tag(
      0, REALMZ_SEMANTIC_INPUT_EXPLORATION);

  uint32_t classic_message = 0xA5A5A5A5U;
  CHECK(!consume_inventory(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_inventory(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0x00002269U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_inventory(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  // A null output still consumes the single-use authorization without
  // consulting mutable legacy state.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticOpenInventoryEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            open_arin,
            nullptr) == 0);
  const int legacy_after_null = legacy_capture_calls;
  const int snapshot_after_null = snapshot_capture_calls;
  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_inventory(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(legacy_capture_calls == legacy_after_null);
  CHECK(snapshot_capture_calls == snapshot_after_null);

  // A queued command cannot retarget after party selection changes.
  captured_snapshot.party.selected_member = 1;
  captured_snapshot.party.members[0].selected = false;
  captured_snapshot.party.members[1].selected = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_inventory(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == legacy_after_null + 1);
  CHECK(snapshot_capture_calls == snapshot_after_null + 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t missing_member = semantic_open_inventory_tag(
      5, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_inventory(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      missing_member,
      classic_message));
  CHECK(snapshot_capture_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    const uint32_t dungeon_inventory = semantic_open_inventory_tag(
        0, REALMZ_SEMANTIC_INPUT_DUNGEON);
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    classic_message = 0xA5A5A5A5U;
    CHECK(consume_inventory(
        REALMZ_SEMANTIC_INPUT_DUNGEON,
        dungeon_inventory,
        classic_message));
    CHECK(classic_message == 0x00002269U);
  }

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  const uint32_t dungeon_inventory = semantic_open_inventory_tag(
      0, REALMZ_SEMANTIC_INPUT_DUNGEON);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_inventory(
      REALMZ_SEMANTIC_INPUT_DUNGEON,
      dungeon_inventory,
      classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_inventory(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(snapshot_capture_calls == 1);
}

void test_open_spellbook_late_validation_and_exact_translation() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t open_arin = semantic_open_spellbook_tag(
      0, REALMZ_SEMANTIC_INPUT_EXPLORATION);

  uint32_t classic_message = 0xA5A5A5A5U;
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0x00000173U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticOpenSpellbookEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            open_arin,
            nullptr) == 0);
  const int legacy_after_null = legacy_capture_calls;
  const int snapshot_after_null = snapshot_capture_calls;
  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(legacy_capture_calls == legacy_after_null);
  CHECK(snapshot_capture_calls == snapshot_after_null);

  // Every caster prerequisite is checked against the processing-time
  // snapshot, so a queued command cannot retarget or outlive eligibility.
  captured_snapshot.party.selected_member = 1;
  captured_snapshot.party.members[0].selected = false;
  captured_snapshot.party.members[1].selected = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.party.members[0].conscious = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.party.members[0].spell_points.current = 0;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t missing_member = semantic_open_spellbook_tag(
      5, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      missing_member,
      classic_message));
  CHECK(snapshot_capture_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    const uint32_t dungeon_spellbook = semantic_open_spellbook_tag(
        0, REALMZ_SEMANTIC_INPUT_DUNGEON);
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    classic_message = 0xA5A5A5A5U;
    CHECK(consume_spellbook(
        REALMZ_SEMANTIC_INPUT_DUNGEON,
        dungeon_spellbook,
        classic_message));
    CHECK(classic_message == 0x00000173U);
  }

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  const uint32_t dungeon_spellbook = semantic_open_spellbook_tag(
      0, REALMZ_SEMANTIC_INPUT_DUNGEON);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_DUNGEON,
      dungeon_spellbook,
      classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      false);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_spellbook(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(snapshot_capture_calls == 1);
}

void test_open_scroll_case_late_validation_and_surface_translation() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t open_arin = semantic_open_scroll_case_tag(
      0, REALMZ_SEMANTIC_INPUT_EXPLORATION);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == 0x0000256CU);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Completed-scope authorization is one-shot.
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  // A null output consumes authorization without consulting mutable state.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticOpenScrollCaseEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            open_arin,
            nullptr) == 0);
  const int legacy_after_null = legacy_capture_calls;
  const int snapshot_after_null = snapshot_capture_calls;
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == legacy_after_null);
  CHECK(snapshot_capture_calls == snapshot_after_null);

  // A queued command cannot retarget after selection changes.
  captured_snapshot.party.selected_member = 1;
  captured_snapshot.party.members[0].selected = false;
  captured_snapshot.party.members[1].selected = true;
  captured_snapshot.party.members[1].use_scroll_available = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.party.members[0].selected = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.party.members[0].use_scroll_available = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t missing_member = semantic_open_scroll_case_tag(
      5, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      missing_member,
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 1);

  // The payload surface must match the freshly completed scope before any
  // mutable legacy state is consulted.
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t dungeon_tag = semantic_open_scroll_case_tag(
      0, REALMZ_SEMANTIC_INPUT_DUNGEON);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, dungeon_tag, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    const uint32_t open_dungeon_scroll_case = semantic_open_scroll_case_tag(
        0, REALMZ_SEMANTIC_INPUT_DUNGEON);
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    classic_message = kUnchangedClassicMessage;
    CHECK(consume_scroll_case(
        REALMZ_SEMANTIC_INPUT_DUNGEON,
        open_dungeon_scroll_case,
        classic_message));
    CHECK(classic_message == 0x00002370U);
  }

  // A screen-correct snapshot with the wrong world presentation still fails
  // closed instead of handing Classic the other surface's shortcut.
  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_DUNGEON, dungeon_tag, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::dungeon_map);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::dungeon_map);
  captured_legacy_context.screen = REALMZ_LEGACY_SCREEN_EXPLORATION;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_DUNGEON, dungeon_tag, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      false);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_scroll_case(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, open_arin, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 1);
}

void test_open_save_game_late_validation_and_exact_menu_translation() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t open_save = semantic_open_save_game_tag(
      REALMZ_SEMANTIC_INPUT_EXPLORATION);

  int16_t menu_id = 0x5A5A;
  int16_t item_id = 0x4B4B;
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(menu_id == 0x5A5A);
  CHECK(item_id == 0x4B4B);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(menu_id == 129);
  CHECK(item_id == 3);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // A completed scope authorizes exactly one attempted translation.
  menu_id = 0x5A5A;
  item_id = 0x4B4B;
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(menu_id == 0x5A5A);
  CHECK(item_id == 0x4B4B);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Missing either destination consumes authorization before any live read.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticOpenSaveGameEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            open_save,
            nullptr,
            &item_id) == 0);
  CHECK(item_id == 0x4B4B);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticOpenSaveGameEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            open_save,
            &menu_id,
            nullptr) == 0);
  CHECK(menu_id == 0x5A5A);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Cross-surface and cross-action payloads fail before mutable capture.
  const uint32_t dungeon_save = semantic_open_save_game_tag(
      REALMZ_SEMANTIC_INPUT_DUNGEON);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      dungeon_save,
      menu_id,
      item_id));
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      semantic_open_inventory_tag(0, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      menu_id,
      item_id));
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    menu_id = 0;
    item_id = 0;
    CHECK(consume_save_game(
        REALMZ_SEMANTIC_INPUT_DUNGEON,
        dungeon_save,
        menu_id,
        item_id));
    CHECK(menu_id == 129);
    CHECK(item_id == 3);
  }

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      false);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::dungeon_map);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::dungeon_map);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_DUNGEON,
      dungeon_save,
      menu_id,
      item_id));
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_save_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_save,
      menu_id,
      item_id));
  CHECK(snapshot_capture_calls == 1);
}

void test_open_load_game_late_validation_and_exact_menu_translation() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t open_load = semantic_open_load_game_tag(
      REALMZ_SEMANTIC_INPUT_EXPLORATION);
  int16_t menu_id = 0x5A5A;
  int16_t item_id = 0x4B4B;

  CHECK(!consume_load_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_load,
      menu_id,
      item_id));
  CHECK(menu_id == 0x5A5A);
  CHECK(item_id == 0x4B4B);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_load_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_load,
      menu_id,
      item_id));
  CHECK(menu_id == 129);
  CHECK(item_id == 2);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Authorization is single-use and a missing destination consumes it.
  menu_id = 0x5A5A;
  item_id = 0x4B4B;
  CHECK(!consume_load_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_load,
      menu_id,
      item_id));
  CHECK(menu_id == 0x5A5A);
  CHECK(item_id == 0x4B4B);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticOpenLoadGameEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION,
            open_load,
            nullptr,
            &item_id) == 0);

  // A save tag cannot cross into the load route.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_load_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      semantic_open_save_game_tag(REALMZ_SEMANTIC_INPUT_EXPLORATION),
      menu_id,
      item_id));
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    const uint32_t dungeon_load = semantic_open_load_game_tag(
        REALMZ_SEMANTIC_INPUT_DUNGEON);
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    menu_id = 0;
    item_id = 0;
    CHECK(consume_load_game(
        REALMZ_SEMANTIC_INPUT_DUNGEON,
        dungeon_load,
        menu_id,
        item_id));
    CHECK(menu_id == 129);
    CHECK(item_id == 2);
  }

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      false);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_load_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_load,
      menu_id,
      item_id));
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_load_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_load,
      menu_id,
      item_id));
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_load_game(
      REALMZ_SEMANTIC_INPUT_EXPLORATION,
      open_load,
      menu_id,
      item_id));
  CHECK(snapshot_capture_calls == 1);
}

void test_rest_party_late_validation_and_exact_translation() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.world.in_camp = true;
  const uint32_t rest = semantic_rest_party_tag(
      REALMZ_SEMANTIC_INPUT_EXPLORATION);
  const uint32_t combat_range = semantic_show_combat_range_tag(
      0, REALMZ_SEMANTIC_INPUT_COMBAT);
  uint32_t output = kUnchangedClassicMessage;

  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(consume_rest_party(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, output));
  CHECK(output == 0x00000F72U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Delivery authorization is one-shot, and even a missing destination burns
  // the completed scope without touching the fresh context.
  output = kUnchangedClassicMessage;
  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, output));
  CHECK(output == kUnchangedClassicMessage);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticRestPartyEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, nullptr) == 0);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Combat Range's same eventual Classic key cannot cross into Rest's typed,
  // world-only tag route.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, combat_range, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  for (const auto presentation : {
           WorldPresentation::dungeon_map,
           WorldPresentation::dungeon_first_person,
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_DUNGEON,
        ScreenContext::dungeon,
        presentation);
    captured_snapshot.world.in_camp = true;
    const uint32_t dungeon_rest = semantic_rest_party_tag(
        REALMZ_SEMANTIC_INPUT_DUNGEON);
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    output = kUnchangedClassicMessage;
    CHECK(consume_rest_party(
        REALMZ_SEMANTIC_INPUT_DUNGEON, dungeon_rest, output));
    CHECK(output == 0x00000F72U);
  }

  // Originating-surface mismatch fails before any context is captured.
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.world.in_camp = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  output = kUnchangedClassicMessage;
  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_DUNGEON, rest, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  // Fresh camp state is authoritative; leaving camp while queued rejects the
  // command without manufacturing a rest quantum.
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.world.in_camp = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      false);
  captured_snapshot.world.in_camp = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  for (const auto invalid : {
           std::pair{
               ScreenContext::dungeon,
               WorldPresentation::outdoor,
           },
           std::pair{
               ScreenContext::exploration,
               WorldPresentation::dungeon_map,
           },
       }) {
    reset_capture(
        REALMZ_LEGACY_SCREEN_EXPLORATION,
        invalid.first,
        invalid.second);
    captured_snapshot.world.in_camp = true;
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
    CHECK(!consume_rest_party(
        REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, output));
    CHECK(output == kUnchangedClassicMessage);
  }

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  captured_snapshot.world.in_camp = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_DUNGEON,
      semantic_rest_party_tag(REALMZ_SEMANTIC_INPUT_DUNGEON),
      output));
  CHECK(output == kUnchangedClassicMessage);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  captured_snapshot.world.in_camp = true;
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(!consume_rest_party(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, rest, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 1);
}

void test_shared_combat_late_validation_matrix() {
  for (const auto& action : kCombatActionCases) {
    reset_valid_shared_combat();
    const uint32_t tag = action.tag(
        1, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(tag != 0);

    uint32_t classic_message = kUnchangedClassicMessage;
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(action.consume(
        REALMZ_SEMANTIC_INPUT_COMBAT, tag, classic_message));
    CHECK(classic_message == action.classic_message);
    CHECK(legacy_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);

    classic_message = kUnchangedClassicMessage;
    CHECK(!action.consume(
        REALMZ_SEMANTIC_INPUT_COMBAT, tag, classic_message));
    CHECK(classic_message == kUnchangedClassicMessage);
    CHECK(legacy_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);

    for (const auto rejection : kSharedCombatRejections) {
      reset_valid_shared_combat();
      const GameSnapshot valid_snapshot = captured_snapshot;
      classic_message = kUnchangedClassicMessage;
      complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
      apply_shared_combat_rejection(rejection);

      CHECK(!action.consume(
          REALMZ_SEMANTIC_INPUT_COMBAT, tag, classic_message));
      CHECK(classic_message == kUnchangedClassicMessage);
      CHECK(legacy_capture_calls == 1);
      CHECK(snapshot_capture_calls == 1);

      // A failed delivery consumes its authorization too. Repairing the
      // snapshot cannot make the same tagged event valid without a new scope.
      captured_snapshot = valid_snapshot;
      CHECK(!action.consume(
          REALMZ_SEMANTIC_INPUT_COMBAT, tag, classic_message));
      CHECK(classic_message == kUnchangedClassicMessage);
      CHECK(legacy_capture_calls == 1);
      CHECK(snapshot_capture_calls == 1);
    }
  }
}

void test_show_combat_range_route_boundaries() {
  reset_valid_shared_combat();
  const uint32_t show_range = semantic_show_combat_range_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(show_range != 0);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_show_combat_range(
      REALMZ_SEMANTIC_INPUT_COMBAT, show_range, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticShowCombatRangeEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, show_range, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  // A same-shaped actor tag from another command family cannot enter the
  // Range consumer or change its output sentinel.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_show_combat_range(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_auto_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);
}

void test_bandage_combatant_route_and_canundo_boundaries() {
  reset_valid_shared_combat();
  const uint32_t bandage = semantic_bandage_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(bandage != 0);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_bandage_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, bandage, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticBandageCombatantEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, bandage, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  // A same-shaped combatant payload from another command family cannot enter
  // Bandage, and consuming the failed authorization leaves the sentinel inert.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_bandage_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_show_combat_range_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_bandage_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, bandage, classic_message));
  CHECK(classic_message == 0x00000B62U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_bandage_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, bandage, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_valid_shared_combat();
  captured_snapshot.combat->bandage_available = false;
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_bandage_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, bandage, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // The processing-time canundo copy is authoritative and rejection is
  // one-shot: repairing it cannot revive the already consumed scope.
  captured_snapshot.combat->bandage_available = true;
  CHECK(!consume_bandage_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, bandage, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
}

void test_undo_combatant_route_and_canundo_boundaries() {
  reset_valid_shared_combat();
  const uint32_t undo = semantic_undo_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(undo == 0x52550301U);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_undo_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, undo, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticUndoCombatantEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, undo, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_undo_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_bandage_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_undo_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, undo, classic_message));
  CHECK(classic_message == 0x00002075U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_undo_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, undo, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_valid_shared_combat();
  captured_snapshot.combat->undo_available = false;
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_undo_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, undo, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  captured_snapshot.combat->undo_available = true;
  CHECK(!consume_undo_combatant(
      REALMZ_SEMANTIC_INPUT_COMBAT, undo, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
}

void test_open_combat_spellbook_route_and_availability_boundaries() {
  reset_valid_shared_combat();
  const uint32_t open_spellbook = semantic_open_combat_spellbook_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(open_spellbook == 0x53430301U);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_open_combat_spellbook(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_spellbook, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticOpenCombatSpellbookEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, open_spellbook, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_open_combat_spellbook(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_open_spellbook_tag(1, REALMZ_SEMANTIC_INPUT_EXPLORATION),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_open_combat_spellbook(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_spellbook, classic_message));
  CHECK(classic_message == 0x00000173U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_open_combat_spellbook(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_spellbook, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_valid_shared_combat();
  captured_snapshot.combat->cast_spell_available = false;
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_open_combat_spellbook(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_spellbook, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  captured_snapshot.combat->cast_spell_available = true;
  CHECK(!consume_open_combat_spellbook(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_spellbook, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
}

void test_open_combat_targeting_route_and_availability_boundaries() {
  reset_valid_shared_combat();
  const uint32_t targeting = semantic_open_combat_targeting_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(targeting == 0x52540301U);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_open_combat_targeting(
      REALMZ_SEMANTIC_INPUT_COMBAT, targeting, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticOpenCombatTargetingEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, targeting, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_open_combat_targeting(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_open_combat_spellbook_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_valid_shared_combat();
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_open_combat_targeting(
      REALMZ_SEMANTIC_INPUT_COMBAT, targeting, classic_message));
  CHECK(classic_message == 0x00001174U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_open_combat_targeting(
      REALMZ_SEMANTIC_INPUT_COMBAT, targeting, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_valid_shared_combat();
  captured_snapshot.combat->target_available = false;
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_open_combat_targeting(
      REALMZ_SEMANTIC_INPUT_COMBAT, targeting, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  captured_snapshot.combat->target_available = true;
  CHECK(!consume_open_combat_targeting(
      REALMZ_SEMANTIC_INPUT_COMBAT, targeting, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
}

void test_escape_combat_route_boundaries() {
  reset_valid_shared_combat();
  const uint32_t escape = semantic_escape_combat_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(escape == 0x52450301U);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_escape_combat(
      REALMZ_SEMANTIC_INPUT_COMBAT, escape, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticEscapeCombatEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, escape, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  // Target and Escape both carry one actor, but their tags and consumers are
  // deliberately disjoint.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_escape_combat(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_open_combat_targeting_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_escape_combat(
      REALMZ_SEMANTIC_INPUT_COMBAT, escape, classic_message));
  CHECK(classic_message == 0x00000E65U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_escape_combat(
      REALMZ_SEMANTIC_INPUT_COMBAT, escape, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
}

void test_open_combat_scroll_case_route_and_availability_boundaries() {
  reset_valid_shared_combat();
  const uint32_t scroll_case = semantic_open_combat_scroll_case_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(scroll_case == 0x55530301U);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_open_combat_scroll_case(
      REALMZ_SEMANTIC_INPUT_COMBAT, scroll_case, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticOpenCombatScrollCaseEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, scroll_case, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  // Scroll and Escape both carry only the actor, but their semantic command
  // families and consumers are deliberately disjoint.
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_open_combat_scroll_case(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_escape_combat_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_open_combat_scroll_case(
      REALMZ_SEMANTIC_INPUT_COMBAT, scroll_case, classic_message));
  CHECK(classic_message == 0x0000256CU);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_open_combat_scroll_case(
      REALMZ_SEMANTIC_INPUT_COMBAT, scroll_case, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  reset_valid_shared_combat();
  captured_snapshot.combat->use_scroll_available = false;
  classic_message = kUnchangedClassicMessage;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_open_combat_scroll_case(
      REALMZ_SEMANTIC_INPUT_COMBAT, scroll_case, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  captured_snapshot.combat->use_scroll_available = true;
  CHECK(!consume_open_combat_scroll_case(
      REALMZ_SEMANTIC_INPUT_COMBAT, scroll_case, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
}

void test_center_combat_cursor_route_and_viewport_boundaries() {
  constexpr CombatFieldCell queued_cell{.x = 5, .y = 4};
  const uint32_t center_cursor = semantic_center_combat_cursor_tag(
      1, queued_cell, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(center_cursor == 0x4D010504U);

  reset_valid_shared_combat();
  uint32_t classic_message = kUnchangedClassicMessage;
  uint8_t absolute_x = 0xA5;
  uint8_t absolute_y = 0x5A;
  CHECK(!consume_center_combat_cursor(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      center_cursor,
      classic_message,
      absolute_x,
      absolute_y));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(absolute_x == 0xA5);
  CHECK(absolute_y == 0x5A);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  // Every output is mandatory. A malformed call spends its authorization
  // without reading either live context or the detached snapshot.
  for (int missing_output = 0; missing_output < 3; ++missing_output) {
    reset_valid_shared_combat();
    classic_message = kUnchangedClassicMessage;
    absolute_x = 0xA5;
    absolute_y = 0x5A;
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(RealmzConsumeSemanticCenterCombatCursorEvent(
              REALMZ_SEMANTIC_INPUT_COMBAT,
              center_cursor,
              missing_output == 0 ? nullptr : &classic_message,
              missing_output == 1 ? nullptr : &absolute_x,
              missing_output == 2 ? nullptr : &absolute_y) == 0);
    CHECK(classic_message == kUnchangedClassicMessage);
    CHECK(absolute_x == 0xA5);
    CHECK(absolute_y == 0x5A);
    CHECK(legacy_capture_calls == 0);
    CHECK(snapshot_capture_calls == 0);
    CHECK(!consume_center_combat_cursor(
        REALMZ_SEMANTIC_INPUT_COMBAT,
        center_cursor,
        classic_message,
        absolute_x,
        absolute_y));
    CHECK(legacy_capture_calls == 0);
    CHECK(snapshot_capture_calls == 0);
  }

  // The absolute payload survives a camera-origin change after queueing and
  // need not lie inside the fresh viewport.
  reset_valid_shared_combat();
  captured_snapshot.combat->field_origin_x = 70;
  captured_snapshot.combat->field_origin_y = 60;
  captured_snapshot.combat->visible_columns = 15;
  captured_snapshot.combat->visible_rows = 13;
  classic_message = kUnchangedClassicMessage;
  absolute_x = 0xA5;
  absolute_y = 0x5A;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_center_combat_cursor(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      center_cursor,
      classic_message,
      absolute_x,
      absolute_y));
  CHECK(classic_message == 0x00002E6DU);
  CHECK(absolute_x == queued_cell.x);
  CHECK(absolute_y == queued_cell.y);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  absolute_x = 0xA5;
  absolute_y = 0x5A;
  CHECK(!consume_center_combat_cursor(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      center_cursor,
      classic_message,
      absolute_x,
      absolute_y));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(absolute_x == 0xA5);
  CHECK(absolute_y == 0x5A);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // The complete active-party gate applies independently of viewport checks.
  for (const auto rejection : kSharedCombatRejections) {
    reset_valid_shared_combat();
    const GameSnapshot valid_snapshot = captured_snapshot;
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
    apply_shared_combat_rejection(rejection);
    classic_message = kUnchangedClassicMessage;
    absolute_x = 0xA5;
    absolute_y = 0x5A;
    CHECK(!consume_center_combat_cursor(
        REALMZ_SEMANTIC_INPUT_COMBAT,
        center_cursor,
        classic_message,
        absolute_x,
        absolute_y));
    CHECK(classic_message == kUnchangedClassicMessage);
    CHECK(absolute_x == 0xA5);
    CHECK(absolute_y == 0x5A);
    CHECK(legacy_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);

    captured_snapshot = valid_snapshot;
    CHECK(!consume_center_combat_cursor(
        REALMZ_SEMANTIC_INPUT_COMBAT,
        center_cursor,
        classic_message,
        absolute_x,
        absolute_y));
    CHECK(legacy_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);
  }

  struct ViewportCase {
    int32_t origin_x;
    int32_t origin_y;
    std::size_t columns;
    std::size_t rows;
    bool accepted;
  };
  constexpr std::array viewport_cases{
      ViewportCase{0, 0, 1, 1, true},
      ViewportCase{75, 77, 15, 13, true},
      ViewportCase{-1, 0, 15, 13, false},
      ViewportCase{0, -1, 15, 13, false},
      ViewportCase{0, 0, 0, 13, false},
      ViewportCase{0, 0, 15, 0, false},
      ViewportCase{76, 0, 15, 13, false},
      ViewportCase{0, 78, 15, 13, false},
      ViewportCase{0, 0, 91, 1, false},
      ViewportCase{0, 0, 1, 91, false},
  };
  for (const auto& viewport : viewport_cases) {
    reset_valid_shared_combat();
    captured_snapshot.combat->field_origin_x = viewport.origin_x;
    captured_snapshot.combat->field_origin_y = viewport.origin_y;
    captured_snapshot.combat->visible_columns = viewport.columns;
    captured_snapshot.combat->visible_rows = viewport.rows;
    classic_message = kUnchangedClassicMessage;
    absolute_x = 0xA5;
    absolute_y = 0x5A;
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
    const bool consumed = consume_center_combat_cursor(
        REALMZ_SEMANTIC_INPUT_COMBAT,
        center_cursor,
        classic_message,
        absolute_x,
        absolute_y);
    CHECK(consumed == viewport.accepted);
    if (viewport.accepted) {
      CHECK(classic_message == 0x00002E6DU);
      CHECK(absolute_x == queued_cell.x);
      CHECK(absolute_y == queued_cell.y);
    } else {
      CHECK(classic_message == kUnchangedClassicMessage);
      CHECK(absolute_x == 0xA5);
      CHECK(absolute_y == 0x5A);
    }
  }

  reset_valid_shared_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  classic_message = kUnchangedClassicMessage;
  absolute_x = 0xA5;
  absolute_y = 0x5A;
  CHECK(!consume_center_combat_cursor(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_center_active_combatant_tag(
          1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message,
      absolute_x,
      absolute_y));
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_center_combat_cursor(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      center_cursor,
      classic_message,
      absolute_x,
      absolute_y));
  CHECK(snapshot_capture_calls == 1);
}

void test_open_combat_items_late_validation_and_exact_translation() {
  reset_valid_shared_combat();
  const uint32_t open_items = semantic_open_combat_items_tag(
      1, 0, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(open_items != 0);

  uint32_t classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == 0x00002269U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticOpenCombatItemsEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, open_items, nullptr) == 0);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  // Every shared acting-party gate also applies to Combat Items.
  for (const auto rejection : kSharedCombatRejections) {
    reset_valid_shared_combat();
    const GameSnapshot valid_snapshot = captured_snapshot;
    classic_message = kUnchangedClassicMessage;
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
    apply_shared_combat_rejection(rejection);

    CHECK(!consume_combat_items(
        REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
    CHECK(classic_message == kUnchangedClassicMessage);
    CHECK(legacy_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);

    captured_snapshot = valid_snapshot;
    CHECK(!consume_combat_items(
        REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
    CHECK(classic_message == kUnchangedClassicMessage);
    CHECK(legacy_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);
  }

  // The selected member is independent from the acting combatant and must
  // remain both present and selected until delivery.
  reset_valid_shared_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  captured_snapshot.party.selected_member = 2;
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_valid_shared_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  captured_snapshot.party.members[0].selected = false;
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  reset_valid_shared_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  captured_snapshot.party.members.erase(
      captured_snapshot.party.members.begin());
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);

  // Another command family cannot cross into the Combat Items consumer.
  reset_valid_shared_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      semantic_guard_combatant_tag(1, REALMZ_SEMANTIC_INPUT_COMBAT),
      classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  captured_legacy_context.adaptive_eligible = 0;
  captured_legacy_context.requires_full_frame = 1;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 0);

  reset_valid_shared_combat();
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  classic_message = kUnchangedClassicMessage;
  CHECK(!consume_combat_items(
      REALMZ_SEMANTIC_INPUT_COMBAT, open_items, classic_message));
  CHECK(classic_message == kUnchangedClassicMessage);
  CHECK(snapshot_capture_calls == 1);
}

void test_guard_combatant_late_validation_and_exact_translation() {
  const auto configure_valid_combat = [] {
    captured_snapshot.combat = CombatView{
        .active = true,
        .round = 3,
        .acting_combatant = 1,
        .combatants = {
            CombatantView{
                .id = 1,
                .kind = CombatantKind::party_member,
                .name = "Bryn",
                .stamina = {14, 20},
                .active = true,
                .targetable = true,
            },
            CombatantView{
                .id = 10,
                .kind = CombatantKind::monster,
                .name = "Goblin",
                .stamina = {8, 8},
                .targetable = true,
            },
        },
    };
  };

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  const uint32_t guard = semantic_guard_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  uint32_t classic_message = 0xA5A5A5A5U;

  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(classic_message == 0x00000567U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticGuardCombatantEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, guard, nullptr) == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->acting_combatant = 10;
  captured_snapshot.combat->combatants[0].active = false;
  captured_snapshot.combat->combatants[1].active = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(snapshot_capture_calls == 1);

  const uint32_t monster_guard = semantic_guard_combatant_tag(
      10, REALMZ_SEMANTIC_INPUT_COMBAT);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, monster_guard, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  captured_snapshot.combat.reset();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->active = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->combatants[0].active = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->combatants.erase(
      captured_snapshot.combat->combatants.begin());
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->combatants[0].stamina = {0, 20};
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->combatants[0].targetable = false;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none,
      false);
  configure_valid_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_guard(
      REALMZ_SEMANTIC_INPUT_COMBAT, guard, classic_message));
  CHECK(snapshot_capture_calls == 1);
}

void test_finish_combatant_late_validation_and_exact_translation() {
  const auto configure_valid_combat = [] {
    captured_snapshot.combat = CombatView{
        .active = true,
        .round = 3,
        .acting_combatant = 1,
        .combatants = {
            CombatantView{
                .id = 1,
                .kind = CombatantKind::party_member,
                .name = "Bryn",
                .stamina = {14, 20},
                .active = true,
                .targetable = true,
            },
            CombatantView{
                .id = 10,
                .kind = CombatantKind::monster,
                .name = "Goblin",
                .stamina = {8, 8},
                .active = true,
                .targetable = true,
            },
        },
    };
  };

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  const uint32_t finish = semantic_finish_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  uint32_t classic_message = 0xA5A5A5A5U;

  CHECK(!consume_finish(
      REALMZ_SEMANTIC_INPUT_COMBAT, finish, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_finish(
      REALMZ_SEMANTIC_INPUT_COMBAT, finish, classic_message));
  CHECK(classic_message == 0x00000366U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_finish(
      REALMZ_SEMANTIC_INPUT_COMBAT, finish, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticFinishCombatantEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, finish, nullptr) == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->acting_combatant = 10;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_finish(
      REALMZ_SEMANTIC_INPUT_COMBAT, finish, classic_message));

  const uint32_t monster_finish = semantic_finish_combatant_tag(
      10, REALMZ_SEMANTIC_INPUT_COMBAT);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_finish(
      REALMZ_SEMANTIC_INPUT_COMBAT, monster_finish, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_finish(
      REALMZ_SEMANTIC_INPUT_COMBAT, finish, classic_message));
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_finish(
      REALMZ_SEMANTIC_INPUT_COMBAT, finish, classic_message));
  CHECK(snapshot_capture_calls == 1);
}

void test_delay_combatant_late_validation_and_exact_translation() {
  const auto configure_valid_delay = [] {
    captured_snapshot.party.members[1].movement = 9;
    captured_snapshot.party.members[1].movement_maximum = 9;
    captured_snapshot.combat = CombatView{
        .active = true,
        .round = 3,
        .acting_combatant = 1,
        .combatants = {
            CombatantView{
                .id = 1,
                .kind = CombatantKind::party_member,
                .name = "Bryn",
                .stamina = {14, 20},
                .active = true,
                .targetable = true,
            },
            CombatantView{
                .id = 10,
                .kind = CombatantKind::monster,
                .name = "Goblin",
                .stamina = {8, 8},
                .active = true,
                .targetable = true,
            },
        },
    };
  };

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_delay();
  const uint32_t delay = semantic_delay_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  uint32_t classic_message = 0xA5A5A5A5U;

  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(classic_message == 0x00000264U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticDelayCombatantEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, delay, nullptr) == 0);
  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_delay();
  captured_snapshot.party.members[1].movement = 8;
  classic_message = 0xA5A5A5A5U;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  captured_snapshot.party.members[1].movement = 9;
  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_delay();
  captured_snapshot.party.members.erase(
      captured_snapshot.party.members.begin() + 1);
  classic_message = 0xA5A5A5A5U;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_delay();
  captured_snapshot.combat->acting_combatant = 10;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_delay();
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_delay(
      REALMZ_SEMANTIC_INPUT_COMBAT, delay, classic_message));
  CHECK(snapshot_capture_calls == 1);
}

void test_center_active_combatant_late_validation_and_exact_translation() {
  const auto configure_valid_combat = [] {
    captured_snapshot.combat = CombatView{
        .active = true,
        .round = 3,
        .acting_combatant = 1,
        .combatants = {
            CombatantView{
                .id = 1,
                .kind = CombatantKind::party_member,
                .name = "Bryn",
                .stamina = {14, 20},
                .active = true,
                .targetable = true,
            },
            CombatantView{
                .id = 10,
                .kind = CombatantKind::monster,
                .name = "Goblin",
                .stamina = {8, 8},
                .active = true,
                .targetable = true,
            },
        },
    };
  };

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  const uint32_t center = semantic_center_active_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  uint32_t classic_message = 0xA5A5A5A5U;

  CHECK(!consume_center_active(
      REALMZ_SEMANTIC_INPUT_COMBAT, center, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(consume_center_active(
      REALMZ_SEMANTIC_INPUT_COMBAT, center, classic_message));
  CHECK(classic_message == 0x00000863U);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);

  classic_message = 0xA5A5A5A5U;
  CHECK(!consume_center_active(
      REALMZ_SEMANTIC_INPUT_COMBAT, center, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzConsumeSemanticCenterActiveCombatantEvent(
            REALMZ_SEMANTIC_INPUT_COMBAT, center, nullptr) == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  captured_snapshot.combat->acting_combatant = 10;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_center_active(
      REALMZ_SEMANTIC_INPUT_COMBAT, center, classic_message));

  const uint32_t monster_center = semantic_center_active_combatant_tag(
      10, REALMZ_SEMANTIC_INPUT_COMBAT);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_center_active(
      REALMZ_SEMANTIC_INPUT_COMBAT, monster_center, classic_message));

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_center_active(
      REALMZ_SEMANTIC_INPUT_COMBAT, center, classic_message));
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_COMBAT,
      ScreenContext::combat,
      WorldPresentation::none);
  configure_valid_combat();
  snapshot_capture_throws = true;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(!consume_center_active(
      REALMZ_SEMANTIC_INPUT_COMBAT, center, classic_message));
  CHECK(snapshot_capture_calls == 1);
}

void expect_rejected_without_output_change(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tag) {
  uint32_t classic_message = 0xA5A5A5A5U;
  CHECK(!consume(expected_surface, tag, classic_message));
  CHECK(classic_message == 0xA5A5A5A5U);
}

void test_fail_closed_context_and_payloads() {
  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  const uint32_t north = semantic_movement_tag(
      MovementCommand::north, REALMZ_SEMANTIC_INPUT_EXPLORATION);
  const uint32_t forward = semantic_movement_tag(
      MovementCommand::step_forward, REALMZ_SEMANTIC_INPUT_DUNGEON);

  uint32_t output = 0xA5A5A5A5U;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(RealmzConsumeSemanticMovementEvent(
            REALMZ_SEMANTIC_INPUT_EXPLORATION, north, nullptr) == 0);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  expect_rejected_without_output_change(REALMZ_SEMANTIC_INPUT_NONE, north);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_DUNGEON, north);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, forward);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, 0x524D010CU);
  CHECK(legacy_capture_calls == 0);
  CHECK(snapshot_capture_calls == 0);

  captured_legacy_context.adaptive_eligible = 0;
  captured_legacy_context.requires_full_frame = 1;
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, north);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::dungeon_map);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, north);
  CHECK(legacy_capture_calls == 1);
  CHECK(snapshot_capture_calls == 0);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, north);
  CHECK(snapshot_capture_calls == 1);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::dungeon_map);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, north);

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::outdoor);
  complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
  expect_rejected_without_output_change(
      REALMZ_SEMANTIC_INPUT_DUNGEON, forward);

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  // Relative dungeon commands are never accepted on the outdoor surface.
  for (const auto& movement : kDungeonMovements) {
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_EXPLORATION);
    expect_rejected_without_output_change(
        REALMZ_SEMANTIC_INPUT_EXPLORATION,
        semantic_movement_tag(
            movement.command, REALMZ_SEMANTIC_INPUT_EXPLORATION));
  }

  reset_capture(
      REALMZ_LEGACY_SCREEN_DUNGEON,
      ScreenContext::dungeon,
      WorldPresentation::dungeon_first_person);
  // Absolute outdoor commands are never accepted on the dungeon surface.
  for (const auto& movement : kOutdoorMovements) {
    complete_top_level_scope(REALMZ_SEMANTIC_INPUT_DUNGEON);
    expect_rejected_without_output_change(
        REALMZ_SEMANTIC_INPUT_DUNGEON,
        semantic_movement_tag(
            movement.command, REALMZ_SEMANTIC_INPUT_DUNGEON));
  }

  reset_capture(
      REALMZ_LEGACY_SCREEN_EXPLORATION,
      ScreenContext::exploration,
      WorldPresentation::outdoor);
  snapshot_capture_throws = true;
  output = 0xA5A5A5A5U;
  CHECK(!consume_after_top_level_scope(
      REALMZ_SEMANTIC_INPUT_EXPLORATION, north, output));
  CHECK(output == 0xA5A5A5A5U);
  CHECK(snapshot_capture_calls == 1);
}

} // namespace

extern "C" RealmzLegacyPresentationContext
RealmzCaptureLegacyPresentationContext(void) {
  ++legacy_capture_calls;
  return captured_legacy_context;
}

namespace realmz::presentation {

GameSnapshot LegacyGameSnapshotSource::capture() const {
  ++snapshot_capture_calls;
  if (snapshot_capture_throws) {
    throw std::runtime_error("injected snapshot failure");
  }
  return captured_snapshot;
}

} // namespace realmz::presentation

int main() {
  try {
    test_tag_encoding_and_validation();
    test_open_character_sheet_tag_encoding_and_collisions();
    test_rest_party_tag_encoding_and_collisions();
    test_open_scroll_case_tag_encoding_and_collisions();
    test_bandage_tag_encoding_collision_and_malformed_rejection();
    test_undo_tag_encoding_collision_and_malformed_rejection();
    test_open_combat_spellbook_tag_encoding_and_collisions();
    test_open_combat_targeting_tag_encoding_and_collisions();
    test_escape_combat_tag_encoding_and_collisions();
    test_open_combat_scroll_case_tag_encoding_and_collisions();
    test_center_combat_cursor_tag_encoding_and_collisions();
    test_scope_lifecycle_and_sticky_nested_failure();
    test_explicit_invalidation_is_sticky_through_scope_cleanup();
    test_top_level_bracket_and_exact_outdoor_translation();
    test_exact_dungeon_translation();
    test_party_selection_late_validation_and_single_use();
    test_open_character_sheet_late_validation_and_single_use();
    test_open_inventory_late_validation_and_exact_translation();
    test_open_spellbook_late_validation_and_exact_translation();
    test_open_scroll_case_late_validation_and_surface_translation();
    test_open_save_game_late_validation_and_exact_menu_translation();
    test_open_load_game_late_validation_and_exact_menu_translation();
    test_rest_party_late_validation_and_exact_translation();
    test_shared_combat_late_validation_matrix();
    test_show_combat_range_route_boundaries();
    test_bandage_combatant_route_and_canundo_boundaries();
    test_undo_combatant_route_and_canundo_boundaries();
    test_open_combat_spellbook_route_and_availability_boundaries();
    test_open_combat_targeting_route_and_availability_boundaries();
    test_escape_combat_route_boundaries();
    test_open_combat_scroll_case_route_and_availability_boundaries();
    test_center_combat_cursor_route_and_viewport_boundaries();
    test_open_combat_items_late_validation_and_exact_translation();
    test_guard_combatant_late_validation_and_exact_translation();
    test_finish_combatant_late_validation_and_exact_translation();
    test_delay_combatant_late_validation_and_exact_translation();
    test_center_active_combatant_late_validation_and_exact_translation();
    test_fail_closed_context_and_payloads();
    RealmzEndSemanticInputSurface();
    std::cout << "SemanticInputBoundaryTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    RealmzEndSemanticInputSurface();
    std::cerr << "SemanticInputBoundaryTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
