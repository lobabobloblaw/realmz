#include "RuntimeLegacyCommandBridge.hpp"

#include <format>
#include <string_view>
#include <utility>

namespace realmz::presentation {
namespace {

constexpr uint32_t kArrowUpMessage = 0x00007E1EU;
constexpr uint32_t kArrowDownMessage = 0x00007D1FU;
constexpr uint32_t kArrowLeftMessage = 0x00007B1CU;
constexpr uint32_t kArrowRightMessage = 0x00007C1DU;
constexpr uint32_t kKeypadOneMessage = 0x00005331U;
constexpr uint32_t kKeypadThreeMessage = 0x00005533U;
constexpr uint32_t kKeypadSevenMessage = 0x00005937U;
constexpr uint32_t kKeypadNineMessage = 0x00005C39U;
constexpr uint32_t kOpenInventoryMessage = 0x00002269U;
constexpr uint32_t kOpenSpellbookMessage = 0x00000173U;
constexpr uint32_t kOpenOutdoorScrollCaseMessage = 0x0000256CU;
constexpr uint32_t kOpenDungeonScrollCaseMessage = 0x00002370U;
constexpr uint32_t kRestPartyMessage = 0x00000F72U;
constexpr uint32_t kSetCampStateMessage = 0x00000863U;
constexpr uint32_t kAreaSearchMessage = 0x00000061U;
constexpr uint32_t kMakeScrollMessage = 0x0000286BU;
constexpr uint32_t kEnterShopOrTempleMessage = 0x00000567U;
constexpr uint32_t kCheckLocalEncounterMessage = 0x00000E65U;
constexpr uint32_t kOpenMoneyManagementMessage = 0x00002E6DU;
constexpr uint32_t kGuardCombatantMessage = 0x00000567U;
constexpr uint32_t kFinishCombatantMessage = 0x00000366U;
constexpr uint32_t kDelayCombatantMessage = 0x00000264U;
constexpr uint32_t kCenterActiveCombatantMessage = 0x00000863U;
constexpr uint32_t kSwitchWeaponMessage = 0x00000D77U;
constexpr uint32_t kCenterPreviousCombatantMessage = 0x00002370U;
constexpr uint32_t kCenterNextCombatantMessage = 0x00002D6EU;
constexpr uint32_t kOpenCombatItemsMessage = 0x00002269U;
constexpr uint32_t kAutoCombatantMessage = 0x00000061U;
constexpr uint32_t kShowCombatRangeMessage = 0x00000F72U;
constexpr uint32_t kBandageCombatantMessage = 0x00000B62U;
constexpr uint32_t kUndoCombatantMessage = 0x00002075U;
constexpr uint32_t kOpenCombatSpellbookMessage = 0x00000173U;
constexpr uint32_t kOpenCombatTargetingMessage = 0x00001174U;
constexpr uint32_t kEscapeCombatMessage = 0x00000E65U;
constexpr uint32_t kOpenCombatScrollCaseMessage = 0x0000256CU;
constexpr uint32_t kCenterCombatCursorMessage = 0x00002E6DU;
constexpr int16_t kGameMenuId = 129;
constexpr int16_t kRevertToPreviousGameItemId = 2;
constexpr int16_t kSaveCurrentGameItemId = 3;
constexpr PartyMemberId kMaximumPartyMemberId = 5;

std::string_view movement_name(MovementCommand command) noexcept {
  switch (command) {
    case MovementCommand::step_forward: return "step_forward";
    case MovementCommand::step_backward: return "step_backward";
    case MovementCommand::turn_left: return "turn_left";
    case MovementCommand::turn_right: return "turn_right";
    case MovementCommand::north: return "north";
    case MovementCommand::northeast: return "northeast";
    case MovementCommand::east: return "east";
    case MovementCommand::southeast: return "southeast";
    case MovementCommand::south: return "south";
    case MovementCommand::southwest: return "southwest";
    case MovementCommand::west: return "west";
    case MovementCommand::northwest: return "northwest";
  }
  return "unknown";
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink) {
  LegacyActionHandlers handlers;
  handlers.move_party = [
      context_provider = std::move(context_provider),
      movement_sink = std::move(movement_sink)](const MovePartyAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!movement_sink) {
      return DispatchResult::failed(
          "Runtime legacy key-event sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic movement");
    }
    const auto message = legacy_key_message_for_movement(
        action.command, context);
    if (!message) {
      return DispatchResult::rejected(std::format(
          "Movement {} is not supported in the current legacy context",
          movement_name(action.command)));
    }
    if (!movement_sink(action.command, *message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic movement");
    }
    return DispatchResult::handled();
  };
  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink);

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink,
    RuntimeLegacyCombatActionSinks combat_action_sinks) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink),
      std::move(open_inventory_sink),
      std::move(open_spellbook_sink),
      std::move(open_save_game_sink),
      std::move(open_load_game_sink));

  if (combat_action_sinks.guard_combatant.has_value()) {
    handlers.guard_combatant = [
        context_provider,
        guard_combatant_sink =
            std::move(*combat_action_sinks.guard_combatant)](
            const GuardCombatantAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!guard_combatant_sink) {
        return DispatchResult::failed(
            "Runtime legacy guard-combatant sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic guard");
      }
      const auto message = legacy_key_message_for_guard_combatant(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Guard is not supported for this combatant in the current legacy "
            "context");
      }
      if (!guard_combatant_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic guard-combatant action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.finish_combatant.has_value()) {
    handlers.finish_combatant = [
        context_provider,
        finish_combatant_sink =
            std::move(*combat_action_sinks.finish_combatant)](
            const FinishCombatantAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!finish_combatant_sink) {
        return DispatchResult::failed(
            "Runtime legacy finish-combatant sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic finish");
      }
      const auto message = legacy_key_message_for_finish_combatant(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Finish is not supported for this combatant in the current legacy "
            "context");
      }
      if (!finish_combatant_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic finish-combatant action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.delay_combatant.has_value()) {
    handlers.delay_combatant = [
        context_provider,
        delay_combatant_sink =
            std::move(*combat_action_sinks.delay_combatant)](
            const DelayCombatantAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!delay_combatant_sink) {
        return DispatchResult::failed(
            "Runtime legacy delay-combatant sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic delay");
      }
      const auto message = legacy_key_message_for_delay_combatant(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Delay is not supported for this combatant in the current legacy "
            "context");
      }
      if (!delay_combatant_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic delay-combatant action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.center_active_combatant.has_value()) {
    handlers.center_active_combatant = [
        context_provider,
        center_active_combatant_sink =
            std::move(*combat_action_sinks.center_active_combatant)](
            const CenterActiveCombatantAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!center_active_combatant_sink) {
        return DispatchResult::failed(
            "Runtime legacy center-active-combatant sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic "
            "center-active");
      }
      const auto message = legacy_key_message_for_center_active_combatant(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Center active is not supported for this combatant in the current "
            "legacy context");
      }
      if (!center_active_combatant_sink(
              action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic center-active-combatant "
            "action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.switch_weapon.has_value()) {
    handlers.switch_weapon_set = [
        context_provider,
        switch_weapon_sink = std::move(*combat_action_sinks.switch_weapon)](
            const SwitchWeaponSetAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!switch_weapon_sink) {
        return DispatchResult::failed(
            "Runtime legacy switch-weapon sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic weapon "
            "switching");
      }
      const auto message = legacy_key_message_for_switch_weapon(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Weapon switching is not supported for this combatant in the "
            "current legacy context");
      }
      if (!switch_weapon_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic switch-weapon action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.cycle_combat_focus.has_value()) {
    handlers.cycle_combat_focus = [
        context_provider,
        cycle_combat_focus_sink =
            std::move(*combat_action_sinks.cycle_combat_focus)](
            const CycleCombatFocusAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!cycle_combat_focus_sink) {
        return DispatchResult::failed(
            "Runtime legacy cycle-combat-focus sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic combat-focus "
            "cycling");
      }
      const auto message = legacy_key_message_for_cycle_combat_focus(
          action.combatant, action.direction, context);
      if (!message) {
        return DispatchResult::rejected(
            "Combat-focus cycling is not supported for this combatant or "
            "direction in the current legacy context");
      }
      if (!cycle_combat_focus_sink(
              action.combatant, action.direction, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic cycle-combat-focus action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.open_combat_items.has_value()) {
    handlers.open_combat_items = [
        context_provider,
        open_combat_items_sink =
            std::move(*combat_action_sinks.open_combat_items)](
            const OpenCombatItemsAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!open_combat_items_sink) {
        return DispatchResult::failed(
            "Runtime legacy open-combat-items sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic combat items");
      }
      const auto message = legacy_key_message_for_open_combat_items(
          action.combatant, action.member, context);
      if (!message) {
        return DispatchResult::rejected(
            "Combat Items is not supported for this combatant or party member "
            "in the current legacy context");
      }
      if (!open_combat_items_sink(
              action.combatant, action.member, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic open-combat-items action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.auto_combatant.has_value()) {
    handlers.auto_combatant = [
        context_provider,
        auto_combatant_sink =
            std::move(*combat_action_sinks.auto_combatant)](
            const AutoCombatantAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!auto_combatant_sink) {
        return DispatchResult::failed(
            "Runtime legacy auto-combatant sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic auto");
      }
      const auto message = legacy_key_message_for_auto_combatant(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Auto is not supported for this combatant in the current legacy "
            "context");
      }
      if (!auto_combatant_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic auto-combatant action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.show_combat_range.has_value()) {
    handlers.show_combat_range = [
        context_provider,
        show_combat_range_sink =
            std::move(*combat_action_sinks.show_combat_range)](
            const ShowCombatRangeAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!show_combat_range_sink) {
        return DispatchResult::failed(
            "Runtime legacy show-combat-range sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic range display");
      }
      const auto message = legacy_key_message_for_show_combat_range(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Range display is not supported for this combatant in the current "
            "legacy context");
      }
      if (!show_combat_range_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic show-combat-range action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.bandage_combatant.has_value()) {
    handlers.bandage_combatant = [
        context_provider,
        bandage_combatant_sink =
            std::move(*combat_action_sinks.bandage_combatant)](
            const BandageCombatantAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!bandage_combatant_sink) {
        return DispatchResult::failed(
            "Runtime legacy bandage-combatant sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic bandage");
      }
      const auto message = legacy_key_message_for_bandage_combatant(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Bandage is not supported for this combatant in the current "
            "legacy context");
      }
      if (!bandage_combatant_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic bandage-combatant action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.undo_combatant.has_value()) {
    handlers.undo_combatant = [
        context_provider,
        undo_combatant_sink =
            std::move(*combat_action_sinks.undo_combatant)](
            const UndoCombatantAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!undo_combatant_sink) {
        return DispatchResult::failed(
            "Runtime legacy undo-combatant sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic undo");
      }
      const auto message = legacy_key_message_for_undo_combatant(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Undo is not supported for this combatant in the current legacy "
            "context");
      }
      if (!undo_combatant_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic undo-combatant action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.open_combat_spellbook.has_value()) {
    handlers.open_combat_spellbook = [
        context_provider,
        open_combat_spellbook_sink =
            std::move(*combat_action_sinks.open_combat_spellbook)](
            const OpenCombatSpellbookAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!open_combat_spellbook_sink) {
        return DispatchResult::failed(
            "Runtime legacy open-combat-spellbook sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic combat "
            "spellcasting");
      }
      const auto message = legacy_key_message_for_open_combat_spellbook(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Combat spellcasting is not supported for this combatant in the "
            "current legacy context");
      }
      if (!open_combat_spellbook_sink(
              action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic open-combat-spellbook "
            "action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.open_combat_targeting.has_value()) {
    handlers.open_combat_targeting = [
        context_provider,
        open_combat_targeting_sink =
            std::move(*combat_action_sinks.open_combat_targeting)](
            const OpenCombatTargetingAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!open_combat_targeting_sink) {
        return DispatchResult::failed(
            "Runtime legacy open-combat-targeting sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic combat "
            "targeting");
      }
      const auto message = legacy_key_message_for_open_combat_targeting(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Combat targeting is not supported for this combatant in the "
            "current legacy context");
      }
      if (!open_combat_targeting_sink(
              action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic open-combat-targeting "
            "action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.escape_combat.has_value()) {
    handlers.escape_combat = [
        context_provider,
        escape_combat_sink = std::move(*combat_action_sinks.escape_combat)](
            const EscapeCombatAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!escape_combat_sink) {
        return DispatchResult::failed(
            "Runtime legacy escape-combat sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic escape");
      }
      const auto message = legacy_key_message_for_escape_combat(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Escape is not supported for this combatant in the current "
            "legacy context");
      }
      if (!escape_combat_sink(action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic escape-combat action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.open_combat_scroll_case.has_value()) {
    handlers.open_combat_scroll_case = [
        context_provider,
        open_combat_scroll_case_sink =
            std::move(*combat_action_sinks.open_combat_scroll_case)](
            const OpenCombatScrollCaseAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!open_combat_scroll_case_sink) {
        return DispatchResult::failed(
            "Runtime legacy open-combat-scroll-case sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic combat "
            "scroll use");
      }
      const auto message = legacy_key_message_for_open_combat_scroll_case(
          action.combatant, context);
      if (!message) {
        return DispatchResult::rejected(
            "Combat scroll use is not supported for this combatant in the "
            "current legacy context");
      }
      if (!open_combat_scroll_case_sink(
              action.combatant, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic open-combat-scroll-case "
            "action");
      }
      return DispatchResult::handled();
    };
  }

  if (combat_action_sinks.center_combat_cursor.has_value()) {
    handlers.center_combat_cursor = [
        context_provider,
        center_combat_cursor_sink =
            std::move(*combat_action_sinks.center_combat_cursor)](
            const CenterCombatCursorAction& action) {
      if (!context_provider) {
        return DispatchResult::failed(
            "Runtime legacy context provider is not available");
      }
      if (!center_combat_cursor_sink) {
        return DispatchResult::failed(
            "Runtime legacy center-combat-cursor sink is not available");
      }

      const auto context = context_provider();
      if (!context.adaptive_eligible) {
        return DispatchResult::rejected(
            "Legacy combat surface is not eligible for semantic cursor "
            "centering");
      }
      const auto message = legacy_key_message_for_center_combat_cursor(
          action.combatant, action.cell, context);
      if (!message) {
        return DispatchResult::rejected(
            "Cursor centering is not supported for this combatant or field "
            "cell in the current legacy context");
      }
      if (!center_combat_cursor_sink(
              action.combatant, action.cell, *message, context)) {
        return DispatchResult::failed(
            "Legacy event queue rejected semantic center-combat-cursor "
            "action");
      }
      return DispatchResult::handled();
    };
  }

  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyWorldActionSinks world_action_sinks,
    RuntimeLegacyCombatActionSinks combat_action_sinks) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink),
      std::move(world_action_sinks.open_inventory),
      std::move(world_action_sinks.open_spellbook),
      std::move(world_action_sinks.open_save_game),
      std::move(world_action_sinks.open_load_game),
      std::move(combat_action_sinks));
  handlers.open_scroll_case = [
      context_provider,
      open_scroll_case_sink =
          std::move(world_action_sinks.open_scroll_case)](
          const OpenScrollCaseAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_scroll_case_sink) {
      return DispatchResult::failed(
          "Runtime legacy open-scroll-case sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic scroll use");
    }
    const auto message = legacy_key_message_for_open_scroll_case(context);
    if (!message) {
      return DispatchResult::rejected(
          "Opening the scroll case is not supported in the current legacy "
          "context");
    }
    if (!open_scroll_case_sink(action.member, *message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic open-scroll-case action");
    }
    return DispatchResult::handled();
  };
  handlers.open_character_sheet = [
      context_provider,
      open_character_sheet_sink =
          std::move(world_action_sinks.open_character_sheet)](
          const OpenCharacterSheetAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_character_sheet_sink) {
      return DispatchResult::failed(
          "Runtime legacy open-character-sheet sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic character "
          "sheets");
    }
    if ((action.member > kMaximumPartyMemberId) ||
        !runtime_legacy_context_supports_open_character_sheet(context)) {
      return DispatchResult::rejected(
          "Opening the character sheet is not supported for this party "
          "member in the current legacy context");
    }
    if (!open_character_sheet_sink(action.member, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic open-character-sheet action");
    }
    return DispatchResult::handled();
  };
  handlers.rest_party = [
      context_provider,
      rest_party_sink = std::move(world_action_sinks.rest_party)](
          const RestPartyAction&) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!rest_party_sink) {
      return DispatchResult::failed(
          "Runtime legacy rest-party sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic rest");
    }
    const auto message = legacy_key_message_for_rest_party(context);
    if (!message) {
      return DispatchResult::rejected(
          "Rest is not supported in the current legacy context");
    }
    if (!rest_party_sink(*message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic rest-party action");
    }
    return DispatchResult::handled();
  };
  handlers.set_camp_state = [
      context_provider,
      set_camp_state_sink =
          std::move(world_action_sinks.set_camp_state)](
          const SetCampStateAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!set_camp_state_sink) {
      return DispatchResult::failed(
          "Runtime legacy set-camp-state sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic camp state");
    }
    const auto message = legacy_key_message_for_set_camp_state(
        action.desired_in_camp, context);
    if (!message) {
      return DispatchResult::rejected(
          "Camp state is already satisfied or is not supported in the "
          "current legacy context");
    }
    if (!set_camp_state_sink(
            action.desired_in_camp, *message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic set-camp-state action");
    }
    return DispatchResult::handled();
  };
  handlers.set_search_state = [
      context_provider,
      set_search_state_sink =
          std::move(world_action_sinks.set_search_state)](
          const SetSearchStateAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!set_search_state_sink) {
      return DispatchResult::failed(
          "Runtime legacy set-search-state sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic search state");
    }
    if (!runtime_legacy_context_supports_set_search_state(
            action.desired_searching, context)) {
      return DispatchResult::rejected(
          "Search state is already satisfied or is not supported in the "
          "current legacy context");
    }
    if (!set_search_state_sink(action.desired_searching, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic set-search-state action");
    }
    return DispatchResult::handled();
  };
  handlers.use_torch = [
      context_provider,
      use_torch_sink = std::move(world_action_sinks.use_torch)](
          const UseTorchAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!use_torch_sink) {
      return DispatchResult::failed(
          "Runtime legacy use-torch sink is not available");
    }
    if (!action.source) {
      return DispatchResult::rejected(
          "Torch use requires a fresh inventory source");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic Torch use");
    }
    if (!runtime_legacy_context_supports_use_torch(*action.source, context)) {
      return DispatchResult::rejected(
          "Torch use is not supported for this inventory source in the "
          "current legacy context");
    }
    if (!use_torch_sink(*action.source, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic use-torch action");
    }
    return DispatchResult::handled();
  };
  handlers.open_selected_item_drilldown = [
      context_provider,
      open_selected_item_drilldown_sink =
          std::move(world_action_sinks.open_selected_item_drilldown)](
          const OpenSelectedItemDrilldownAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_selected_item_drilldown_sink) {
      return DispatchResult::failed(
          "Runtime legacy selected-item-drilldown sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic selected-item "
          "drilldown");
    }
    if ((action.member > kMaximumPartyMemberId) ||
        !runtime_legacy_context_supports_selected_item_drilldown(context)) {
      return DispatchResult::rejected(
          "Opening the selected-item drilldown is not supported for this "
          "party member in the current legacy context");
    }
    if (!open_selected_item_drilldown_sink(action.member, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic selected-item-drilldown "
          "action");
    }
    return DispatchResult::handled();
  };
  handlers.contextual_world_entry = [
      context_provider,
      contextual_world_entry_sink =
          std::move(world_action_sinks.contextual_world_entry)](
          const ContextualWorldEntryAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!contextual_world_entry_sink) {
      return DispatchResult::failed(
          "Runtime legacy contextual-world-entry sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic world "
          "entry");
    }
    const auto message = legacy_key_message_for_contextual_world_entry(
        action, context);
    if (!message) {
      return DispatchResult::rejected(
          "Contextual world entry is not supported in the current legacy "
          "context");
    }
    if (!contextual_world_entry_sink(action, *message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic contextual-world-entry "
          "action");
    }
    return DispatchResult::handled();
  };
  handlers.open_money_management = [
      context_provider,
      open_money_management_sink =
          std::move(world_action_sinks.open_money_management)](
          const OpenMoneyManagementAction&) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_money_management_sink) {
      return DispatchResult::failed(
          "Runtime legacy open-money-management sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic money "
          "management");
    }
    const auto message = legacy_key_message_for_open_money_management(context);
    if (!message) {
      return DispatchResult::rejected(
          "Opening money management is not supported in the current legacy "
          "context");
    }
    if (!open_money_management_sink(*message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic open-money-management "
          "action");
    }
    return DispatchResult::handled();
  };
  handlers.contextual_overview = [
      context_provider = std::move(context_provider),
      contextual_overview_sink =
          std::move(world_action_sinks.contextual_overview)](
          const ContextualOverviewAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!contextual_overview_sink) {
      return DispatchResult::failed(
          "Runtime legacy contextual-overview sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for the semantic "
          "Overview control");
    }
    const auto message = legacy_key_message_for_contextual_overview(
        action, context);
    if (!message) {
      return DispatchResult::rejected(
          "The contextual Overview control is not supported in the current "
          "legacy context");
    }
    if (!contextual_overview_sink(action, *message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic contextual-overview action");
    }
    return DispatchResult::handled();
  };
  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink);

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink),
      std::move(open_inventory_sink),
      std::move(open_spellbook_sink));
  handlers.open_save_game = [
      context_provider = std::move(context_provider),
      open_save_game_sink = std::move(open_save_game_sink)](
          const OpenSaveGameAction&) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_save_game_sink) {
      return DispatchResult::failed(
          "Runtime legacy open-save-game sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic save");
    }
    const auto command = legacy_menu_command_for_open_save_game(context);
    if (!command) {
      return DispatchResult::rejected(
          "Opening the save chooser is not supported in the current legacy "
          "context");
    }
    if (!open_save_game_sink(*command, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic open-save-game action");
    }
    return DispatchResult::handled();
  };
  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink) {
  auto handlers = make_handlers(context_provider, std::move(movement_sink));
  handlers.select_party_member = [
      context_provider = std::move(context_provider),
      party_selection_sink = std::move(party_selection_sink)](
          const SelectPartyMemberAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!party_selection_sink) {
      return DispatchResult::failed(
          "Runtime legacy party-selection sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic party "
          "selection");
    }
    if ((context.screen != ScreenContext::exploration) &&
        (context.screen != ScreenContext::dungeon)) {
      return DispatchResult::rejected(
          "Party selection is not supported in the current legacy context");
    }
    if (!party_selection_sink(action.member, context)) {
      return DispatchResult::failed(
          "Legacy party-selection sink rejected semantic selection");
    }
    return DispatchResult::handled();
  };
  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink),
      std::move(open_inventory_sink),
      std::move(open_spellbook_sink),
      std::move(open_save_game_sink));
  handlers.open_load_game = [
      context_provider = std::move(context_provider),
      open_load_game_sink = std::move(open_load_game_sink)](
          const OpenLoadGameAction&) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_load_game_sink) {
      return DispatchResult::failed(
          "Runtime legacy open-load-game sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic load");
    }
    const auto command = legacy_menu_command_for_open_load_game(context);
    if (!command) {
      return DispatchResult::rejected(
          "Opening the load chooser is not supported in the current legacy "
          "context");
    }
    if (!open_load_game_sink(*command, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic open-load-game action");
    }
    return DispatchResult::handled();
  };
  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink));
  handlers.open_inventory = [
      context_provider = std::move(context_provider),
      open_inventory_sink = std::move(open_inventory_sink)](
          const OpenInventoryAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_inventory_sink) {
      return DispatchResult::failed(
          "Runtime legacy open-inventory sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic inventory");
    }
    const auto message = legacy_key_message_for_open_inventory(context);
    if (!message) {
      return DispatchResult::rejected(
          "Opening inventory is not supported in the current legacy context");
    }
    if (!open_inventory_sink(action.member, *message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic open-inventory action");
    }
    return DispatchResult::handled();
  };
  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink),
      std::move(open_inventory_sink));
  handlers.open_spellbook = [
      context_provider = std::move(context_provider),
      open_spellbook_sink = std::move(open_spellbook_sink)](
          const OpenSpellbookAction& action) {
    if (!context_provider) {
      return DispatchResult::failed(
          "Runtime legacy context provider is not available");
    }
    if (!open_spellbook_sink) {
      return DispatchResult::failed(
          "Runtime legacy open-spellbook sink is not available");
    }

    const auto context = context_provider();
    if (!context.adaptive_eligible) {
      return DispatchResult::rejected(
          "Legacy gameplay surface is not eligible for semantic spells");
    }
    const auto message = legacy_key_message_for_open_spellbook(context);
    if (!message) {
      return DispatchResult::rejected(
          "Opening the spellbook is not supported in the current legacy "
          "context");
    }
    if (!open_spellbook_sink(action.member, *message, context)) {
      return DispatchResult::failed(
          "Legacy event queue rejected semantic open-spellbook action");
    }
    return DispatchResult::handled();
  };
  return handlers;
}

