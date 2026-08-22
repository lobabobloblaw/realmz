#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "GameSnapshot.hpp"
#include "PresentationMode.hpp"

namespace realmz::presentation {

using ActionSequence = uint64_t;

enum class MovementCommand {
  step_forward,
  step_backward,
  turn_left,
  turn_right,
  north,
  northeast,
  east,
  southeast,
  south,
  southwest,
  west,
  northwest,
};

struct MovePartyAction {
  MovementCommand command = MovementCommand::step_forward;

  bool operator==(const MovePartyAction&) const = default;
};

struct SelectPartyMemberAction {
  PartyMemberId member = 0;

  bool operator==(const SelectPartyMemberAction&) const = default;
};

// Opens the preserved Classic inventory flow for the explicitly selected
// member. Item-level commands remain separate InventoryAction payloads.
struct OpenInventoryAction {
  PartyMemberId member = 0;

  bool operator==(const OpenInventoryAction&) const = default;
};

// Opens the preserved Classic spell-selection flow for the explicitly
// selected caster. Choosing a spell and target remains a separate
// CastSpellAction concern.
struct OpenSpellbookAction {
  PartyMemberId member = 0;

  bool operator==(const OpenSpellbookAction&) const = default;
};

// Opens the preserved Classic non-combat scroll-case chooser for the
// explicitly selected party member. The chooser remains authoritative for
// the five case slots, scroll selection and consumption, targeting, costs,
// RNG, and every gameplay mutation.
struct OpenScrollCaseAction {
  PartyMemberId member = 0;

  bool operator==(const OpenScrollCaseAction&) const = default;
};

// Opens the preserved Classic character-sheet flow for the explicitly
// selected party member. Browsing the sheet and every nested modal remain
// owned by the compatibility flow.
struct OpenCharacterSheetAction {
  PartyMemberId member = 0;

  bool operator==(const OpenCharacterSheetAction&) const = default;
};

// Opens the preserved Classic save-slot chooser. Selecting a slot and writing
// data remain separate SaveGameAction concerns owned by the compatibility
// flow.
struct OpenSaveGameAction {
  bool operator==(const OpenSaveGameAction&) const = default;
};

// Opens the preserved in-game load/revert chooser. Selecting a slot and
// replacing engine state remain separate LoadGameAction concerns owned by the
// compatibility flow.
struct OpenLoadGameAction {
  bool operator==(const OpenLoadGameAction&) const = default;
};

// Requests the preserved Classic Rest command for the party. Camp entry,
// healing, time advancement, spell recovery, encounters, RNG, hold behavior,
// and every gameplay mutation remain authoritative in the compatibility flow.
struct RestPartyAction {
  bool operator==(const RestPartyAction&) const = default;
};

// Requests an explicit transition into or out of the preserved Classic camp
// state. Carrying the desired state prevents a queued command from reversing
// a newer camp transition as a relative toggle would.
struct SetCampStateAction {
  bool desired_in_camp = false;

  bool operator==(const SetCampStateAction&) const = default;
};

// Requests an explicit transition into or out of Classic's persistent Search
// condition. Carrying the desired state prevents a queued command from
// reversing a newer Search transition as a relative toggle would.
struct SetSearchStateAction {
  bool desired_searching = false;

  bool operator==(const SetSearchStateAction&) const = default;
};

// Requests Classic's party-scoped Torch command. An engaged source binds the
// request to the first usable Torch observed by the detached model so delayed
// delivery cannot silently consume a different inventory position. A
// disengaged source exists only for the shell's visible disabled control and
// is never dispatchable.
struct UseTorchAction {
  std::optional<TorchSource> source;

  bool operator==(const UseTorchAction&) const = default;
};

// Ends the explicitly identified party combatant's current turn in the
// preserved Classic combat loop with its Guard command. Carrying the actor
// prevents a queued action from silently retargeting after the turn advances.
struct GuardCombatantAction {
  CombatantId combatant = 0;

