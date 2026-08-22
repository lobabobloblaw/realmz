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

// Character-sheet tags retain the selected member and originating world
// surface until the guarded top-level loop can revalidate both. The preserved
// outer loop owns the eventual second-click-equivalent buttonchoice handoff.
uint8_t RealmzIsSemanticOpenCharacterSheetTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenCharacterSheetTagSurface(
    uint32_t tagged_message);

// Selected-item drilldown tags carry only the exact selected member and the
// originating world surface. The preserved outer loop owns the real Show Item
// control and every subsequent Classic item interaction.
uint8_t RealmzIsSemanticSelectedItemDrilldownTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticSelectedItemDrilldownTagSurface(
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

// Non-combat scroll-case tags carry the selected member explicitly and retain
// their originating world surface. Late validation prevents queued commands
// from retargeting or outliving the selected member's live eligibility.
uint8_t RealmzIsSemanticOpenScrollCaseTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenScrollCaseTagSurface(
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

// Rest tags carry only their originating world surface. The guarded consumer
// rechecks a fresh adaptive snapshot and camp state before producing Classic's
// lowercase-r key record.
uint8_t RealmzIsSemanticRestPartyTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticRestPartyTagSurface(
    uint32_t tagged_message);

// Set-camp-state tags retain both their originating world surface and the
// absolute desired state. A strict boolean payload prevents malformed values
// from entering Classic's relative lowercase-c route.
uint8_t RealmzIsSemanticSetCampStateTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticSetCampStateTagSurface(
    uint32_t tagged_message);
uint8_t RealmzSemanticSetCampStateTagDesiredInCamp(
    uint32_t tagged_message);

// Set-search-state tags retain the originating world surface plus a strict
// absolute boolean. Search has no Classic key route, so successful delivery is
// staged for the preserved outer loop's existing Search-control path.
uint8_t RealmzIsSemanticSetSearchStateTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticSetSearchStateTagSurface(
    uint32_t tagged_message);
uint8_t RealmzSemanticSetSearchStateTagDesiredSearching(
    uint32_t tagged_message);

// Use-Torch tags carry the originating world surface plus the bounded party
// member/inventory slot freshness locator selected by Classic's first exact
// usable +805 scan. No charge, light strength, key, or pointer is encoded.
uint8_t RealmzIsSemanticUseTorchTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticUseTorchTagSurface(
    uint32_t tagged_message);
uint8_t RealmzSemanticUseTorchTagMember(uint32_t tagged_message);
uint8_t RealmzSemanticUseTorchTagSlot(uint32_t tagged_message);

// The contextual Overview tag uses one shared collision-free signature for
// both Classic controls. Payload 0 is Area Search; payloads 0x80..0x85 are
// Make Scroll bound to party members 0..5. Every other payload is malformed.
uint8_t RealmzIsSemanticContextualOverviewTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticContextualOverviewTagSurface(
    uint32_t tagged_message);
uint8_t RealmzSemanticContextualOverviewTagIsAreaSearch(
    uint32_t tagged_message);

// Contextual-world-entry tags retain one explicit semantic mode. Wire payloads
// 0, 1, and 2 mean Shop, Temple, and Encounter respectively; unavailable and
// every other payload are malformed. The guarded world consumer rechecks the
// exact live mode before yielding Classic's lowercase "g" or "e" key record.
uint8_t RealmzIsSemanticContextualWorldEntryTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticContextualWorldEntryTagSurface(
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

// Bandage tags carry only the acting combatant. Classic remains authoritative
// for target selection and all resulting combat-state mutation.
uint8_t RealmzIsSemanticBandageCombatantTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticBandageCombatantTagSurface(
    uint32_t tagged_message);

// Undo tags carry only the acting combatant. Classic remains authoritative
// for its condition checks and every resulting combat-state mutation.
uint8_t RealmzIsSemanticUndoCombatantTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticUndoCombatantTagSurface(
    uint32_t tagged_message);

// Combat spellbook tags carry only the acting combatant. Classic remains
// authoritative for spell selection, targeting, costs/refunds, RNG, and every
// resulting combat-state mutation.
uint8_t RealmzIsSemanticOpenCombatSpellbookTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenCombatSpellbookTagSurface(
    uint32_t tagged_message);

// Combat-targeting tags carry only the acting combatant. Classic remains
// authoritative for live equipment resolution, target selection, costs, RNG,
// and every resulting combat-state mutation.
uint8_t RealmzIsSemanticOpenCombatTargetingTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenCombatTargetingTagSurface(
    uint32_t tagged_message);

// Escape tags carry only the acting combatant. Classic remains authoritative
// for range and condition checks, warning/confirmation modals, mutation, RNG,
// and every subsequent turn or combat-exit effect.
uint8_t RealmzIsSemanticEscapeCombatTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticEscapeCombatTagSurface(
    uint32_t tagged_message);

// Combat scroll-case tags carry only the acting combatant. Classic remains
// authoritative for modal selection, scroll consumption, targeting, costs,
// RNG, and every subsequent turn effect.
uint8_t RealmzIsSemanticOpenCombatScrollCaseTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticOpenCombatScrollCaseTagSurface(
    uint32_t tagged_message);

// Center-on-cursor tags carry the acting combatant plus an absolute field
// cell. They are valid only on the combat surface and never encode the cell in
// an EventRecord mouse position.
uint8_t RealmzIsSemanticCenterCombatCursorTag(uint32_t tagged_message);
RealmzSemanticInputSurface RealmzSemanticCenterCombatCursorTagSurface(
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

// Revalidates the originating world surface and selected member after the
// semantic scope ends, then returns that stable member ID. EventManager stages
// it for a one-shot take by the preserved world loop; no modal runs here.
uint8_t RealmzConsumeSemanticOpenCharacterSheetEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint8_t* party_member);

// Revalidates the exact adaptive world presentation and selected member after
// the semantic scope ends, then returns that member for a neutral one-shot
// app1Evt handoff to Classic's existing Show Item buttonchoice path.
uint8_t RealmzConsumeSemanticSelectedItemDrilldownEvent(
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

// Revalidates the originating gameplay surface, selected member, and fresh
// non-combat Scroll capability, then returns the surface-specific preserved
// Classic key record. Classic owns the chooser and all scroll-slot behavior.
uint8_t RealmzConsumeSemanticOpenScrollCaseEvent(
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

// Revalidates the originating world surface, exact world presentation, and
// fresh camp state, then returns Classic's lowercase "r" key record. Classic
// remains authoritative for the complete rest quantum and every mutation.
uint8_t RealmzConsumeSemanticRestPartyEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the originating world surface, exact presentation, fresh camp
// state, and absolute desired-state mismatch before returning Classic's
// lowercase "c" record. Classic remains authoritative for whether camping is
// permitted and for all feedback, music, time, and state mutations.
uint8_t RealmzConsumeSemanticSetCampStateEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the exact live world context and absolute Search-state mismatch,
// then returns only a strict desired boolean for EventManager's one-shot
// app1Evt sideband. It never creates Classic key or pointer input.
uint8_t RealmzConsumeSemanticSetSearchStateEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint8_t* desired_searching);

// Revalidates the exact live world context and first usable Torch source, then
// returns only its locator for EventManager's one-shot neutral app1Evt
// sideband. Classic's real Torch control remains the sole mutation path.
uint8_t RealmzConsumeSemanticUseTorchEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint8_t* member,
    uint8_t* slot);

// Revalidates the exact live world presentation and camp state. Make Scroll
// additionally requires the encoded member to remain selected and eligible.
// On success this returns Classic's exact lowercase "a" or "k" key record.
uint8_t RealmzConsumeSemanticContextualOverviewEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the exact adaptive world presentation, non-camp state, and live
// contextual entry mode. Classic retains all shop, temple, encounter, modal,
// RNG, and mutation ownership after the exact lowercase key record is returned.
uint8_t RealmzConsumeSemanticContextualWorldEntryEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

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

// Revalidates the live acting party combatant and Classic's canundo Bandage
// gate, then returns the preserved lowercase "b" key record. Classic owns the
// target picker, selection state, feedback, mutations, and turn handling.
uint8_t RealmzConsumeSemanticBandageCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant and the distinct Undo capability,
// then returns the preserved lowercase "u" key record. Classic owns every
// status check, position/field mutation, redraw, queue update, and turn effect.
uint8_t RealmzConsumeSemanticUndoCombatantEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant and the distinct combat-cast
// capability, then returns the preserved lowercase "s" key record. Classic
// owns the complete chooser, targeting flow, and all mutations after handoff.
uint8_t RealmzConsumeSemanticOpenCombatSpellbookEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant and the read-only Target
// capability, then returns the preserved lowercase "t" key record. Classic
// owns equipment/quiver resolution, targeting, costs, RNG, and every mutation.
uint8_t RealmzConsumeSemanticOpenCombatTargetingEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates only the live acting party combatant, then returns the preserved
// lowercase "e" key record. Classic owns all Escape-specific eligibility,
// feedback, confirmation, mutation, and subsequent turn handling.
uint8_t RealmzConsumeSemanticEscapeCombatEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live acting party combatant and the read-only combat Scroll
// capability, then returns the preserved lowercase "l" key record. Classic
// owns the chooser and every selection, consumption, targeting, RNG, cost, and
// turn effect after handoff.
uint8_t RealmzConsumeSemanticOpenCombatScrollCaseEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message);

// Revalidates the live party actor and a sane current battlefield viewport,
// then returns the preserved lowercase "m" key and the queued absolute cell.
// The cell need not remain inside the current viewport after a camera move.
uint8_t RealmzConsumeSemanticCenterCombatCursorEvent(
    RealmzSemanticInputSurface expected_surface,
    uint32_t tagged_message,
    uint32_t* classic_key_message,
    uint8_t* absolute_x,
    uint8_t* absolute_y);

#ifdef __cplusplus
} // extern "C"

#include "GameSnapshot.hpp"

namespace realmz::presentation {

enum class MovementCommand;
enum class CombatFocusDirection;
struct CombatFieldCell;
struct ContextualOverviewAction;
struct ContextualWorldEntryAction;

[[nodiscard]] uint32_t semantic_movement_tag(
    MovementCommand command,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_party_selection_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_character_sheet_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_selected_item_drilldown_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_inventory_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_spellbook_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_scroll_case_tag(
    PartyMemberId member,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_save_game_tag(
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_load_game_tag(
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_rest_party_tag(
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_set_camp_state_tag(
    bool desired_in_camp,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_set_search_state_tag(
    bool desired_searching,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_use_torch_tag(
    const TorchSource& source,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_contextual_overview_tag(
    const ContextualOverviewAction& action,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_contextual_world_entry_tag(
    const ContextualWorldEntryAction& action,
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

[[nodiscard]] uint32_t semantic_bandage_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_undo_combatant_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_combat_spellbook_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_combat_targeting_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_escape_combat_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_open_combat_scroll_case_tag(
    CombatantId combatant,
    RealmzSemanticInputSurface surface) noexcept;

[[nodiscard]] uint32_t semantic_center_combat_cursor_tag(
    CombatantId combatant,
    CombatFieldCell cell,
    RealmzSemanticInputSurface surface) noexcept;

} // namespace realmz::presentation
#endif

#endif