RuntimeLegacyMovementSink movement_sink_for_key_sink(
    RuntimeLegacyKeySink key_sink) {
  return [key_sink = std::move(key_sink)](
             MovementCommand,
             uint32_t message,
             const RuntimeLegacyCommandContext&) {
    return key_sink && key_sink(message);
  };
}

} // namespace

std::optional<uint32_t> legacy_key_message_for_movement(
    MovementCommand command,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }

  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    switch (command) {
      case MovementCommand::north: return kArrowUpMessage;
      case MovementCommand::northeast: return kKeypadNineMessage;
      case MovementCommand::east: return kArrowRightMessage;
      case MovementCommand::southeast: return kKeypadThreeMessage;
      case MovementCommand::south: return kArrowDownMessage;
      case MovementCommand::southwest: return kKeypadOneMessage;
      case MovementCommand::west: return kArrowLeftMessage;
      case MovementCommand::northwest: return kKeypadSevenMessage;
      case MovementCommand::step_forward:
      case MovementCommand::step_backward:
      case MovementCommand::turn_left:
      case MovementCommand::turn_right:
        return std::nullopt;
    }
  }

  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    switch (command) {
      case MovementCommand::step_forward: return kArrowUpMessage;
      case MovementCommand::step_backward: return kArrowDownMessage;
      case MovementCommand::turn_left: return kArrowLeftMessage;
      case MovementCommand::turn_right: return kArrowRightMessage;
      case MovementCommand::north:
      case MovementCommand::northeast:
      case MovementCommand::east:
      case MovementCommand::southeast:
      case MovementCommand::south:
      case MovementCommand::southwest:
      case MovementCommand::west:
      case MovementCommand::northwest:
        return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<uint32_t> legacy_key_message_for_open_inventory(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return kOpenInventoryMessage;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return kOpenInventoryMessage;
  }
  return std::nullopt;
}