  bool operator==(const GuardCombatantAction&) const = default;
};

// Ends the explicitly identified party combatant's current turn through the
// preserved Classic Finish command. The stable actor ID prevents a queued
// command from finishing whichever combatant happens to act next.
struct FinishCombatantAction {
  CombatantId combatant = 0;

  bool operator==(const FinishCombatantAction&) const = default;
};

// Requests the preserved Classic Delay command for the explicitly identified
// party combatant. Carrying the actor prevents a queued Delay from being
// retargeted if the combat turn changes before the command is consumed.
struct DelayCombatantAction {
  CombatantId combatant = 0;

  bool operator==(const DelayCombatantAction&) const = default;
};

// Recenters the preserved Classic combat view on the explicitly identified
// acting party combatant. The stable actor ID prevents a queued presentation
// command from centering whichever combatant owns a later turn.
struct CenterActiveCombatantAction {
  CombatantId combatant = 0;

  bool operator==(const CenterActiveCombatantAction&) const = default;
};

// Requests the preserved Classic weapon-set toggle for the explicitly
// identified acting party combatant. The desired set is intentionally not
// encoded: the compatibility flow remains authoritative for the relative
// toggle and for its failure feedback when no alternate weapon is equipped.
struct SwitchWeaponSetAction {
  CombatantId combatant = 0;

  bool operator==(const SwitchWeaponSetAction&) const = default;
};

enum class CombatFocusDirection {
  previous,
  next,
};

// Cycles the preserved Classic combat view relative to its current inspected
// combatant. The acting combatant is carried independently so a queued camera
// command cannot silently cross a turn before it reaches the legacy loop.
struct CycleCombatFocusAction {
  CombatantId combatant = 0;
  CombatFocusDirection direction = CombatFocusDirection::next;

  bool operator==(const CycleCombatFocusAction&) const = default;
};

// Opens the preserved Classic combat Items flow for an explicitly identified
// acting combatant and selected party member. Keeping both identities stable
// prevents a queued command from crossing a turn or silently opening another
// member's inventory after the Classic portrait selection changes.
struct OpenCombatItemsAction {
  CombatantId combatant = 0;
  PartyMemberId member = 0;

  bool operator==(const OpenCombatItemsAction&) const = default;
};

// Requests the preserved Classic Auto command for the explicitly identified
// acting party combatant. Carrying the actor prevents a queued automation
// request from silently starting on whichever combatant owns a later turn.
struct AutoCombatantAction {
  CombatantId combatant = 0;

  bool operator==(const AutoCombatantAction&) const = default;
};

// Requests the preserved Classic combat-range overlay for the explicitly
// identified acting party combatant. The stable actor prevents a queued view
// command from revealing ranges relative to whichever combatant acts later.
struct ShowCombatRangeAction {
  CombatantId combatant = 0;

  bool operator==(const ShowCombatRangeAction&) const = default;
};

// Requests the preserved Classic Bandage flow for the explicitly identified
// acting party combatant. The stable actor prevents a queued request from
// opening the target picker on whichever combatant owns a later turn.
struct BandageCombatantAction {
  CombatantId combatant = 0;

  bool operator==(const BandageCombatantAction&) const = default;
};

// Requests the preserved Classic Undo flow for the explicitly identified
// acting party combatant. The stable actor prevents a queued request from
// undoing movement for whichever combatant owns a later turn. Classic remains
// authoritative for every condition check and combat-state mutation.
struct UndoCombatantAction {
  CombatantId combatant = 0;

  bool operator==(const UndoCombatantAction&) const = default;
};

// Opens the preserved Classic combat spell chooser for the explicitly
// identified acting party combatant. Spell selection, targeting, costs,
// refunds, RNG, and every combat-state mutation remain inside Classic.
struct OpenCombatSpellbookAction {
  CombatantId combatant = 0;

  bool operator==(const OpenCombatSpellbookAction&) const = default;
};

// Begins the preserved Classic Target flow for the explicitly identified
// acting party combatant. The live equipped item determines whether Classic
// selects a combatant, cell, area, multiple targets, or resolves automatically;
// no target choice or combat mutation crosses this boundary.
struct OpenCombatTargetingAction {
  CombatantId combatant = 0;

