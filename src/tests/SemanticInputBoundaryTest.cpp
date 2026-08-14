#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

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
    test_scope_lifecycle_and_sticky_nested_failure();
    test_explicit_invalidation_is_sticky_through_scope_cleanup();
    test_top_level_bracket_and_exact_outdoor_translation();
    test_exact_dungeon_translation();
    test_party_selection_late_validation_and_single_use();
    test_open_inventory_late_validation_and_exact_translation();
    test_open_spellbook_late_validation_and_exact_translation();
    test_open_save_game_late_validation_and_exact_menu_translation();
    test_open_load_game_late_validation_and_exact_menu_translation();
    test_shared_combat_late_validation_matrix();
    test_show_combat_range_route_boundaries();
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