std::optional<uint32_t> legacy_key_message_for_open_spellbook(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return kOpenSpellbookMessage;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return kOpenSpellbookMessage;
  }
  return std::nullopt;
}

std::optional<uint32_t> legacy_key_message_for_open_scroll_case(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return kOpenOutdoorScrollCaseMessage;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return kOpenDungeonScrollCaseMessage;
  }
  return std::nullopt;
}

bool runtime_legacy_context_supports_open_character_sheet(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return false;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return true;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  return (context.screen == ScreenContext::dungeon) && dungeon_presentation;
}

bool runtime_legacy_context_supports_selected_item_drilldown(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return false;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return true;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  return (context.screen == ScreenContext::dungeon) && dungeon_presentation;
}

bool runtime_legacy_context_supports_contextual_world_entry(
    const ContextualWorldEntryAction& action,
    const RuntimeLegacyCommandContext& context) noexcept {
  switch (action.mode) {
    case ContextualWorldEntryMode::shop:
    case ContextualWorldEntryMode::temple:
    case ContextualWorldEntryMode::encounter:
      break;
    case ContextualWorldEntryMode::unavailable:
    default:
      return false;
  }
  if (!context.adaptive_eligible || context.in_camp ||
      (action.mode != context.contextual_world_entry_mode)) {
    return false;
  }
  const bool outdoor_presentation =
      (context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor);
  const bool dungeon_presentation =
      (context.screen == ScreenContext::dungeon) &&
      ((context.world_presentation == WorldPresentation::dungeon_map) ||
       (context.world_presentation ==
           WorldPresentation::dungeon_first_person));
  return outdoor_presentation || dungeon_presentation;
}