  bool operator==(const OpenCombatTargetingAction&) const = default;
};

// Attempts to escape through the preserved Classic combat flow for the
// explicitly identified acting party combatant. Classic remains authoritative
// for range and condition checks, warning/confirmation modals, RNG, queue and
// field mutation, prestige/light/coward state, and subsequent turn handling.
struct EscapeCombatAction {
  CombatantId combatant = 0;

  bool operator==(const EscapeCombatAction&) const = default;
};

// Opens the preserved Classic combat scroll case for the explicitly
// identified acting party combatant. Classic remains authoritative for scroll
// selection and consumption, targeting, costs, RNG, and turn handling.
struct OpenCombatScrollCaseAction {
  CombatantId combatant = 0;

  bool operator==(const OpenCombatScrollCaseAction&) const = default;
};

// Absolute battlefield coordinates remain stable if the Classic camera moves
// between pointer sampling and command delivery.
struct CombatFieldCell {
  uint8_t x = 0;
  uint8_t y = 0;

  bool operator==(const CombatFieldCell&) const = default;
};

// Recenters the preserved Classic combat view on an explicitly sampled field
// cell. The stable actor prevents a queued camera command from crossing a turn;
// Classic remains authoritative for camera bounds, drawing, and button state.
struct CenterCombatCursorAction {
  CombatantId combatant = 0;
  CombatFieldCell cell;

  bool operator==(const CenterCombatCursorAction&) const = default;
};

enum class InventoryVerb {
  use,
  equip,
  unequip,
  drop,
  transfer,
};

struct InventoryAction {
  InventoryVerb verb = InventoryVerb::use;
  PartyMemberId actor = 0;
  ItemInstanceId item = 0;
  std::optional<PartyMemberId> recipient;
  uint16_t quantity = 1;

  bool operator==(const InventoryAction&) const = default;
};

enum class TargetKind {
  none,
  party_member,
  combatant,
  map_cell,
};

struct ActionTarget {
  TargetKind kind = TargetKind::none;
  int32_t primary = 0;
  int32_t secondary = 0;

  bool operator==(const ActionTarget&) const = default;

  [[nodiscard]] static constexpr ActionTarget none() noexcept {
    return {};
  }

  [[nodiscard]] static constexpr ActionTarget party_member(PartyMemberId member) noexcept {
    return {TargetKind::party_member, member, 0};
  }

  [[nodiscard]] static constexpr ActionTarget combatant(CombatantId combatant_id) noexcept {
    return {TargetKind::combatant, combatant_id, 0};
  }

  [[nodiscard]] static constexpr ActionTarget map_cell(int32_t x, int32_t y) noexcept {
    return {TargetKind::map_cell, x, y};
  }
};

struct CastSpellAction {
  PartyMemberId caster = 0;
  int32_t spell_id = 0;
  ActionTarget target;

  bool operator==(const CastSpellAction&) const = default;
};

enum class TradeVerb {
  buy,
  sell,
};

struct TradeAction {
  TradeVerb verb = TradeVerb::buy;
  int32_t merchant_item_id = 0;
  uint16_t quantity = 1;
  std::optional<PartyMemberId> party_member;

  bool operator==(const TradeAction&) const = default;
};

struct SaveGameAction {
  std::string slot;

  bool operator==(const SaveGameAction&) const = default;
};

struct LoadGameAction {
  std::string slot;

