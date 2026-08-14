#ifndef REALMZ_PRESENTATION_SEMANTIC_INPUT_BOUNDARY_H
#define REALMZ_PRESENTATION_SEMANTIC_INPUT_BOUNDARY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t RealmzSemanticInputSurface;
enum {
  REALMZ_SEMANTIC_INPUT_NONE = 0,
  REALMZ_SEMANTIC_INPUT_EXPLORATION = 1,
  REALMZ_SEMANTIC_INPUT_DUNGEON = 2,
  REALMZ_SEMANTIC_INPUT_COMBAT = 3
};

// These functions bracket only the three top-level gameplay GetNextEvent calls.
// Nested Classic loops, FlushEvents, and Button/StillDown polling intentionally
// run outside a semantic input surface.
void RealmzBeginSemanticInputSurface(RealmzSemanticInputSurface surface);
void RealmzEndSemanticInputSurface(void);
RealmzSemanticInputSurface RealmzCurrentSemanticInputSurface(void);
void RealmzInvalidateSemanticInputBoundary(void);

// Returns nonzero only for a well-formed movement tag produced by the
// presentation boundary. The movement wire encoding is stable for replay and
// compatibility with existing producers.
uint8_t RealmzIsSemanticMovementTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticMovementTagSurface(
    uint32_t tagged_message);

// Party selection uses a distinct tag signature, so it cannot be interpreted
// as a movement command by an older or movement-only consumer.
uint8_t RealmzIsSemanticPartySelectionTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticPartySelectionTagSurface(
    uint32_t tagged_message);

// Open-inventory tags carry the selected member explicitly. This keeps a
// queued action from silently retargeting if selection changes before the
// guarded top-level loop receives it.
uint8_t RealmzIsSemanticOpenInventoryTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenInventoryTagSurface(
    uint32_t tagged_message);

// Open-spellbook tags carry the intended caster. Late validation rejects a
// queued command if selection, consciousness, or spell points change before
// the guarded gameplay loop receives it.
uint8_t RealmzIsSemanticOpenSpellbookTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenSpellbookTagSurface(
    uint32_t tagged_message);

// Open-save-game tags request only the preserved slot chooser. They do not
// identify a slot and cannot write save data at this boundary.
uint8_t RealmzIsSemanticOpenSaveGameTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenSaveGameTagSurface(
    uint32_t tagged_message);

// Open-load-game tags request only the preserved in-game chooser. They do not
// identify a slot and cannot replace engine state at this boundary.
uint8_t RealmzIsSemanticOpenLoadGameTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenLoadGameTagSurface(
    uint32_t tagged_message);

// Guard tags carry the acting combatant explicitly and are valid only on the
// combat surface. This prevents a queued command from applying to a later turn.
uint8_t RealmzIsSemanticGuardCombatantTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticGuardCombatantTagSurface(
    uint32_t tagged_message);

// Finish tags carry the acting combatant explicitly and are valid only on the
// combat surface. This prevents a queued command from ending a later turn.
uint8_t RealmzIsSemanticFinishCombatantTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticFinishCombatantTagSurface(
    uint32_t tagged_message);

// Delay tags carry the acting combatant explicitly and are valid only on the
// combat surface. Processing-time movement validation prevents a queued Delay
// from rotating a later or already-moved turn.
uint8_t RealmzIsSemanticDelayCombatantTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticDelayCombatantTagSurface(
    uint32_t tagged_message);

// Center-active tags carry the acting combatant explicitly and are valid only
// on the combat surface. This prevents a queued camera command from following
// a later turn's actor.
uint8_t RealmzIsSemanticCenterActiveCombatantTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticCenterActiveCombatantTagSurface(
    uint32_t tagged_message);

// Switch-weapon tags carry the acting combatant explicitly and are valid only
// on the combat surface. The desired set is intentionally left to the live
// Classic relative-toggle flow.
uint8_t RealmzIsSemanticSwitchWeaponTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticSwitchWeaponTagSurface(
    uint32_t tagged_message);

// Cycle-focus tags carry both the acting combatant and the relative direction.
// The eventual destination remains owned by the live Classic combat view.
uint8_t RealmzIsSemanticCycleCombatFocusTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticCycleCombatFocusTagSurface(
    uint32_t tagged_message);

// Combat-Items tags carry the acting combatant and selected party member as
// independent stable IDs. Both must still match the fresh combat snapshot
// before the preserved Classic modal can be requested.
uint8_t RealmzIsSemanticOpenCombatItemsTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenCombatItemsTagSurface(
    uint32_t tagged_message);