std::optional<uint32_t> legacy_key_message_for_contextual_world_entry(
    const ContextualWorldEntryAction& action,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!runtime_legacy_context_supports_contextual_world_entry(
          action, context)) {
    return std::nullopt;
  }
  switch (action.mode) {
    case ContextualWorldEntryMode::shop:
    case ContextualWorldEntryMode::temple:
      return kEnterShopOrTempleMessage;
    case ContextualWorldEntryMode::encounter:
      return kCheckLocalEncounterMessage;
    case ContextualWorldEntryMode::unavailable:
    default:
      return std::nullopt;
  }
}

std::optional<uint32_t> legacy_key_message_for_open_money_management(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return kOpenMoneyManagementMessage;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return kOpenMoneyManagementMessage;
  }
  return std::nullopt;
}

std::optional<uint32_t> legacy_key_message_for_rest_party(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || !context.in_camp) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return kRestPartyMessage;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return kRestPartyMessage;
  }
  return std::nullopt;
}

std::optional<uint32_t> legacy_key_message_for_set_camp_state(
    bool desired_in_camp,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible ||
      (context.in_camp == desired_in_camp)) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return kSetCampStateMessage;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return kSetCampStateMessage;
  }
  return std::nullopt;
}

bool runtime_legacy_context_supports_set_search_state(
    bool desired_searching,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible ||
      (context.searching == desired_searching)) {
    return false;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return true;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  return (context.screen == ScreenContext::dungeon) && dungeon_presentation;
}