  bool operator==(const LoadGameAction&) const = default;
};

struct ConfirmAction {
  bool operator==(const ConfirmAction&) const = default;
};

struct CancelAction {
  bool operator==(const CancelAction&) const = default;
};

enum class DrawerPanel {
  details,
  event_log,
};

enum class WorldActionPage {
  travel,
  party,
  game,
};

// World pages are selected directly from a persistent command deck. The
// already-selected page remains a valid idempotent destination, while
// malformed enum values fail the same shared transition contract used by
// pointer and keyboard routing.
[[nodiscard]] constexpr bool is_valid_world_action_page_transition(
    WorldActionPage from,
    WorldActionPage to) noexcept {
  const auto is_valid_page = [](WorldActionPage page) constexpr {
    switch (page) {
      case WorldActionPage::travel:
      case WorldActionPage::party:
      case WorldActionPage::game:
        return true;
    }
    return false;
  };
  return is_valid_page(from) && is_valid_page(to);
}

enum class CombatActionPage {
  primary,
  secondary,
  utility,
  special,
};

// Combat pages are selected directly from a persistent command deck. Keeping
// this predicate shared makes pointer and keyboard routing accept every valid
// page (including the already-selected page) while still rejecting malformed
// enum values through the same constexpr contract.
[[nodiscard]] constexpr bool is_valid_combat_action_page_transition(
    CombatActionPage from,
    CombatActionPage to) noexcept {
  const auto is_valid_page = [](CombatActionPage page) constexpr {
    switch (page) {
      case CombatActionPage::primary:
      case CombatActionPage::secondary:
      case CombatActionPage::utility:
      case CombatActionPage::special:
        return true;
    }
    return false;
  };
  return is_valid_page(from) && is_valid_page(to);
}

// Compact-shell drawers are presentation state only. The desired panel is
// explicit so a recorded action does not depend on whatever happened to be
// open when it is replayed; nullopt closes the current drawer.
struct SetDrawerPanelAction {
  std::optional<DrawerPanel> panel;

  bool operator==(const SetDrawerPanelAction&) const = default;
};

// The world action page is presentation state only. Carrying the desired page
// makes recorded shell input deterministic and avoids a state-relative toggle.
struct SetWorldActionPageAction {
  WorldActionPage page = WorldActionPage::travel;

  bool operator==(const SetWorldActionPageAction&) const = default;
};

// The combat action page is presentation state only. Carrying the desired page
// makes recorded shell input deterministic and avoids a state-relative toggle.
struct SetCombatActionPageAction {
  CombatActionPage page = CombatActionPage::primary;

  bool operator==(const SetCombatActionPageAction&) const = default;
};

// Presentation changes are semantic UI commands but do not mutate save data.
// Keeping them in the same stream allows deterministic input replays.
struct SetPresentationModeAction {
  PresentationMode mode = PresentationMode::classic;

  bool operator==(const SetPresentationModeAction&) const = default;
};

using UIActionPayload = std::variant<
    MovePartyAction,
    SelectPartyMemberAction,
    OpenInventoryAction,
    OpenSpellbookAction,
    OpenScrollCaseAction,
    OpenCharacterSheetAction,
    OpenSaveGameAction,
    OpenLoadGameAction,
    RestPartyAction,
    GuardCombatantAction,
    FinishCombatantAction,
    DelayCombatantAction,
    CenterActiveCombatantAction,
    SwitchWeaponSetAction,
    CycleCombatFocusAction,
    OpenCombatItemsAction,
    AutoCombatantAction,
    ShowCombatRangeAction,
    BandageCombatantAction,
    UndoCombatantAction,
    OpenCombatSpellbookAction,
    OpenCombatTargetingAction,
    EscapeCombatAction,
    OpenCombatScrollCaseAction,
    CenterCombatCursorAction,
    InventoryAction,
    CastSpellAction,
    TradeAction,
    SaveGameAction,
    LoadGameAction,
    ConfirmAction,
    CancelAction,
    SetDrawerPanelAction,
    SetWorldActionPageAction,
    SetCombatActionPageAction,
    SetPresentationModeAction,
    SetCampStateAction,
    SetSearchStateAction,
    UseTorchAction>;

struct UIAction {
  ActionSequence sequence = 0;
  UIActionPayload payload;

