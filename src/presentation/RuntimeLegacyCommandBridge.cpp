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
constexpr uint32_t kGuardCombatantMessage = 0x00000567U;
constexpr uint32_t kFinishCombatantMessage = 0x00000366U;
constexpr int16_t kGameMenuId = 129;
constexpr int16_t kRevertToPreviousGameItemId = 2;
constexpr int16_t kSaveCurrentGameItemId = 3;

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
    RuntimeLegacyGuardCombatantSink guard_combatant_sink) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink),
      std::move(open_inventory_sink),
      std::move(open_spellbook_sink),
      std::move(open_save_game_sink),
      std::move(open_load_game_sink));
  handlers.guard_combatant = [
      context_provider = std::move(context_provider),
      guard_combatant_sink = std::move(guard_combatant_sink)](
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
  return handlers;
}

LegacyActionHandlers make_handlers(
    RuntimeLegacyContextProvider context_provider,
    RuntimeLegacyMovementSink movement_sink,
    RuntimeLegacyPartySelectionSink party_selection_sink,
    RuntimeLegacyOpenInventorySink open_inventory_sink,
    RuntimeLegacyOpenSpellbookSink open_spellbook_sink,
    RuntimeLegacyOpenSaveGameSink open_save_game_sink,
    RuntimeLegacyOpenLoadGameSink open_load_game_sink,
    RuntimeLegacyGuardCombatantSink guard_combatant_sink,
    RuntimeLegacyFinishCombatantSink finish_combatant_sink) {
  auto handlers = make_handlers(
      context_provider,
      std::move(movement_sink),
      std::move(party_selection_sink),
      std::move(open_inventory_sink),
      std::move(open_spellbook_sink),
      std::move(open_save_game_sink),
      std::move(open_load_game_sink),
      std::move(guard_combatant_sink));
  handlers.finish_combatant = [
      context_provider = std::move(context_provider),
      finish_combatant_sink = std::move(finish_combatant_sink)](
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
    RuntimeLegacyGuardCombatantSink guard_combatant_sink)
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink),
          std::move(guard_combatant_sink))) {}

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
    : injected_bridge_(make_handlers(
          std::move(context_provider),
          std::move(movement_sink),
          std::move(party_selection_sink),
          std::move(open_inventory_sink),
          std::move(open_spellbook_sink),
          std::move(open_save_game_sink),
          std::move(open_load_game_sink),
          std::move(guard_combatant_sink),
          std::move(finish_combatant_sink))) {}

DispatchResult RuntimeLegacyCommandBridge::dispatch(const UIAction& action) {
  return this->injected_bridge_.dispatch(action);
}

} // namespace realmz::presentation