bool runtime_legacy_context_supports_use_torch(
    const TorchSource& source,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (source.member > kMaximumPartyMemberId) ||
      (source.slot > 29) || !context.usable_torch_source ||
      (*context.usable_torch_source != source)) {
    return false;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return true;
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  return (context.screen == ScreenContext::dungeon) && dungeon_presentation;
}

std::optional<uint32_t> legacy_key_message_for_contextual_overview(
    const ContextualOverviewAction& action,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }
  const bool outdoor_presentation =
      (context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor);
  const bool dungeon_presentation =
      (context.screen == ScreenContext::dungeon) &&
      ((context.world_presentation == WorldPresentation::dungeon_map) ||
       (context.world_presentation ==
           WorldPresentation::dungeon_first_person));
  if (!outdoor_presentation && !dungeon_presentation) {
    return std::nullopt;
  }

  switch (action.mode) {
    case ContextualOverviewMode::area_search:
      if (context.in_camp || action.member) {
        return std::nullopt;
      }
      return kAreaSearchMessage;
    case ContextualOverviewMode::make_scroll:
      if (!context.in_camp || !action.member ||
          (*action.member > kMaximumPartyMemberId)) {
        return std::nullopt;
      }
      return kMakeScrollMessage;
  }
  return std::nullopt;
}