  bool operator==(const UIAction&) const = default;
};

[[nodiscard]] inline std::string_view action_name(const UIActionPayload& payload) noexcept {
  return std::visit([](const auto& action) -> std::string_view {
    using Action = std::decay_t<decltype(action)>;
    if constexpr (std::is_same_v<Action, MovePartyAction>) {
      return "move_party";
    } else if constexpr (std::is_same_v<Action, SelectPartyMemberAction>) {
      return "select_party_member";
    } else if constexpr (std::is_same_v<Action, OpenInventoryAction>) {
      return "open_inventory";
    } else if constexpr (std::is_same_v<Action, OpenSpellbookAction>) {
      return "open_spellbook";
    } else if constexpr (std::is_same_v<Action, OpenScrollCaseAction>) {
      return "open_scroll_case";
    } else if constexpr (std::is_same_v<Action, OpenCharacterSheetAction>) {
      return "open_character_sheet";
    } else if constexpr (std::is_same_v<Action, OpenSaveGameAction>) {
      return "open_save_game";
    } else if constexpr (std::is_same_v<Action, OpenLoadGameAction>) {
      return "open_load_game";
    } else if constexpr (std::is_same_v<Action, RestPartyAction>) {
      return "rest_party";
    } else if constexpr (std::is_same_v<Action, GuardCombatantAction>) {
      return "guard_combatant";
    } else if constexpr (std::is_same_v<Action, FinishCombatantAction>) {
      return "finish_combatant";
    } else if constexpr (std::is_same_v<Action, DelayCombatantAction>) {
      return "delay_combatant";
    } else if constexpr (
        std::is_same_v<Action, CenterActiveCombatantAction>) {
      return "center_active_combatant";
    } else if constexpr (std::is_same_v<Action, SwitchWeaponSetAction>) {
      return "switch_weapon_set";
    } else if constexpr (std::is_same_v<Action, CycleCombatFocusAction>) {
      return "cycle_combat_focus";
    } else if constexpr (std::is_same_v<Action, OpenCombatItemsAction>) {
      return "open_combat_items";
    } else if constexpr (std::is_same_v<Action, AutoCombatantAction>) {
      return "auto_combatant";
    } else if constexpr (std::is_same_v<Action, ShowCombatRangeAction>) {
      return "show_combat_range";
    } else if constexpr (std::is_same_v<Action, BandageCombatantAction>) {
      return "bandage_combatant";
    } else if constexpr (std::is_same_v<Action, UndoCombatantAction>) {
      return "undo_combatant";
    } else if constexpr (std::is_same_v<Action, OpenCombatSpellbookAction>) {
      return "open_combat_spellbook";
    } else if constexpr (std::is_same_v<Action, OpenCombatTargetingAction>) {
      return "open_combat_targeting";
    } else if constexpr (std::is_same_v<Action, EscapeCombatAction>) {
      return "escape_combat";
    } else if constexpr (
        std::is_same_v<Action, OpenCombatScrollCaseAction>) {
      return "open_combat_scroll_case";
    } else if constexpr (std::is_same_v<Action, CenterCombatCursorAction>) {
      return "center_combat_cursor";
    } else if constexpr (std::is_same_v<Action, InventoryAction>) {
      return "inventory";
    } else if constexpr (std::is_same_v<Action, CastSpellAction>) {
      return "cast_spell";
    } else if constexpr (std::is_same_v<Action, TradeAction>) {
      return "trade";
    } else if constexpr (std::is_same_v<Action, SaveGameAction>) {
      return "save_game";
    } else if constexpr (std::is_same_v<Action, LoadGameAction>) {
      return "load_game";
    } else if constexpr (std::is_same_v<Action, ConfirmAction>) {
      return "confirm";
    } else if constexpr (std::is_same_v<Action, CancelAction>) {
      return "cancel";
    } else if constexpr (std::is_same_v<Action, SetDrawerPanelAction>) {
      return "set_drawer_panel";
    } else if constexpr (std::is_same_v<Action, SetWorldActionPageAction>) {
      return "set_world_action_page";
    } else if constexpr (std::is_same_v<Action, SetCombatActionPageAction>) {
      return "set_combat_action_page";
    } else if constexpr (std::is_same_v<Action, SetCampStateAction>) {
      return "set_camp_state";
    } else if constexpr (std::is_same_v<Action, SetSearchStateAction>) {
      return "set_search_state";
    } else if constexpr (std::is_same_v<Action, UseTorchAction>) {
      return "use_torch";
    } else {
      return "set_presentation_mode";
    }
  }, payload);
}

} // namespace realmz::presentation
