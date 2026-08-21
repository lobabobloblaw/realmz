#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "LegacyCommandBridge.hpp"

namespace realmz::presentation {

// The production bridge captures this value immediately before queueing a
// semantic command. It deliberately contains no legacy pointers or mutable
// save structures.
struct RuntimeLegacyCommandContext {
  ScreenContext screen = ScreenContext::title;
  WorldPresentation world_presentation = WorldPresentation::none;
  bool adaptive_eligible = false;

  bool operator==(const RuntimeLegacyCommandContext&) const = default;
};

struct RuntimeLegacyMenuCommand {
  int16_t menu_id = 0;
  int16_t item_id = 0;

  bool operator==(const RuntimeLegacyMenuCommand&) const = default;
};

using RuntimeLegacyContextProvider =
    std::function<RuntimeLegacyCommandContext()>;
using RuntimeLegacyKeySink = std::function<bool(uint32_t)>;
using RuntimeLegacyMovementSink = std::function<bool(
    MovementCommand,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyPartySelectionSink = std::function<bool(
    PartyMemberId,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenInventorySink = std::function<bool(
    PartyMemberId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenSpellbookSink = std::function<bool(
    PartyMemberId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenSaveGameSink = std::function<bool(
    RuntimeLegacyMenuCommand,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenLoadGameSink = std::function<bool(
    RuntimeLegacyMenuCommand,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyGuardCombatantSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyFinishCombatantSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyDelayCombatantSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyCenterActiveCombatantSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacySwitchWeaponSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyCycleCombatFocusSink = std::function<bool(
    CombatantId,
    CombatFocusDirection,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenCombatItemsSink = std::function<bool(
    CombatantId,
    PartyMemberId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyAutoCombatantSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyShowCombatRangeSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyBandageCombatantSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyUndoCombatantSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenCombatSpellbookSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenCombatTargetingSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyEscapeCombatSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenCombatScrollCaseSink = std::function<bool(
    CombatantId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;

// Combat sinks are named because several callable signatures are intentionally
// identical even though their commands are not interchangeable. The focus
// sink additionally carries its typed direction. A disengaged field leaves
// that action unsupported. An engaged field containing an empty std::function
// registers the action and makes dispatch fail closed, preserving the behavior
// of the positional compatibility constructors below.
struct RuntimeLegacyCombatActionSinks {
  std::optional<RuntimeLegacyGuardCombatantSink> guard_combatant;
  std::optional<RuntimeLegacyFinishCombatantSink> finish_combatant;
  std::optional<RuntimeLegacyDelayCombatantSink> delay_combatant;
  std::optional<RuntimeLegacyCenterActiveCombatantSink>
      center_active_combatant;
  std::optional<RuntimeLegacySwitchWeaponSink> switch_weapon;
  std::optional<RuntimeLegacyCycleCombatFocusSink> cycle_combat_focus;
  std::optional<RuntimeLegacyOpenCombatItemsSink> open_combat_items;
  std::optional<RuntimeLegacyAutoCombatantSink> auto_combatant;
  std::optional<RuntimeLegacyShowCombatRangeSink> show_combat_range;
  std::optional<RuntimeLegacyBandageCombatantSink> bandage_combatant;
  std::optional<RuntimeLegacyUndoCombatantSink> undo_combatant;
  std::optional<RuntimeLegacyOpenCombatSpellbookSink> open_combat_spellbook;
  std::optional<RuntimeLegacyOpenCombatTargetingSink> open_combat_targeting;
  std::optional<RuntimeLegacyEscapeCombatSink> escape_combat;
  std::optional<RuntimeLegacyOpenCombatScrollCaseSink>
      open_combat_scroll_case;
};

// Returns the exact Classic Mac key message already consumed by the preserved
// exploration/dungeon event loops. Unsupported command/context combinations
// return nullopt instead of reaching into engine globals directly.
[[nodiscard]] std::optional<uint32_t> legacy_key_message_for_movement(
    MovementCommand command,
    const RuntimeLegacyCommandContext& context) noexcept;

// Opening inventory is intentionally distinct from item-level InventoryAction
// commands. The returned message is the preserved Classic "i" key record and
// is available only on the two guarded top-level gameplay surfaces.
[[nodiscard]] std::optional<uint32_t> legacy_key_message_for_open_inventory(
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the preserved Classic "s" key record used to enter the spell
// chooser. The command is exposed only on guarded exploration/dungeon
// surfaces; spell selection and targeting stay in the Classic flow.
[[nodiscard]] std::optional<uint32_t> legacy_key_message_for_open_spellbook(
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Game > Save Current Game menu selection consumed by the
// existing top-level gameplay loops. The save-slot chooser and all writes stay
// inside the preserved Classic flow.
[[nodiscard]] std::optional<RuntimeLegacyMenuCommand>
legacy_menu_command_for_open_save_game(
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact in-game Game > Revert To A Previous Game menu selection.
// It opens the preserved chooser; only that compatibility flow can select a
// slot and replace live engine state.
[[nodiscard]] std::optional<RuntimeLegacyMenuCommand>
legacy_menu_command_for_open_load_game(
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "g" key record consumed by the preserved combat
// switch. The actor is range-checked here and revalidated against the live turn
// immediately before EventManager translates the tagged command.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_guard_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "f" key record consumed by the preserved combat
// switch. The actor is carried and range-checked independently from Guard so a
// queued Finish can never silently change its turn-ending semantics.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_finish_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "d" key record consumed by the preserved combat
// switch. Delay has its own typed route and sink so it cannot be confused with
// the adjacent Guard or Finish commands despite their identical signatures.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_delay_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "c" key record consumed by the preserved combat
// switch. The acting combatant is carried through an independent typed route so
// a queued camera command cannot silently follow a later turn.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_center_active_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "w" key record consumed by the preserved combat
// switch. The actor is carried through the typed route so a queued toggle
// cannot be applied to whichever combatant owns a later turn.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_switch_weapon(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "p" or "n" key record consumed by the preserved
// combat camera switch. The destination remains intentionally relative, while
// the acting combatant prevents a queued camera command from crossing a turn.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_cycle_combat_focus(
    CombatantId combatant,
    CombatFocusDirection direction,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "i" key record consumed by combat's preserved
// Items branch. Both the acting combatant and selected party member are carried
// to the runtime sink; the Classic modal remains authoritative for item actions.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_open_combat_items(
    CombatantId combatant,
    PartyMemberId member,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact lowercase Classic "a" key record consumed by combat's
// preserved Auto branch. The stable actor prevents a queued automation request
// from crossing a turn before EventManager performs its late validation.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_auto_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic "r" key record consumed by combat's preserved
// range-overlay branch. The actor is carried so a queued presentation command
// cannot cross a turn; Classic owns drawing, dismissal, and recentring.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_show_combat_range(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic lowercase "b" key record consumed by combat's
// preserved Bandage branch. The actor is carried through late validation;
// Classic remains authoritative for target selection and every mutation.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_bandage_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic lowercase "u" key record consumed by combat's
// preserved Undo branch. The actor and fresh Undo capability are late-checked;
// Classic remains authoritative for condition checks and every mutation.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_undo_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic lowercase "s" key record consumed by combat's
// preserved Cast Spell branch. Only the stable acting combatant crosses this
// boundary; Classic owns the chooser, targeting, costs/refunds, RNG, and every
// resulting combat-state mutation.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_open_combat_spellbook(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic lowercase "t" key record consumed by combat's
// preserved Target branch. Only the stable acting combatant crosses this
// boundary; Classic owns live equipment resolution, target selection, costs,
// RNG, and every resulting combat-state mutation.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_open_combat_targeting(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic lowercase "e" key record consumed by combat's
// preserved Escape branch. Only the stable acting combatant crosses this
// boundary; Classic owns range/condition checks, warnings, confirmation,
// mutation, RNG, and every subsequent turn or combat-exit effect.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_escape_combat(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the exact Classic lowercase "l" key record consumed by combat's
// preserved Use Scroll branch. Only the stable acting combatant crosses this
// boundary; Classic owns the chooser, selection/consumption, targeting, costs,
// RNG, and every subsequent turn effect.
[[nodiscard]] std::optional<uint32_t>
legacy_key_message_for_open_combat_scroll_case(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept;

// Live UIAction boundary for the migrated command subset. The bridge queues
// legacy input for the next normal event-loop iteration; it never calls nested
// Classic screen functions synchronously.
class RuntimeLegacyCommandBridge final : public LegacyCommandBridge {
public:
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyKeySink key_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyKeySink key_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
      RuntimeLegacyOpenSaveGameSink open_save_game_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
      RuntimeLegacyOpenSaveGameSink open_save_game_sink,
      RuntimeLegacyOpenLoadGameSink open_load_game_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
      RuntimeLegacyOpenSaveGameSink open_save_game_sink,
      RuntimeLegacyOpenLoadGameSink open_load_game_sink,
      RuntimeLegacyCombatActionSinks combat_action_sinks);

  // Compatibility overloads retain the append-only API used before combat
  // registration became named. New combat routes belong only in the bundle.
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
      RuntimeLegacyOpenSaveGameSink open_save_game_sink,
      RuntimeLegacyOpenLoadGameSink open_load_game_sink,
      RuntimeLegacyGuardCombatantSink guard_combatant_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
      RuntimeLegacyOpenSaveGameSink open_save_game_sink,
      RuntimeLegacyOpenLoadGameSink open_load_game_sink,
      RuntimeLegacyGuardCombatantSink guard_combatant_sink,
      RuntimeLegacyFinishCombatantSink finish_combatant_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
      RuntimeLegacyOpenSaveGameSink open_save_game_sink,
      RuntimeLegacyOpenLoadGameSink open_load_game_sink,
      RuntimeLegacyGuardCombatantSink guard_combatant_sink,
      RuntimeLegacyFinishCombatantSink finish_combatant_sink,
      RuntimeLegacyDelayCombatantSink delay_combatant_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
      RuntimeLegacyOpenSaveGameSink open_save_game_sink,
      RuntimeLegacyOpenLoadGameSink open_load_game_sink,
      RuntimeLegacyGuardCombatantSink guard_combatant_sink,
      RuntimeLegacyFinishCombatantSink finish_combatant_sink,
      RuntimeLegacyDelayCombatantSink delay_combatant_sink,
      RuntimeLegacyCenterActiveCombatantSink center_active_combatant_sink);

  [[nodiscard]] DispatchResult dispatch(const UIAction& action) override;

private:
  InjectedLegacyCommandBridge injected_bridge_;
};

} // namespace realmz::presentation
