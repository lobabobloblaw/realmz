#include "SemanticInputBoundary.h"

#include <algorithm>
#include <optional>

#include "LegacyGameSnapshotSource.hpp"
#include "LegacyPresentationContext.h"
#include "RuntimeLegacyCommandBridge.hpp"
#include "UIAction.hpp"

namespace {

constexpr uint32_t kSemanticMovementSignature = 0x524D0000U;
constexpr uint32_t kSemanticMovementMask = 0xFFFF0000U;
constexpr uint32_t kSemanticMovementSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticMovementCommandMask = 0x000000FFU;
constexpr uint32_t kSemanticPartySelectionSignature = 0x52530000U;
constexpr uint32_t kSemanticPartySelectionMask = 0xFFFF0000U;
constexpr uint32_t kSemanticPartySelectionSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticPartySelectionMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenCharacterSheetSignature = 0x43530000U;
constexpr uint32_t kSemanticOpenCharacterSheetMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenCharacterSheetSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenCharacterSheetMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenInventorySignature = 0x52490000U;
constexpr uint32_t kSemanticOpenInventoryMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenInventorySurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenInventoryMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenSpellbookSignature = 0x52500000U;
constexpr uint32_t kSemanticOpenSpellbookMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenSpellbookSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenSpellbookMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenScrollCaseSignature = 0x53550000U;
constexpr uint32_t kSemanticOpenScrollCaseMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenScrollCaseSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenScrollCaseMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenSaveGameSignature = 0x52560000U;
constexpr uint32_t kSemanticOpenSaveGameMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenSaveGameSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenSaveGameReservedMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenLoadGameSignature = 0x524C0000U;
constexpr uint32_t kSemanticOpenLoadGameMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenLoadGameSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenLoadGameReservedMask = 0x000000FFU;
constexpr uint32_t kSemanticRestPartySignature = 0x57520000U;
constexpr uint32_t kSemanticRestPartyMask = 0xFFFF0000U;
constexpr uint32_t kSemanticRestPartySurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticRestPartyReservedMask = 0x000000FFU;
constexpr uint32_t kSemanticDelayCombatantSignature = 0x52440000U;
constexpr uint32_t kSemanticDelayCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticDelayCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticDelayCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticCenterActiveCombatantSignature = 0x52430000U;
constexpr uint32_t kSemanticCenterActiveCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticCenterActiveCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticCenterActiveCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticSwitchWeaponSignature = 0x52570000U;
constexpr uint32_t kSemanticSwitchWeaponMask = 0xFFFF0000U;
constexpr uint32_t kSemanticSwitchWeaponSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticSwitchWeaponIdMask = 0x000000FFU;
constexpr uint32_t kSemanticCycleCombatFocusNextSignature = 0x524E0000U;
constexpr uint32_t kSemanticCycleCombatFocusPreviousSignature = 0x52420000U;
constexpr uint32_t kSemanticCycleCombatFocusMask = 0xFFFF0000U;
constexpr uint32_t kSemanticCycleCombatFocusSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticCycleCombatFocusIdMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenCombatItemsSignature = 0x49000000U;
constexpr uint32_t kSemanticOpenCombatItemsMask = 0xFF000000U;
constexpr uint32_t kSemanticOpenCombatItemsSurfaceMask = 0x00FF0000U;
constexpr uint32_t kSemanticOpenCombatItemsActorMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenCombatItemsMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticAutoCombatantSignature = 0x52410000U;
constexpr uint32_t kSemanticAutoCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticAutoCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticAutoCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticShowCombatRangeSignature = 0x52520000U;
constexpr uint32_t kSemanticShowCombatRangeMask = 0xFFFF0000U;
constexpr uint32_t kSemanticShowCombatRangeSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticShowCombatRangeIdMask = 0x000000FFU;
constexpr uint32_t kSemanticBandageCombatantSignature = 0x52480000U;
constexpr uint32_t kSemanticBandageCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticBandageCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticBandageCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticUndoCombatantSignature = 0x52550000U;
constexpr uint32_t kSemanticUndoCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticUndoCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticUndoCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenCombatSpellbookSignature = 0x53430000U;
constexpr uint32_t kSemanticOpenCombatSpellbookMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenCombatSpellbookSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenCombatSpellbookIdMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenCombatTargetingSignature = 0x52540000U;
constexpr uint32_t kSemanticOpenCombatTargetingMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenCombatTargetingSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenCombatTargetingIdMask = 0x000000FFU;
constexpr uint32_t kSemanticEscapeCombatSignature = 0x52450000U;
constexpr uint32_t kSemanticEscapeCombatMask = 0xFFFF0000U;
constexpr uint32_t kSemanticEscapeCombatSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticEscapeCombatIdMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenCombatScrollCaseSignature = 0x55530000U;
constexpr uint32_t kSemanticOpenCombatScrollCaseMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenCombatScrollCaseSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenCombatScrollCaseIdMask = 0x000000FFU;
constexpr uint32_t kSemanticCenterCombatCursorSignature = 0x4D000000U;
constexpr uint32_t kSemanticCenterCombatCursorMask = 0xFF000000U;
constexpr uint32_t kSemanticCenterCombatCursorActorMask = 0x00FF0000U;
constexpr uint32_t kSemanticCenterCombatCursorXMask = 0x0000FF00U;
constexpr uint32_t kSemanticCenterCombatCursorYMask = 0x000000FFU;
constexpr uint32_t kSemanticFinishCombatantSignature = 0x52460000U;
constexpr uint32_t kSemanticFinishCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticFinishCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticFinishCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticGuardCombatantSignature = 0x52470000U;
constexpr uint32_t kSemanticGuardCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticGuardCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticGuardCombatantIdMask = 0x000000FFU;
constexpr RealmzSemanticInputSurface kNoSemanticInputSurface =
    REALMZ_SEMANTIC_INPUT_NONE;
uint32_t scope_depth = 0;
bool scope_invalid = false;
RealmzSemanticInputSurface scope_surface = kNoSemanticInputSurface;
RealmzSemanticInputSurface completed_surface = kNoSemanticInputSurface;

struct DecodedMovement {
  realmz::presentation::MovementCommand command;
  RealmzSemanticInputSurface surface;
};

struct DecodedPartySelection {
  realmz::presentation::PartyMemberId member;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenCharacterSheet {
  realmz::presentation::PartyMemberId member;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenInventory {
  realmz::presentation::PartyMemberId member;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenSpellbook {
  realmz::presentation::PartyMemberId member;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenScrollCase {
  realmz::presentation::PartyMemberId member;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenSaveGame {
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenLoadGame {
  RealmzSemanticInputSurface surface;
};

struct DecodedRestParty {
  RealmzSemanticInputSurface surface;
};

struct DecodedGuardCombatant {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedFinishCombatant {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedDelayCombatant {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedCenterActiveCombatant {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedSwitchWeapon {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedCycleCombatFocus {
  realmz::presentation::CombatantId combatant;
  realmz::presentation::CombatFocusDirection direction;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenCombatItems {
  realmz::presentation::CombatantId combatant;
  realmz::presentation::PartyMemberId member;
  RealmzSemanticInputSurface surface;
};

struct DecodedAutoCombatant {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedShowCombatRange {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedBandageCombatant {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedUndoCombatant {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenCombatSpellbook {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenCombatTargeting {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedEscapeCombat {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenCombatScrollCase {
  realmz::presentation::CombatantId combatant;
  RealmzSemanticInputSurface surface;
};

struct DecodedCenterCombatCursor {
  realmz::presentation::CombatantId combatant;
  realmz::presentation::CombatFieldCell cell;
  RealmzSemanticInputSurface surface;
};

bool is_world_gameplay_surface(RealmzSemanticInputSurface surface) noexcept {
  return (surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) ||
      (surface == REALMZ_SEMANTIC_INPUT_DUNGEON);
}

bool is_semantic_input_surface(RealmzSemanticInputSurface surface) noexcept {
  return is_world_gameplay_surface(surface) ||
      (surface == REALMZ_SEMANTIC_INPUT_COMBAT);
}

std::optional<DecodedMovement> decode_movement(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticMovementMask) !=
      kSemanticMovementSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticMovementSurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  const uint32_t value = tagged_message & kSemanticMovementCommandMask;
  if (value > static_cast<uint32_t>(
                  realmz::presentation::MovementCommand::northwest)) {
    return std::nullopt;
  }
  return DecodedMovement{
      .command = static_cast<realmz::presentation::MovementCommand>(value),
      .surface = surface_value,
  };
}

std::optional<DecodedPartySelection> decode_party_selection(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticPartySelectionMask) !=
      kSemanticPartySelectionSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticPartySelectionSurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  return DecodedPartySelection{
      .member = static_cast<realmz::presentation::PartyMemberId>(
          tagged_message & kSemanticPartySelectionMemberMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenCharacterSheet> decode_open_character_sheet(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenCharacterSheetMask) !=
      kSemanticOpenCharacterSheetSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenCharacterSheetSurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  return DecodedOpenCharacterSheet{
      .member = static_cast<realmz::presentation::PartyMemberId>(
          tagged_message & kSemanticOpenCharacterSheetMemberMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenInventory> decode_open_inventory(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenInventoryMask) !=
      kSemanticOpenInventorySignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenInventorySurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  return DecodedOpenInventory{
      .member = static_cast<realmz::presentation::PartyMemberId>(
          tagged_message & kSemanticOpenInventoryMemberMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenSpellbook> decode_open_spellbook(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenSpellbookMask) !=
      kSemanticOpenSpellbookSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenSpellbookSurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  return DecodedOpenSpellbook{
      .member = static_cast<realmz::presentation::PartyMemberId>(
          tagged_message & kSemanticOpenSpellbookMemberMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenScrollCase> decode_open_scroll_case(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenScrollCaseMask) !=
      kSemanticOpenScrollCaseSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenScrollCaseSurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  return DecodedOpenScrollCase{
      .member = static_cast<realmz::presentation::PartyMemberId>(
          tagged_message & kSemanticOpenScrollCaseMemberMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenSaveGame> decode_open_save_game(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenSaveGameMask) !=
          kSemanticOpenSaveGameSignature ||
      (tagged_message & kSemanticOpenSaveGameReservedMask) != 0) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenSaveGameSurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  return DecodedOpenSaveGame{.surface = surface_value};
}

std::optional<DecodedOpenLoadGame> decode_open_load_game(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenLoadGameMask) !=
          kSemanticOpenLoadGameSignature ||
      (tagged_message & kSemanticOpenLoadGameReservedMask) != 0) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenLoadGameSurfaceMask) >> 8U;
  if ((surface_value != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface_value != REALMZ_SEMANTIC_INPUT_DUNGEON)) {
    return std::nullopt;
  }
  return DecodedOpenLoadGame{.surface = surface_value};
}

std::optional<DecodedRestParty> decode_rest_party(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticRestPartyMask) !=
          kSemanticRestPartySignature ||
      (tagged_message & kSemanticRestPartyReservedMask) != 0) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticRestPartySurfaceMask) >> 8U;
  if (!is_world_gameplay_surface(surface_value)) {
    return std::nullopt;
  }
  return DecodedRestParty{.surface = surface_value};
}

std::optional<DecodedGuardCombatant> decode_guard_combatant(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticGuardCombatantMask) !=
      kSemanticGuardCombatantSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticGuardCombatantSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedGuardCombatant{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticGuardCombatantIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedFinishCombatant> decode_finish_combatant(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticFinishCombatantMask) !=
      kSemanticFinishCombatantSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticFinishCombatantSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedFinishCombatant{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticFinishCombatantIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedDelayCombatant> decode_delay_combatant(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticDelayCombatantMask) !=
      kSemanticDelayCombatantSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticDelayCombatantSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedDelayCombatant{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticDelayCombatantIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedCenterActiveCombatant> decode_center_active_combatant(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticCenterActiveCombatantMask) !=
      kSemanticCenterActiveCombatantSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticCenterActiveCombatantSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedCenterActiveCombatant{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticCenterActiveCombatantIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedSwitchWeapon> decode_switch_weapon(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticSwitchWeaponMask) !=
      kSemanticSwitchWeaponSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticSwitchWeaponSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedSwitchWeapon{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticSwitchWeaponIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedCycleCombatFocus> decode_cycle_combat_focus(
    uint32_t tagged_message) noexcept {
  const uint32_t signature =
      tagged_message & kSemanticCycleCombatFocusMask;
  realmz::presentation::CombatFocusDirection direction;
  if (signature == kSemanticCycleCombatFocusPreviousSignature) {
    direction = realmz::presentation::CombatFocusDirection::previous;
  } else if (signature == kSemanticCycleCombatFocusNextSignature) {
    direction = realmz::presentation::CombatFocusDirection::next;
  } else {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticCycleCombatFocusSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedCycleCombatFocus{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticCycleCombatFocusIdMask),
      .direction = direction,
      .surface = surface_value,
  };
}

std::optional<DecodedOpenCombatItems> decode_open_combat_items(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenCombatItemsMask) !=
      kSemanticOpenCombatItemsSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenCombatItemsSurfaceMask) >> 16U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedOpenCombatItems{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          (tagged_message & kSemanticOpenCombatItemsActorMask) >> 8U),
      .member = static_cast<realmz::presentation::PartyMemberId>(
          tagged_message & kSemanticOpenCombatItemsMemberMask),
      .surface = surface_value,
  };
}

std::optional<DecodedAutoCombatant> decode_auto_combatant(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticAutoCombatantMask) !=
      kSemanticAutoCombatantSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticAutoCombatantSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedAutoCombatant{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticAutoCombatantIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedShowCombatRange> decode_show_combat_range(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticShowCombatRangeMask) !=
      kSemanticShowCombatRangeSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticShowCombatRangeSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedShowCombatRange{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticShowCombatRangeIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedBandageCombatant> decode_bandage_combatant(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticBandageCombatantMask) !=
      kSemanticBandageCombatantSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticBandageCombatantSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedBandageCombatant{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticBandageCombatantIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedUndoCombatant> decode_undo_combatant(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticUndoCombatantMask) !=
      kSemanticUndoCombatantSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticUndoCombatantSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedUndoCombatant{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticUndoCombatantIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenCombatSpellbook> decode_open_combat_spellbook(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenCombatSpellbookMask) !=
      kSemanticOpenCombatSpellbookSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenCombatSpellbookSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedOpenCombatSpellbook{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticOpenCombatSpellbookIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenCombatTargeting> decode_open_combat_targeting(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenCombatTargetingMask) !=
      kSemanticOpenCombatTargetingSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenCombatTargetingSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedOpenCombatTargeting{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticOpenCombatTargetingIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedEscapeCombat> decode_escape_combat(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticEscapeCombatMask) !=
      kSemanticEscapeCombatSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticEscapeCombatSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedEscapeCombat{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticEscapeCombatIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedOpenCombatScrollCase> decode_open_combat_scroll_case(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticOpenCombatScrollCaseMask) !=
      kSemanticOpenCombatScrollCaseSignature) {
    return std::nullopt;
  }
  const uint32_t surface_value =
      (tagged_message & kSemanticOpenCombatScrollCaseSurfaceMask) >> 8U;
  if (surface_value != REALMZ_SEMANTIC_INPUT_COMBAT) {
    return std::nullopt;
  }
  return DecodedOpenCombatScrollCase{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          tagged_message & kSemanticOpenCombatScrollCaseIdMask),
      .surface = surface_value,
  };
}

std::optional<DecodedCenterCombatCursor> decode_center_combat_cursor(
    uint32_t tagged_message) noexcept {
  if ((tagged_message & kSemanticCenterCombatCursorMask) !=
      kSemanticCenterCombatCursorSignature) {
    return std::nullopt;
  }
  const auto absolute_x = static_cast<uint8_t>(
      (tagged_message & kSemanticCenterCombatCursorXMask) >> 8U);
  const auto absolute_y = static_cast<uint8_t>(
      tagged_message & kSemanticCenterCombatCursorYMask);
  if ((absolute_x > 89) || (absolute_y > 89)) {
    return std::nullopt;
  }
  return DecodedCenterCombatCursor{
      .combatant = static_cast<realmz::presentation::CombatantId>(
          (tagged_message & kSemanticCenterCombatCursorActorMask) >> 16U),
      .cell = {.x = absolute_x, .y = absolute_y},
      .surface = REALMZ_SEMANTIC_INPUT_COMBAT,
  };
}

bool authorize_completed_scope(
    RealmzSemanticInputSurface expected_surface) noexcept {
  const bool completed_expected_scope =
      (scope_depth == 0) &&
      (completed_surface == expected_surface) &&
      is_semantic_input_surface(expected_surface);
  // Delivery authorization is single-use regardless of whether the payload or
  // processing-time context validates. A later event needs a fresh scope.
  completed_surface = kNoSemanticInputSurface;
  return completed_expected_scope;
}

realmz::presentation::ScreenContext screen_for_surface(
    RealmzSemanticInputSurface surface) noexcept {
  using realmz::presentation::ScreenContext;
  switch (surface) {
    case REALMZ_SEMANTIC_INPUT_EXPLORATION:
      return ScreenContext::exploration;
    case REALMZ_SEMANTIC_INPUT_DUNGEON:
      return ScreenContext::dungeon;
    case REALMZ_SEMANTIC_INPUT_COMBAT:
      return ScreenContext::combat;
    case REALMZ_SEMANTIC_INPUT_NONE:
    default:
      return ScreenContext::title;
  }
}

using CombatantSnapshotValidator = bool (*)(
    const realmz::presentation::GameSnapshot&,
    realmz::presentation::CombatantId) noexcept;

bool has_full_movement(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId combatant) noexcept {
  if ((combatant < 0) || (combatant > 0xFF)) {
    return false;
  }
  const auto* member = snapshot.party.member(
      static_cast<realmz::presentation::PartyMemberId>(combatant));
  return member && (member->movement == member->movement_maximum);
}

bool is_bandage_available(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId) noexcept {
  return snapshot.combat && snapshot.combat->bandage_available;
}

bool is_undo_available(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId) noexcept {
  return snapshot.combat && snapshot.combat->undo_available;
}

bool is_combat_spellbook_available(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId) noexcept {
  return snapshot.combat && snapshot.combat->cast_spell_available;
}

bool is_combat_targeting_available(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId) noexcept {
  return snapshot.combat && snapshot.combat->target_available;
}

bool is_combat_scroll_available(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId) noexcept {
  return snapshot.combat && snapshot.combat->use_scroll_available;
}

bool is_live_active_party_combatant(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId combatant_id) noexcept {
  if (!snapshot.combat || !snapshot.combat->active ||
      (snapshot.combat->acting_combatant != combatant_id)) {
    return false;
  }
  const auto combatant = std::ranges::find(
      snapshot.combat->combatants,
      combatant_id,
      &realmz::presentation::CombatantView::id);
  const auto* party_member =
      (combatant_id >= 0) && (combatant_id <= 0xFF)
      ? snapshot.party.member(
            static_cast<realmz::presentation::PartyMemberId>(combatant_id))
      : nullptr;
  return party_member &&
      (combatant != snapshot.combat->combatants.end()) &&
      (combatant->kind == realmz::presentation::CombatantKind::party_member) &&
      combatant->active && combatant->targetable &&
      (combatant->stamina.current > 0);
}

bool has_sane_current_battlefield_viewport(
    const realmz::presentation::GameSnapshot& snapshot,
    realmz::presentation::CombatantId) noexcept {
  if (!snapshot.combat || (snapshot.combat->field_origin_x < 0) ||
      (snapshot.combat->field_origin_y < 0) ||
      (snapshot.combat->visible_columns == 0) ||
      (snapshot.combat->visible_rows == 0) ||
      (snapshot.combat->visible_columns > 90) ||
      (snapshot.combat->visible_rows > 90)) {
    return false;
  }
  return snapshot.combat->field_origin_x <=
          90 - static_cast<int32_t>(snapshot.combat->visible_columns) &&
      snapshot.combat->field_origin_y <=
          90 - static_cast<int32_t>(snapshot.combat->visible_rows);
}

template <typename DecodedCombatant, typename MessageMapper>
uint8_t consume_semantic_combatant_event(
    RealmzSemanticInputSurface expected_surface,
    const std::optional<DecodedCombatant>& decoded,
    uint32_t* classic_key_message,
    MessageMapper message_mapper,
    CombatantSnapshotValidator snapshot_validator = nullptr,
    std::optional<realmz::presentation::PartyMemberId>
        required_selected_member = std::nullopt) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!classic_key_message || !authorized || !decoded ||
      (decoded->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    if ((snapshot.screen != screen) ||
        !is_live_active_party_combatant(snapshot, decoded->combatant)) {
      return 0;
    }
    if (required_selected_member) {
      const auto* selected_member =
          snapshot.party.member(*required_selected_member);
      if (!selected_member || !selected_member->selected ||
          (snapshot.party.selected_member != *required_selected_member)) {
        return 0;
      }
    }
    if (snapshot_validator &&
        !snapshot_validator(snapshot, decoded->combatant)) {
      return 0;
    }
    const auto message = message_mapper(
        decoded->combatant,
        {
            .screen = screen,
            .world_presentation = snapshot.world.presentation,
            .adaptive_eligible = true,
        });
    if (!message) {
      return 0;
    }
    *classic_key_message = *message;
    return 1;
  } catch (...) {
    return 0;
  }
}

} // namespace

namespace realmz::presentation {

uint32_t semantic_movement_tag(
    MovementCommand command,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface) ||
      (static_cast<uint32_t>(command) >
          static_cast<uint32_t>(MovementCommand::northwest))) {
    return 0;
  }
  return kSemanticMovementSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(command);
}

uint32_t semantic_party_selection_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticPartySelectionSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_open_character_sheet_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenCharacterSheetSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_open_inventory_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenInventorySignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_open_spellbook_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenSpellbookSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_open_scroll_case_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenScrollCaseSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_open_save_game_tag(
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenSaveGameSignature |
      (static_cast<uint32_t>(surface) << 8U);
}

uint32_t semantic_open_load_game_tag(
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenLoadGameSignature |
      (static_cast<uint32_t>(surface) << 8U);
}

uint32_t semantic_rest_party_tag(
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_world_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticRestPartySignature |
      (static_cast<uint32_t>(surface) << 8U);
}

uint32_t semantic_guard_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticGuardCombatantSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_finish_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticFinishCombatantSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_delay_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticDelayCombatantSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_center_active_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticCenterActiveCombatantSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_switch_weapon_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticSwitchWeaponSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_cycle_combat_focus_tag(
    CombatantId combatant,
    CombatFocusDirection direction,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  uint32_t signature = 0;
  switch (direction) {
    case CombatFocusDirection::previous:
      signature = kSemanticCycleCombatFocusPreviousSignature;
      break;
    case CombatFocusDirection::next:
      signature = kSemanticCycleCombatFocusNextSignature;
      break;
    default:
      return 0;
  }
  return signature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_open_combat_items_tag(
    CombatantId combatant,
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticOpenCombatItemsSignature |
      (static_cast<uint32_t>(surface) << 16U) |
      (static_cast<uint32_t>(combatant) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_auto_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticAutoCombatantSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_show_combat_range_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticShowCombatRangeSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_bandage_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticBandageCombatantSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_undo_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticUndoCombatantSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_open_combat_spellbook_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticOpenCombatSpellbookSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_open_combat_targeting_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticOpenCombatTargetingSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_escape_combat_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticEscapeCombatSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_open_combat_scroll_case_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return 0;
  }
  return kSemanticOpenCombatScrollCaseSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(combatant);
}

uint32_t semantic_center_combat_cursor_tag(
    CombatantId combatant,
    CombatFieldCell cell,
    RealmzSemanticInputSurface surface) noexcept {
  if ((surface != REALMZ_SEMANTIC_INPUT_COMBAT) ||
      (combatant < 0) || (combatant > 0xFF) || (cell.x > 89) ||
      (cell.y > 89)) {
    return 0;
  }
  return kSemanticCenterCombatCursorSignature |
      (static_cast<uint32_t>(combatant) << 16U) |
      (static_cast<uint32_t>(cell.x) << 8U) |
      static_cast<uint32_t>(cell.y);
}

} // namespace realmz::presentation

extern "C" void RealmzBeginSemanticInputSurface(
    RealmzSemanticInputSurface surface) {
  completed_surface = kNoSemanticInputSurface;
  if (scope_depth == 0) {
    scope_surface = is_semantic_input_surface(surface)
        ? surface
        : kNoSemanticInputSurface;
    scope_invalid = !is_semantic_input_surface(surface);
  } else {
    // Nested scopes are a programming error. Stay fail-closed until the
    // complete nesting depth unwinds; a third Begin cannot reopen the route.
    scope_invalid = true;
  }
  ++scope_depth;
}

extern "C" void RealmzEndSemanticInputSurface(void) {
  if (scope_depth == 0) {
    completed_surface = kNoSemanticInputSurface;
    scope_surface = kNoSemanticInputSurface;
    return;
  }
  --scope_depth;
  if (scope_depth == 0) {
    completed_surface = scope_invalid
        ? kNoSemanticInputSurface
        : scope_surface;
    scope_surface = kNoSemanticInputSurface;
    scope_invalid = false;
  }
}

extern "C" RealmzSemanticInputSurface
RealmzCurrentSemanticInputSurface(void) {
  return ((scope_depth == 1) && !scope_invalid)
      ? scope_surface
      : kNoSemanticInputSurface;
}

extern "C" void RealmzInvalidateSemanticInputBoundary(void) {
  scope_depth = 0;
  scope_invalid = false;
  scope_surface = kNoSemanticInputSurface;
  completed_surface = kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticMovementTag(
    uint32_t tagged_message) {
  return decode_movement(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticMovementTagSurface(uint32_t tagged_message) {
  const auto movement = decode_movement(tagged_message);
  return movement ? movement->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticPartySelectionTag(
    uint32_t tagged_message) {
  return decode_party_selection(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticPartySelectionTagSurface(uint32_t tagged_message) {
  const auto selection = decode_party_selection(tagged_message);
  return selection ? selection->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenCharacterSheetTag(
    uint32_t tagged_message) {
  return decode_open_character_sheet(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenCharacterSheetTagSurface(uint32_t tagged_message) {
  const auto character_sheet = decode_open_character_sheet(tagged_message);
  return character_sheet
      ? character_sheet->surface
      : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenInventoryTag(
    uint32_t tagged_message) {
  return decode_open_inventory(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenInventoryTagSurface(uint32_t tagged_message) {
  const auto inventory = decode_open_inventory(tagged_message);
  return inventory ? inventory->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenSpellbookTag(
    uint32_t tagged_message) {
  return decode_open_spellbook(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenSpellbookTagSurface(uint32_t tagged_message) {
  const auto spellbook = decode_open_spellbook(tagged_message);
  return spellbook ? spellbook->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenScrollCaseTag(
    uint32_t tagged_message) {
  return decode_open_scroll_case(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenScrollCaseTagSurface(uint32_t tagged_message) {
  const auto scroll_case = decode_open_scroll_case(tagged_message);
  return scroll_case ? scroll_case->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenSaveGameTag(
    uint32_t tagged_message) {
  return decode_open_save_game(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenSaveGameTagSurface(uint32_t tagged_message) {
  const auto save_game = decode_open_save_game(tagged_message);
  return save_game ? save_game->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenLoadGameTag(
    uint32_t tagged_message) {
  return decode_open_load_game(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenLoadGameTagSurface(uint32_t tagged_message) {
  const auto load_game = decode_open_load_game(tagged_message);
  return load_game ? load_game->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticRestPartyTag(
    uint32_t tagged_message) {
  return decode_rest_party(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticRestPartyTagSurface(uint32_t tagged_message) {
  const auto rest_party = decode_rest_party(tagged_message);
  return rest_party ? rest_party->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticGuardCombatantTag(
    uint32_t tagged_message) {
  return decode_guard_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticGuardCombatantTagSurface(uint32_t tagged_message) {
  const auto guard = decode_guard_combatant(tagged_message);
  return guard ? guard->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticFinishCombatantTag(
    uint32_t tagged_message) {
  return decode_finish_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticFinishCombatantTagSurface(uint32_t tagged_message) {
  const auto finish = decode_finish_combatant(tagged_message);
  return finish ? finish->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticDelayCombatantTag(
    uint32_t tagged_message) {
  return decode_delay_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticDelayCombatantTagSurface(uint32_t tagged_message) {
  const auto delay = decode_delay_combatant(tagged_message);
  return delay ? delay->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticCenterActiveCombatantTag(
    uint32_t tagged_message) {
  return decode_center_active_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticCenterActiveCombatantTagSurface(uint32_t tagged_message) {
  const auto center = decode_center_active_combatant(tagged_message);
  return center ? center->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticSwitchWeaponTag(
    uint32_t tagged_message) {
  return decode_switch_weapon(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticSwitchWeaponTagSurface(uint32_t tagged_message) {
  const auto switch_weapon = decode_switch_weapon(tagged_message);
  return switch_weapon ? switch_weapon->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticCycleCombatFocusTag(
    uint32_t tagged_message) {
  return decode_cycle_combat_focus(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticCycleCombatFocusTagSurface(uint32_t tagged_message) {
  const auto cycle_focus = decode_cycle_combat_focus(tagged_message);
  return cycle_focus
      ? cycle_focus->surface
      : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenCombatItemsTag(
    uint32_t tagged_message) {
  return decode_open_combat_items(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenCombatItemsTagSurface(uint32_t tagged_message) {
  const auto combat_items = decode_open_combat_items(tagged_message);
  return combat_items
      ? combat_items->surface
      : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticAutoCombatantTag(
    uint32_t tagged_message) {
  return decode_auto_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticAutoCombatantTagSurface(uint32_t tagged_message) {
  const auto auto_combatant = decode_auto_combatant(tagged_message);
  return auto_combatant
      ? auto_combatant->surface
      : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticShowCombatRangeTag(
    uint32_t tagged_message) {
  return decode_show_combat_range(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticShowCombatRangeTagSurface(uint32_t tagged_message) {
  const auto show_range = decode_show_combat_range(tagged_message);
  return show_range ? show_range->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticBandageCombatantTag(
    uint32_t tagged_message) {
  return decode_bandage_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticBandageCombatantTagSurface(uint32_t tagged_message) {
  const auto bandage = decode_bandage_combatant(tagged_message);
  return bandage ? bandage->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticUndoCombatantTag(
    uint32_t tagged_message) {
  return decode_undo_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticUndoCombatantTagSurface(uint32_t tagged_message) {
  const auto undo = decode_undo_combatant(tagged_message);
  return undo ? undo->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenCombatSpellbookTag(
    uint32_t tagged_message) {
  return decode_open_combat_spellbook(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenCombatSpellbookTagSurface(uint32_t tagged_message) {
  const auto spellbook = decode_open_combat_spellbook(tagged_message);
  return spellbook ? spellbook->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenCombatTargetingTag(
    uint32_t tagged_message) {
  return decode_open_combat_targeting(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenCombatTargetingTagSurface(uint32_t tagged_message) {
  const auto targeting = decode_open_combat_targeting(tagged_message);
  return targeting ? targeting->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticEscapeCombatTag(
    uint32_t tagged_message) {
  return decode_escape_combat(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticEscapeCombatTagSurface(uint32_t tagged_message) {
  const auto escape = decode_escape_combat(tagged_message);
  return escape ? escape->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticOpenCombatScrollCaseTag(
    uint32_t tagged_message) {
  return decode_open_combat_scroll_case(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenCombatScrollCaseTagSurface(uint32_t tagged_message) {
  const auto scroll_case = decode_open_combat_scroll_case(tagged_message);
  return scroll_case ? scroll_case->surface : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticCenterCombatCursorTag(
    uint32_t tagged_message) {
  return decode_center_combat_cursor(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticCenterCombatCursorTagSurface(uint32_t tagged_message) {
  const auto center_cursor = decode_center_combat_cursor(tagged_message);
  return center_cursor
      ? center_cursor->surface
      : kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzIsSemanticGameplayTag(
    uint32_t tagged_message) {
  return (decode_movement(tagged_message) ||
          decode_party_selection(tagged_message) ||
          decode_open_character_sheet(tagged_message) ||
          decode_open_inventory(tagged_message) ||
          decode_open_spellbook(tagged_message) ||
          decode_open_scroll_case(tagged_message) ||
          decode_open_save_game(tagged_message) ||
          decode_open_load_game(tagged_message) ||
          decode_rest_party(tagged_message) ||
          decode_guard_combatant(tagged_message) ||
          decode_finish_combatant(tagged_message) ||
          decode_delay_combatant(tagged_message) ||
          decode_center_active_combatant(tagged_message) ||
          decode_switch_weapon(tagged_message) ||
          decode_cycle_combat_focus(tagged_message) ||
          decode_open_combat_items(tagged_message) ||
          decode_auto_combatant(tagged_message) ||
          decode_show_combat_range(tagged_message) ||
          decode_bandage_combatant(tagged_message) ||
          decode_undo_combatant(tagged_message) ||
          decode_open_combat_spellbook(tagged_message) ||
          decode_open_combat_targeting(tagged_message) ||
          decode_escape_combat(tagged_message) ||
          decode_open_combat_scroll_case(tagged_message) ||
          decode_center_combat_cursor(tagged_message))
      ? 1
      : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticGameplayTagSurface(uint32_t tagged_message) {
  if (const auto movement = decode_movement(tagged_message)) {
    return movement->surface;
  }
  if (const auto selection = decode_party_selection(tagged_message)) {
    return selection->surface;
  }
  if (const auto character_sheet =
          decode_open_character_sheet(tagged_message)) {
    return character_sheet->surface;
  }
  if (const auto inventory = decode_open_inventory(tagged_message)) {
    return inventory->surface;
  }
  if (const auto spellbook = decode_open_spellbook(tagged_message)) {
    return spellbook->surface;
  }
  if (const auto scroll_case = decode_open_scroll_case(tagged_message)) {
    return scroll_case->surface;
  }
  if (const auto save_game = decode_open_save_game(tagged_message)) {
    return save_game->surface;
  }
  if (const auto load_game = decode_open_load_game(tagged_message)) {
    return load_game->surface;
  }
  if (const auto rest_party = decode_rest_party(tagged_message)) {
    return rest_party->surface;
  }
  if (const auto guard = decode_guard_combatant(tagged_message)) {
    return guard->surface;
  }
  if (const auto finish = decode_finish_combatant(tagged_message)) {
    return finish->surface;
  }
  if (const auto delay = decode_delay_combatant(tagged_message)) {
    return delay->surface;
  }
  if (const auto center = decode_center_active_combatant(tagged_message)) {
    return center->surface;
  }
  if (const auto switch_weapon = decode_switch_weapon(tagged_message)) {
    return switch_weapon->surface;
  }
  if (const auto cycle_focus = decode_cycle_combat_focus(tagged_message)) {
    return cycle_focus->surface;
  }
  if (const auto combat_items = decode_open_combat_items(tagged_message)) {
    return combat_items->surface;
  }
  if (const auto auto_combatant = decode_auto_combatant(tagged_message)) {
    return auto_combatant->surface;
  }
  if (const auto show_range = decode_show_combat_range(tagged_message)) {
    return show_range->surface;
  }
  if (const auto bandage = decode_bandage_combatant(tagged_message)) {
    return bandage->surface;
  }
  if (const auto undo = decode_undo_combatant(tagged_message)) {
    return undo->surface;
  }
  if (const auto spellbook = decode_open_combat_spellbook(tagged_message)) {
    return spellbook->surface;
  }
  if (const auto targeting = decode_open_combat_targeting(tagged_message)) {
    return targeting->surface;
  }
  if (const auto escape = decode_escape_combat(tagged_message)) {
    return escape->surface;
  }
  if (const auto scroll_case =
          decode_open_combat_scroll_case(tagged_message)) {
    return scroll_case->surface;
  }
  if (const auto center_cursor =
          decode_center_combat_cursor(tagged_message)) {
    return center_cursor->surface;
  }
  return kNoSemanticInputSurface;
}

extern "C" uint8_t RealmzConsumeSemanticMovementEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!classic_key_message || !authorized) {
    return 0;
  }
  const auto movement = decode_movement(tagged_message);
  if (!movement || (movement->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    if (snapshot.screen != screen) {
      return 0;
    }
    const auto message =
            realmz::presentation::legacy_key_message_for_movement(
            movement->command,
            {
                .screen = screen,
                .world_presentation = snapshot.world.presentation,
                .adaptive_eligible = true,
            });
    if (!message) {
      return 0;
    }
    *classic_key_message = *message;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticPartySelectionEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint8_t* party_member) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!party_member || !authorized) {
    return 0;
  }
  const auto selection = decode_party_selection(tagged_message);
  if (!selection || (selection->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    if ((snapshot.screen != screen) ||
        !snapshot.party.member(selection->member)) {
      return 0;
    }
    *party_member = selection->member;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticOpenCharacterSheetEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint8_t* party_member) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!party_member || !authorized) {
    return 0;
  }
  const auto character_sheet = decode_open_character_sheet(tagged_message);
  if (!character_sheet ||
      (character_sheet->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    const auto* member = snapshot.party.member(character_sheet->member);
    const realmz::presentation::RuntimeLegacyCommandContext context{
        .screen = screen,
        .world_presentation = snapshot.world.presentation,
        .adaptive_eligible = legacy.adaptive_eligible != 0,
    };
    if ((snapshot.screen != screen) ||
        !realmz::presentation::
            runtime_legacy_context_supports_open_character_sheet(context) ||
        !member || !member->selected ||
        (snapshot.party.selected_member != character_sheet->member)) {
      return 0;
    }
    *party_member = character_sheet->member;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticOpenInventoryEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!classic_key_message || !authorized) {
    return 0;
  }
  const auto inventory = decode_open_inventory(tagged_message);
  if (!inventory || (inventory->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    const auto* member = snapshot.party.member(inventory->member);
    if ((snapshot.screen != screen) || !member || !member->selected ||
        (snapshot.party.selected_member != inventory->member)) {
      return 0;
    }
    const auto message =
        realmz::presentation::legacy_key_message_for_open_inventory({
            .screen = screen,
            .world_presentation = snapshot.world.presentation,
            .adaptive_eligible = true,
        });
    if (!message) {
      return 0;
    }
    *classic_key_message = *message;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticOpenSpellbookEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!classic_key_message || !authorized) {
    return 0;
  }
  const auto spellbook = decode_open_spellbook(tagged_message);
  if (!spellbook || (spellbook->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    const auto* member = snapshot.party.member(spellbook->member);
    if ((snapshot.screen != screen) || !member || !member->selected ||
        !member->conscious || (member->spell_points.current <= 0) ||
        (snapshot.party.selected_member != spellbook->member)) {
      return 0;
    }
    const auto message =
        realmz::presentation::legacy_key_message_for_open_spellbook({
            .screen = screen,
            .world_presentation = snapshot.world.presentation,
            .adaptive_eligible = true,
        });
    if (!message) {
      return 0;
    }
    *classic_key_message = *message;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticOpenScrollCaseEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!classic_key_message || !authorized) {
    return 0;
  }
  const auto scroll_case = decode_open_scroll_case(tagged_message);
  if (!scroll_case || (scroll_case->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    const auto* member = snapshot.party.member(scroll_case->member);
    if ((snapshot.screen != screen) || !member || !member->selected ||
        !member->use_scroll_available ||
        (snapshot.party.selected_member != scroll_case->member)) {
      return 0;
    }
    const auto message =
        realmz::presentation::legacy_key_message_for_open_scroll_case({
            .screen = screen,
            .world_presentation = snapshot.world.presentation,
            .adaptive_eligible = true,
        });
    if (!message) {
      return 0;
    }
    *classic_key_message = *message;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticOpenSaveGameEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    int16_t* menu_id,
    int16_t* item_id) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!menu_id || !item_id || !authorized) {
    return 0;
  }
  const auto save_game = decode_open_save_game(tagged_message);
  if (!save_game || (save_game->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    if (snapshot.screen != screen) {
      return 0;
    }
    const auto command =
        realmz::presentation::legacy_menu_command_for_open_save_game({
            .screen = screen,
            .world_presentation = snapshot.world.presentation,
            .adaptive_eligible = true,
        });
    if (!command) {
      return 0;
    }
    *menu_id = command->menu_id;
    *item_id = command->item_id;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticOpenLoadGameEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    int16_t* menu_id,
    int16_t* item_id) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!menu_id || !item_id || !authorized) {
    return 0;
  }
  const auto load_game = decode_open_load_game(tagged_message);
  if (!load_game || (load_game->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    if (snapshot.screen != screen) {
      return 0;
    }
    const auto command =
        realmz::presentation::legacy_menu_command_for_open_load_game({
            .screen = screen,
            .world_presentation = snapshot.world.presentation,
            .adaptive_eligible = true,
        });
    if (!command) {
      return 0;
    }
    *menu_id = command->menu_id;
    *item_id = command->item_id;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticRestPartyEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!classic_key_message || !authorized) {
    return 0;
  }
  const auto rest_party = decode_rest_party(tagged_message);
  if (!rest_party || (rest_party->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    const realmz::presentation::RuntimeLegacyCommandContext context{
        .screen = screen,
        .world_presentation = snapshot.world.presentation,
        .adaptive_eligible = legacy.adaptive_eligible != 0,
        .in_camp = snapshot.world.in_camp,
    };
    if (snapshot.screen != screen) {
      return 0;
    }
    const auto message =
        realmz::presentation::legacy_key_message_for_rest_party(context);
    if (!message) {
      return 0;
    }
    *classic_key_message = *message;
    return 1;
  } catch (...) {
    return 0;
  }
}

extern "C" uint8_t RealmzConsumeSemanticGuardCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_guard_combatant(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_guard_combatant);
}

extern "C" uint8_t RealmzConsumeSemanticFinishCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_finish_combatant(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_finish_combatant);
}

extern "C" uint8_t RealmzConsumeSemanticDelayCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_delay_combatant(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_delay_combatant,
      has_full_movement);
}

extern "C" uint8_t RealmzConsumeSemanticCenterActiveCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_center_active_combatant(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_center_active_combatant);
}

extern "C" uint8_t RealmzConsumeSemanticSwitchWeaponEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_switch_weapon(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_switch_weapon);
}

extern "C" uint8_t RealmzConsumeSemanticCycleCombatFocusEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  const auto cycle_focus = decode_cycle_combat_focus(tagged_message);
  const auto direction = cycle_focus
      ? cycle_focus->direction
      : realmz::presentation::CombatFocusDirection::next;
  return consume_semantic_combatant_event(
      expected_surface,
      cycle_focus,
      classic_key_message,
      [direction](
          realmz::presentation::CombatantId combatant,
          const realmz::presentation::RuntimeLegacyCommandContext& context)
          noexcept {
        return realmz::presentation::
            legacy_key_message_for_cycle_combat_focus(
                combatant, direction, context);
      });
}

extern "C" uint8_t RealmzConsumeSemanticOpenCombatItemsEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  const auto combat_items = decode_open_combat_items(tagged_message);
  const auto member = combat_items
      ? combat_items->member
      : realmz::presentation::PartyMemberId{};
  return consume_semantic_combatant_event(
      expected_surface,
      combat_items,
      classic_key_message,
      [member](
          realmz::presentation::CombatantId combatant,
          const realmz::presentation::RuntimeLegacyCommandContext& context)
          noexcept {
        return realmz::presentation::legacy_key_message_for_open_combat_items(
            combatant, member, context);
      },
      nullptr,
      member);
}

extern "C" uint8_t RealmzConsumeSemanticAutoCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_auto_combatant(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_auto_combatant);
}

extern "C" uint8_t RealmzConsumeSemanticShowCombatRangeEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_show_combat_range(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_show_combat_range);
}

extern "C" uint8_t RealmzConsumeSemanticBandageCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_bandage_combatant(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_bandage_combatant,
      is_bandage_available);
}

extern "C" uint8_t RealmzConsumeSemanticUndoCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_undo_combatant(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_undo_combatant,
      is_undo_available);
}

extern "C" uint8_t RealmzConsumeSemanticOpenCombatSpellbookEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_open_combat_spellbook(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_open_combat_spellbook,
      is_combat_spellbook_available);
}

extern "C" uint8_t RealmzConsumeSemanticOpenCombatTargetingEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_open_combat_targeting(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_open_combat_targeting,
      is_combat_targeting_available);
}

extern "C" uint8_t RealmzConsumeSemanticEscapeCombatEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_escape_combat(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_escape_combat);
}

extern "C" uint8_t RealmzConsumeSemanticOpenCombatScrollCaseEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message) {
  return consume_semantic_combatant_event(
      expected_surface,
      decode_open_combat_scroll_case(tagged_message),
      classic_key_message,
      realmz::presentation::legacy_key_message_for_open_combat_scroll_case,
      is_combat_scroll_available);
}

extern "C" uint8_t RealmzConsumeSemanticCenterCombatCursorEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message,
    uint8_t* absolute_x,
    uint8_t* absolute_y) {
  const bool authorized = authorize_completed_scope(expected_surface);
  if (!classic_key_message || !absolute_x || !absolute_y || !authorized) {
    return 0;
  }
  const auto center_cursor = decode_center_combat_cursor(tagged_message);
  if (!center_cursor || (center_cursor->surface != expected_surface)) {
    return 0;
  }

  const auto legacy = RealmzCaptureLegacyPresentationContext();
  const auto screen = realmz::presentation::screen_context_from_legacy(legacy);
  if (!legacy.adaptive_eligible ||
      (screen != screen_for_surface(expected_surface))) {
    return 0;
  }

  try {
    const auto snapshot =
        realmz::presentation::LegacyGameSnapshotSource().capture();
    if ((snapshot.screen != screen) ||
        !is_live_active_party_combatant(
            snapshot, center_cursor->combatant) ||
        !has_sane_current_battlefield_viewport(
            snapshot, center_cursor->combatant)) {
      return 0;
    }
    const auto message = realmz::presentation::
        legacy_key_message_for_center_combat_cursor(
            center_cursor->combatant,
            center_cursor->cell,
            {
                .screen = screen,
                .world_presentation = snapshot.world.presentation,
                .adaptive_eligible = true,
            });
    if (!message) {
      return 0;
    }
    *classic_key_message = *message;
    *absolute_x = center_cursor->cell.x;
    *absolute_y = center_cursor->cell.y;
    return 1;
  } catch (...) {
    return 0;
  }
}