// Auto-combatant tags carry only the acting combatant. The preserved Classic
// flow remains authoritative for every automated decision and combat effect.
uint8_t RealmzIsSemanticAutoCombatantTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticAutoCombatantTagSurface(
    uint32_t tagged_message);

// Combat-range tags carry only the acting combatant. Classic remains
// authoritative for drawing, dismissal input, and restoring the combat view.
uint8_t RealmzIsSemanticShowCombatRangeTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticShowCombatRangeTagSurface(
    uint32_t tagged_message);

// EventManager uses these generic predicates to keep every tagged gameplay
// command out of nested Classic loops without interpreting its payload.
uint8_t RealmzIsSemanticGameplayTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticGameplayTagSurface(
    uint32_t tagged_message);

// Receives a tagged semantic movement event immediately after a guarded
// top-level loop has ended its input scope. It revalidates the current
// presentation context and translates the command into the exact legacy
// keyDown record consumed by the preserved switch.
uint8_t RealmzConsumeSemanticMovementEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Receives a tagged party selection immediately after a guarded top-level loop
// has ended its input scope. It revalidates the live presentation context and
// detached party snapshot, then returns the stable member ID. The caller owns
// the narrow legacy mutation adapter.
uint8_t RealmzConsumeSemanticPartySelectionEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint8_t* party_member);

// Revalidates the originating gameplay surface and selected member, then
// returns the preserved Classic "i" key record to the top-level loop.
uint8_t RealmzConsumeSemanticOpenInventoryEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the originating gameplay surface and intended caster, then
// returns the preserved Classic "s" key record to the top-level loop.
uint8_t RealmzConsumeSemanticOpenSpellbookEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the originating gameplay surface, then returns the preserved
// Game-menu selection that opens the Classic save-slot chooser.
uint8_t RealmzConsumeSemanticOpenSaveGameEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    int16_t* menu_id,
    int16_t* item_id);

// Revalidates the originating gameplay surface, then returns the preserved
// Game-menu selection that opens the Classic load/revert chooser.
uint8_t RealmzConsumeSemanticOpenLoadGameEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    int16_t* menu_id,
    int16_t* item_id);

// Revalidates the live acting party combatant, then returns the preserved
// Classic "g" key record to the top-level combat loop.
uint8_t RealmzConsumeSemanticGuardCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant, then returns the preserved
// Classic "f" key record to the top-level combat loop.
uint8_t RealmzConsumeSemanticFinishCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant and full-movement prerequisite,
// then returns the preserved Classic "d" key record to the combat loop.
uint8_t RealmzConsumeSemanticDelayCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant, then returns the preserved
// Classic "c" key record that centers the combat view through the original
// combat loop.
uint8_t RealmzConsumeSemanticCenterActiveCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant, then returns the preserved
// Classic lowercase "w" key record. The original combat flow owns the relative
// weapon-set toggle, feedback, and any subsequent turn handling.
uint8_t RealmzConsumeSemanticSwitchWeaponEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant, then returns the preserved
// Classic lowercase "p" or "n" key record. Classic remains authoritative for
// resolving the requested relative focus destination.
uint8_t RealmzConsumeSemanticCycleCombatFocusEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant and selected party member, then
// returns the preserved Classic lowercase "i" key record. Classic remains
// authoritative for the complete modal and every item or turn effect.
uint8_t RealmzConsumeSemanticOpenCombatItemsEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant, then returns the preserved
// Classic lowercase "a" key record. Classic owns automation, RNG, mutations,
// and all subsequent turn handling.
uint8_t RealmzConsumeSemanticAutoCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant, then returns the preserved
// Classic lowercase "r" key record. Classic owns the raw modal wait, drawing,
// dismissal, recentering, and all subsequent event handling.
uint8_t RealmzConsumeSemanticShowCombatRangeEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

#ifdef __cplusplus
} // extern "C"

#include "GameSnapshot.hpp"

namespace realmz::presentation {

enum class MovementCommand;
enum class CombatFocusDirection;

[[nodiscard]] uint32_t semantic_movement_tag(
    MovementCommand command,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_party_selection_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_inventory_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_spellbook_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_save_game_tag(
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_load_game_tag(
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_guard_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_finish_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_delay_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_center_active_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_switch_weapon_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_cycle_combat_focus_tag(
    CombatantId combatant,
    CombatFocusDirection direction,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_combat_items_tag(
    CombatantId combatant,
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_auto_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_show_combat_range_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

} // namespace realmz::presentation
#endif

#endif