std::optional<RuntimeLegacyMenuCommand>
legacy_menu_command_for_open_save_game(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return RuntimeLegacyMenuCommand{
        .menu_id = kGameMenuId,
        .item_id = kSaveCurrentGameItemId,
    };
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return RuntimeLegacyMenuCommand{
        .menu_id = kGameMenuId,
        .item_id = kSaveCurrentGameItemId,
    };
  }
  return std::nullopt;
}

std::optional<RuntimeLegacyMenuCommand>
legacy_menu_command_for_open_load_game(
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible) {
    return std::nullopt;
  }
  if ((context.screen == ScreenContext::exploration) &&
      (context.world_presentation == WorldPresentation::outdoor)) {
    return RuntimeLegacyMenuCommand{
        .menu_id = kGameMenuId,
        .item_id = kRevertToPreviousGameItemId,
    };
  }
  const bool dungeon_presentation =
      (context.world_presentation == WorldPresentation::dungeon_map) ||
      (context.world_presentation == WorldPresentation::dungeon_first_person);
  if ((context.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return RuntimeLegacyMenuCommand{
        .menu_id = kGameMenuId,
        .item_id = kRevertToPreviousGameItemId,
    };
  }
  return std::nullopt;
}

std::optional<uint32_t> legacy_key_message_for_guard_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kGuardCombatantMessage;
}

