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
constexpr uint32_t kSemanticOpenInventorySignature = 0x52490000U;
constexpr uint32_t kSemanticOpenInventoryMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenInventorySurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenInventoryMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenSpellbookSignature = 0x52500000U;
constexpr uint32_t kSemanticOpenSpellbookMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenSpellbookSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenSpellbookMemberMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenSaveGameSignature = 0x52560000U;
constexpr uint32_t kSemanticOpenSaveGameMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenSaveGameSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenSaveGameReservedMask = 0x000000FFU;
constexpr uint32_t kSemanticOpenLoadGameSignature = 0x524C0000U;
constexpr uint32_t kSemanticOpenLoadGameMask = 0xFFFF0000U;
constexpr uint32_t kSemanticOpenLoadGameSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticOpenLoadGameReservedMask = 0x000000FFU;
constexpr uint32_t kSemanticDelayCombatantSignature = 0x52440000U;
constexpr uint32_t kSemanticDelayCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticDelayCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticDelayCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticFinishCombatantSignature = 0x52460000U;
constexpr uint32_t kSemanticFinishCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticFinishCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticFinishCombatantIdMask = 0x000000FFU;
constexpr uint32_t kSemanticGuardCombatantSignature = 0x52470000U;
constexpr uint32_t kSemanticGuardCombatantMask = 0xFFFF0000U;
constexpr uint32_t kSemanticGuardCombatantSurfaceMask = 0x0000FF00U;
constexpr uint32_t kSemanticGuardCombatantIdMask = 0x000000FFU;
uint32_t scope_depth = 0;
bool scope_invalid = false;
RealmzSemanticInputSurface scope_surface = REALMZ_SEMANTIC_INPUT_NONE;
RealmzSemanticInputSurface completed_surface = REALMZ_SEMANTIC_INPUT_NONE;

struct DecodedMovement {
  realmz::presentation::MovementCommand command;
  RealmzSemanticInputSurface surface;
};

