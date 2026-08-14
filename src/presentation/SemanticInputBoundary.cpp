#include "SemanticInputBoundary.h"

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

bool is_gameplay_surface(RealmzSemanticInputSurface surface) noexcept {
  return (surface == REALMZ_SEMANTIC_INPUT_EXPLORATION) ||
      (surface == REALMZ_SEMANTIC_INPUT_DUNGEON);
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

bool authorize_completed_scope(
    RealmzSemanticInputSurface expected_surface) noexcept {
  const bool completed_expected_scope =
      (scope_depth == 0) &&
      (completed_surface == expected_surface) &&
      is_gameplay_surface(expected_surface);
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
    case REALMZ_SEMANTIC_INPUT_NONE:
    default:
      return ScreenContext::title;
  }
}

} // namespace

namespace realmz::presentation {

uint32_t semantic_movement_tag(
    MovementCommand command,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_gameplay_surface(surface) ||
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
  if (!is_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticPartySelectionSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_open_inventory_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenInventorySignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

uint32_t semantic_open_spellbook_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept {
  if (!is_gameplay_surface(surface)) {
    return 0;
  }
  return kSemanticOpenSpellbookSignature |
      (static_cast<uint32_t>(surface) << 8U) |
      static_cast<uint32_t>(member);
}

} // namespace realmz::presentation

extern "C" void RealmzBeginSemanticInputSurface(
    RealmzSemanticInputSurface surface) {
  completed_surface = REALMZ_SEMANTIC_INPUT_NONE;
  if (scope_depth == 0) {
    scope_surface = is_gameplay_surface(surface)
        ? surface
        : REALMZ_SEMANTIC_INPUT_NONE;
    scope_invalid = !is_gameplay_surface(surface);
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

extern "C" uint8_t RealmzIsSemanticGameplayTag(
    uint32_t tagged_message) {
  return (decode_movement(tagged_message) ||
          decode_party_selection(tagged_message) ||
          decode_open_inventory(tagged_message) ||
          decode_open_spellbook(tagged_message))
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