std::optional<uint32_t> legacy_key_message_for_finish_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kFinishCombatantMessage;
}

std::optional<uint32_t> legacy_key_message_for_delay_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kDelayCombatantMessage;
}

std::optional<uint32_t> legacy_key_message_for_center_active_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kCenterActiveCombatantMessage;
}

std::optional<uint32_t> legacy_key_message_for_switch_weapon(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kSwitchWeaponMessage;
}

std::optional<uint32_t> legacy_key_message_for_cycle_combat_focus(
    CombatantId combatant,
    CombatFocusDirection direction,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  switch (direction) {
    case CombatFocusDirection::previous:
      return kCenterPreviousCombatantMessage;
    case CombatFocusDirection::next:
      return kCenterNextCombatantMessage;
  }
  return std::nullopt;
}

std::optional<uint32_t> legacy_key_message_for_open_combat_items(
    CombatantId combatant,
    PartyMemberId member,
    const RuntimeLegacyCommandContext& context) noexcept {
  // PartyMemberId is an unsigned byte, so every representable member value is
  // already in the stable wire range. Keep it in this signature because the
  // runtime sink must preserve the selected member independently from actor.
  (void)member;
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kOpenCombatItemsMessage;
}

std::optional<uint32_t> legacy_key_message_for_auto_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kAutoCombatantMessage;
}