struct DecodedPartySelection {
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

struct DecodedOpenSaveGame {
  RealmzSemanticInputSurface surface;
};

struct DecodedOpenLoadGame {
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

bool authorize_completed_scope(
    RealmzSemanticInputSurface expected_surface) noexcept {
  const bool completed_expected_scope =
      (scope_depth == 0) &&
      (completed_surface == expected_surface) &&
      is_semantic_input_surface(expected_surface);
  // Delivery authorization is single-use regardless of whether the payload or
  // processing-time context validates. A later event needs a fresh scope.
  completed_surface = REALMZ_SEMANTIC_INPUT_NONE;
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

using CombatantMessageMapper = std::optional<uint32_t> (*)(
    realmz::presentation::CombatantId,
    const realmz::presentation::RuntimeLegacyCommandContext&) noexcept;
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

template <typename DecodedCombatant>
uint8_t consume_semantic_combatant_event(
    RealmzSemanticInputSurface expected_surface,
    const std::optional<DecodedCombatant>& decoded,
    uint32_t* classic_key_message,
    CombatantMessageMapper message_mapper,
    CombatantSnapshotValidator snapshot_validator = nullptr) {
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
    if ((snapshot.screen != screen) || !snapshot.combat ||
        !snapshot.combat->active ||
        (snapshot.combat->acting_combatant != decoded->combatant)) {
      return 0;
    }
    const auto combatant = std::ranges::find(
        snapshot.combat->combatants,
        decoded->combatant,
        &realmz::presentation::CombatantView::id);
    if ((combatant == snapshot.combat->combatants.end()) ||
        (combatant->kind != realmz::presentation::CombatantKind::party_member) ||
        !combatant->active || !combatant->targetable ||
        (combatant->stamina.current <= 0)) {
      return 0;
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

} // namespace realmz::presentation

extern "C" void RealmzBeginSemanticInputSurface(
    RealmzSemanticInputSurface surface) {
  completed_surface = REALMZ_SEMANTIC_INPUT_NONE;
  if (scope_depth == 0) {
    scope_surface = is_semantic_input_surface(surface)
        ? surface
        : REALMZ_SEMANTIC_INPUT_NONE;
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
    completed_surface = REALMZ_SEMANTIC_INPUT_NONE;
    scope_surface = REALMZ_SEMANTIC_INPUT_NONE;
    return;
  }
  --scope_depth;
  if (scope_depth == 0) {
    completed_surface = scope_invalid
        ? REALMZ_SEMANTIC_INPUT_NONE
        : scope_surface;
    scope_surface = REALMZ_SEMANTIC_INPUT_NONE;
    scope_invalid = false;
  }
}

extern "C" RealmzSemanticInputSurface
RealmzCurrentSemanticInputSurface(void) {
  return ((scope_depth == 1) && !scope_invalid)
      ? scope_surface
      : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" void RealmzInvalidateSemanticInputBoundary(void) {
  scope_depth = 0;
  scope_invalid = false;
  scope_surface = REALMZ_SEMANTIC_INPUT_NONE;
  completed_surface = REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticMovementTag(
    uint32_t tagged_message) {
  return decode_movement(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticMovementTagSurface(uint32_t tagged_message) {
  const auto movement = decode_movement(tagged_message);
  return movement ? movement->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticPartySelectionTag(
    uint32_t tagged_message) {
  return decode_party_selection(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticPartySelectionTagSurface(uint32_t tagged_message) {
  const auto selection = decode_party_selection(tagged_message);
  return selection ? selection->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticOpenInventoryTag(
    uint32_t tagged_message) {
  return decode_open_inventory(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenInventoryTagSurface(uint32_t tagged_message) {
  const auto inventory = decode_open_inventory(tagged_message);
  return inventory ? inventory->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticOpenSpellbookTag(
    uint32_t tagged_message) {
  return decode_open_spellbook(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenSpellbookTagSurface(uint32_t tagged_message) {
  const auto spellbook = decode_open_spellbook(tagged_message);
  return spellbook ? spellbook->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticOpenSaveGameTag(
    uint32_t tagged_message) {
  return decode_open_save_game(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenSaveGameTagSurface(uint32_t tagged_message) {
  const auto save_game = decode_open_save_game(tagged_message);
  return save_game ? save_game->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticOpenLoadGameTag(
    uint32_t tagged_message) {
  return decode_open_load_game(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticOpenLoadGameTagSurface(uint32_t tagged_message) {
  const auto load_game = decode_open_load_game(tagged_message);
  return load_game ? load_game->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticGuardCombatantTag(
    uint32_t tagged_message) {
  return decode_guard_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticGuardCombatantTagSurface(uint32_t tagged_message) {
  const auto guard = decode_guard_combatant(tagged_message);
  return guard ? guard->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticFinishCombatantTag(
    uint32_t tagged_message) {
  return decode_finish_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticFinishCombatantTagSurface(uint32_t tagged_message) {
  const auto finish = decode_finish_combatant(tagged_message);
  return finish ? finish->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticDelayCombatantTag(
    uint32_t tagged_message) {
  return decode_delay_combatant(tagged_message).has_value() ? 1 : 0;
}

extern "C" RealmzSemanticInputSurface
RealmzSemanticDelayCombatantTagSurface(uint32_t tagged_message) {
  const auto delay = decode_delay_combatant(tagged_message);
  return delay ? delay->surface : REALMZ_SEMANTIC_INPUT_NONE;
}

extern "C" uint8_t RealmzIsSemanticGameplayTag(
    uint32_t tagged_message) {
  return (decode_movement(tagged_message) ||
          decode_party_selection(tagged_message) ||
          decode_open_inventory(tagged_message) ||
          decode_open_spellbook(tagged_message) ||
          decode_open_save_game(tagged_message) ||
          decode_open_load_game(tagged_message) ||
          decode_guard_combatant(tagged_message) ||
          decode_finish_combatant(tagged_message) ||
          decode_delay_combatant(tagged_message))
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
  if (const auto inventory = decode_open_inventory(tagged_message)) {
    return inventory->surface;
  }
  if (const auto spellbook = decode_open_spellbook(tagged_message)) {
    return spellbook->surface;
  }
  if (const auto save_game = decode_open_save_game(tagged_message)) {
    return save_game->surface;
  }
  if (const auto load_game = decode_open_load_game(tagged_message)) {
    return load_game->surface;
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
  return REALMZ_SEMANTIC_INPUT_NONE;
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