std::optional<uint32_t> legacy_key_message_for_show_combat_range(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kShowCombatRangeMessage;
}

std::optional<uint32_t> legacy_key_message_for_bandage_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kBandageCombatantMessage;
}

std::optional<uint32_t> legacy_key_message_for_undo_combatant(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kUndoCombatantMessage;
}

std::optional<uint32_t> legacy_key_message_for_open_combat_spellbook(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kOpenCombatSpellbookMessage;
}

std::optional<uint32_t> legacy_key_message_for_open_combat_targeting(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kOpenCombatTargetingMessage;
}

std::optional<uint32_t> legacy_key_message_for_escape_combat(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kEscapeCombatMessage;
}

std::optional<uint32_t> legacy_key_message_for_open_combat_scroll_case(
    CombatantId combatant,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF)) {
    return std::nullopt;
  }
  return kOpenCombatScrollCaseMessage;
}

std::optional<uint32_t> legacy_key_message_for_center_combat_cursor(
    CombatantId combatant,
    CombatFieldCell cell,
    const RuntimeLegacyCommandContext& context) noexcept {
  if (!context.adaptive_eligible || (context.screen != ScreenContext::combat) ||
      (combatant < 0) || (combatant > 0xFF) || (cell.x > 89) ||
      (cell.y > 89)) {
    return std::nullopt;
  }
  return kCenterCombatCursorMessage;
}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyKeySink key_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          movement_sink_for_key_sink(std::move(key_sink)))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyKeySink key_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          movement_sink_for_key_sink(std::move(key_sink)),
          std::move(party_selection_sink))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider), std::move(movement_sink))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyNamedActionSinksTag&,
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyWorldActionSinks world_action_sinks,
    RuntimeLegacyCombatActionSinks combat_action_sinks)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(world_action_sinks),
          std::move(combat_action_sinks))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink,
    RuntimeLegacyCombatActionSinks combat_action_sinks)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink),
          std::move(combat_action_sinks))) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink,
    RuntimeLegacyGuardCombatantSink guard_combatant_sink)
    : RuntimeLegacyCommandBridge(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink),
          RuntimeLegacyCombatActionSinks{
              .guard_combatant = std::move(guard_combatant_sink),
          }) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink,
    RuntimeLegacyGuardCombatantSink guard_combatant_sink,
    RuntimeLegacyFinishCombatantSink finish_combatant_sink)
    : RuntimeLegacyCommandBridge(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink),
          RuntimeLegacyCombatActionSinks{
              .guard_combatant = std::move(guard_combatant_sink),
              .finish_combatant = std::move(finish_combatant_sink),
          }) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink,
    RuntimeLegacyGuardCombatantSink guard_combatant_sink,
    RuntimeLegacyFinishCombatantSink finish_combatant_sink,
    RuntimeLegacyDelayCombatantSink delay_combatant_sink)
    : RuntimeLegacyCommandBridge(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink),
          RuntimeLegacyCombatActionSinks{
              .guard_combatant = std::move(guard_combatant_sink),
              .finish_combatant = std::move(finish_combatant_sink),
              .delay_combatant = std::move(delay_combatant_sink),
          }) {}

RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge(
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
    RuntimeLegacyCenterActiveCombatantSink center_active_combatant_sink)
    : RuntimeLegacyCommandBridge(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink),
          RuntimeLegacyCombatActionSinks{
              .guard_combatant = std::move(guard_combatant_sink),
              .finish_combatant = std::move(finish_combatant_sink),
              .delay_combatant = std::move(delay_combatant_sink),
              .center_active_combatant =
                  std::move(center_active_combatant_sink),
          }) {}

DispatchResult RuntimeLegacyCommandBridge::dispatch(const UIAction& action) {
  return this->injected_bridge_.dispatch(action);
}

} // namespace realmz::presentation
