#include "PartyRailModel.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace realmz::presentation {
namespace {

constexpr int32_t kPartyTabStart = 100;
constexpr int32_t kActionTabStart = 1000;
constexpr int32_t kDrawerTabStart = 2000;

constexpr std::array<std::string_view, 40> kConditionLabels{
    "In Retreat",
    "Helpless",
    "Entangled",
    "Cursed",
    "Magic Aura",
    "Stupid",
    "Moving Slowly",
    "Shielded from Hits",
    "Missile Shield",
    "Poisoned",
    "Regenerating",
    "Fire Protection",
    "Cold Protection",
    "Electrical Protection",
    "Chemical Protection",
    "Psi Protection",
    "First-level Spell Protection",
    "Second-level Spell Protection",
    "Third-level Spell Protection",
    "Fourth-level Spell Protection",
    "Fifth-level Spell Protection",
    "Strong",
    "Protection from Foe",
    "Speedy",
    "Invisible",
    "Animated",
    "Turned to Stone",
    "Blind",
    "Diseased",
    "Confused",
    "Reflecting Spells",
    "Reflecting Attacks",
    "Bonus Damage",
    "Absorbing Energy",
    "Losing Energy",
    "Absorbing Spell Energy",
    "Hindered Attacks",
    "Hindered Defense",
    "Increased Defense",
    "Silenced",
};

std::string condition_label(int16_t condition) {
  if ((condition >= 0) &&
      (static_cast<size_t>(condition) < kConditionLabels.size())) {
    return std::string(kConditionLabels[static_cast<size_t>(condition)]);
  }
  return "Condition " + std::to_string(condition);
}

StateTokenModel token(
    std::string identifier,
    std::string label,
    StateEmphasis emphasis,
    StateMarker marker) {
  return {
      .identifier = std::move(identifier),
      .label = std::move(label),
      .emphasis = emphasis,
      .marker = marker,
  };
}

StateTokenModel unavailable_token(std::string label) {
  return token(
      "availability.unavailable",
      std::move(label),
      StateEmphasis::inactive,
      StateMarker::unavailable);
}

StateTokenModel engine_rules_token() {
  return token(
      "availability.engine_rules",
      "Game rules apply",
      StateEmphasis::information,
      StateMarker::information);
}

MeterModel meter_model(const MeterView& meter, std::string_view prefix) {
  MeterModel result{
      .current = meter.current,
      .maximum = meter.maximum,
  };

  if (meter.maximum <= 0) {
    result.state = token(
        std::string(prefix) + ".unavailable",
        "Not available",
        StateEmphasis::inactive,
        StateMarker::unavailable);
    return result;
  }

  result.fill_fraction = std::clamp(
      static_cast<double>(meter.current) /
          static_cast<double>(meter.maximum),
      0.0,
      1.0);
  if (meter.current <= 0) {
    result.state = token(
        std::string(prefix) + ".depleted",
        "Depleted",
        StateEmphasis::critical,
        StateMarker::stop);
  } else if (result.fill_fraction <= 0.25) {
    result.state = token(
        std::string(prefix) + ".critical",
        "Critical",
        StateEmphasis::critical,
        StateMarker::stop);
  } else if (result.fill_fraction <= 0.5) {
    result.state = token(
        std::string(prefix) + ".low",
        "Low",
        StateEmphasis::caution,
        StateMarker::alert);
  } else {
    result.state = token(
        std::string(prefix) + ".ready",
        "Ready",
        StateEmphasis::positive,
        StateMarker::check);
  }
  return result;
}

std::optional<PartyMemberId> resolved_selected_member(
    const PartyView& party) {
  if (party.selected_member && party.member(*party.selected_member)) {
    return party.selected_member;
  }
  const auto selected = std::ranges::find_if(
      party.members,
      [](const PartyMemberView& member) { return member.selected; });
  if (selected != party.members.end()) {
    return selected->id;
  }
  return std::nullopt;
}

std::vector<StateTokenModel> member_states(
    const PartyMemberView& member,
    bool selected,
    const MeterModel& stamina) {
  std::vector<StateTokenModel> result;
  if (selected) {
    result.emplace_back(token(
        "status.selected",
        "Selected",
        StateEmphasis::information,
        StateMarker::selection));
  }
  if (!member.conscious) {
    result.emplace_back(token(
        "status.unconscious",
        "Unconscious",
        StateEmphasis::critical,
        StateMarker::stop));
  } else if (stamina.state.identifier != "stamina.ready") {
    result.emplace_back(stamina.state);
  }

  auto condition_codes = member.conditions;
  std::ranges::sort(condition_codes);
  const auto unique_end = std::ranges::unique(condition_codes).begin();
  condition_codes.erase(unique_end, condition_codes.end());
  for (const auto condition : condition_codes) {
    result.emplace_back(token(
        "status.condition." + std::to_string(condition),
        condition_label(condition),
        StateEmphasis::caution,
        StateMarker::condition));
  }
  return result;
}

std::vector<StateTokenModel> selected_detail_states(
    const PartyRailMemberModel& member) {
  std::vector<StateTokenModel> result;
  result.reserve(member.states.size() + 1U);
  result.emplace_back(member.conscious
      ? token(
            "status.conscious",
            "Conscious",
            StateEmphasis::positive,
            StateMarker::check)
      : token(
            "status.unconscious",
            "Unconscious",
            StateEmphasis::critical,
            StateMarker::stop));

  // The member's presence in this model already communicates selection.
  // Retain every gameplay status while replacing that redundant token with an
  // explicit, non-color consciousness cue. member_states has already put
  // conditions in ascending numeric order and removed duplicate codes.
  for (const auto& state : member.states) {
    if ((state.identifier == "status.selected") ||
        (state.identifier == "status.conscious") ||
        (state.identifier == "status.unconscious")) {
      continue;
    }
    const auto duplicate = std::ranges::find(
        result,
        state.identifier,
        &StateTokenModel::identifier);
    if (duplicate == result.end()) {
      result.emplace_back(state);
    }
  }
  return result;
}

StateTokenModel fatigue_state(int16_t fatigue) {
  if (fatigue <= 0) {
    return token(
        "fatigue.rested",
        "Rested",
        StateEmphasis::positive,
        StateMarker::check);
  }
  return token(
      "fatigue.present",
      "Fatigued",
      StateEmphasis::caution,
      StateMarker::alert);
}

bool has_world_navigation(ScreenContext screen) noexcept {
  return (screen == ScreenContext::exploration) ||
      (screen == ScreenContext::dungeon);
}

ActionControlModel action(
    ActionIntent intent,
    std::string command,
    std::string label,
    ActionAvailability availability,
    int32_t tab_order,
    std::optional<StateTokenModel> reason = std::nullopt) {
  return {
      .intent = intent,
      .command = std::move(command),
      .label = std::move(label),
      .availability = availability,
      .availability_reason = std::move(reason),
      .focus_identifier = "focus.action." + std::to_string(tab_order),
      .tab_order = tab_order,
  };
}

std::vector<ActionControlModel> build_actions(
    const GameSnapshot& snapshot,
    std::optional<PartyMemberId> selected_member) {
  std::vector<ActionControlModel> result;
  int32_t tab_order = kActionTabStart;
  const bool encounter_active = snapshot.encounter && snapshot.encounter->active;

  const bool navigation_context = has_world_navigation(snapshot.screen) &&
      !encounter_active;
  result.emplace_back(action(
      ActionIntent::navigate,
      "action.navigate",
      "Navigate",
      navigation_context ? ActionAvailability::deferred_to_engine
                         : ActionAvailability::unavailable,
      tab_order++,
      navigation_context
          ? std::optional<StateTokenModel>{engine_rules_token()}
          : std::optional<StateTokenModel>{
                unavailable_token("Navigation is not available here")}));

  const PartyMemberView* selected = selected_member
      ? snapshot.party.member(*selected_member)
      : nullptr;
  result.emplace_back(action(
      ActionIntent::open_inventory,
      "action.inventory.open",
      "Inventory",
      selected ? ActionAvailability::deferred_to_engine
               : ActionAvailability::unavailable,
      tab_order++,
      selected
          ? std::optional<StateTokenModel>{engine_rules_token()}
          : std::optional<StateTokenModel>{
                unavailable_token("Select a party member first")}));
  result.back().party_member = selected_member;

  ActionAvailability cast_availability = ActionAvailability::deferred_to_engine;
  std::optional<StateTokenModel> cast_reason = engine_rules_token();
  if (!selected) {
    cast_availability = ActionAvailability::unavailable;
    cast_reason = unavailable_token("Select a party member first");
  } else if (!selected->conscious) {
    cast_availability = ActionAvailability::unavailable;
    cast_reason = unavailable_token("The selected member is unconscious");
  } else if (selected->spell_points.current <= 0) {
    cast_availability = ActionAvailability::unavailable;
    cast_reason = unavailable_token("No spell points remain");
  }
  result.emplace_back(action(
      ActionIntent::cast_spell,
      "action.spell.cast",
      "Cast spell",
      cast_availability,
      tab_order++,
      std::move(cast_reason)));
  result.back().party_member = selected_member;

  result.emplace_back(action(
      ActionIntent::save_game,
      "action.game.save",
      "Save",
      ActionAvailability::deferred_to_engine,
      tab_order++,
      engine_rules_token()));
  result.emplace_back(action(
      ActionIntent::load_game,
      "action.game.load",
      "Load",
      ActionAvailability::deferred_to_engine,
      tab_order++,
      engine_rules_token()));
  const bool can_rest = navigation_context && snapshot.world.in_camp;
  result.emplace_back(action(
      ActionIntent::rest,
      "action.party.rest",
      "Rest",
      can_rest ? ActionAvailability::deferred_to_engine
               : ActionAvailability::unavailable,
      tab_order++,
      can_rest
          ? std::optional<StateTokenModel>{engine_rules_token()}
          : std::optional<StateTokenModel>{unavailable_token(
                snapshot.world.in_camp ? "Rest is unavailable now"
                                       : "Camp first")}));
  result.emplace_back(action(
      ActionIntent::set_camp_state,
      "action.party.camp",
      snapshot.world.in_camp ? "Break camp" : "Camp",
      navigation_context ? ActionAvailability::deferred_to_engine
                         : ActionAvailability::unavailable,
      tab_order++,
      navigation_context
          ? std::optional<StateTokenModel>{engine_rules_token()}
          : std::optional<StateTokenModel>{
                unavailable_token("Camp is unavailable now")}));
  result.back().desired_in_camp = !snapshot.world.in_camp;

  result.emplace_back(action(
      ActionIntent::set_search_state,
      "action.party.search",
      snapshot.world.searching ? "Stop search" : "Search",
      navigation_context ? ActionAvailability::deferred_to_engine
                         : ActionAvailability::unavailable,
      tab_order++,
      navigation_context
          ? std::optional<StateTokenModel>{engine_rules_token()}
          : std::optional<StateTokenModel>{
                unavailable_token("Search is unavailable now")}));
  result.back().desired_searching = !snapshot.world.searching;

  const std::optional<TorchSource> torch_source =
      snapshot.world.usable_torch_source &&
          (snapshot.world.usable_torch_source->member < 6U) &&
          (snapshot.world.usable_torch_source->slot < 30U)
      ? snapshot.world.usable_torch_source
      : std::nullopt;
  const bool can_use_torch = navigation_context && torch_source.has_value();
  result.emplace_back(action(
      ActionIntent::use_torch,
      "action.party.torch",
      "Use torch",
      can_use_torch ? ActionAvailability::deferred_to_engine
                    : ActionAvailability::unavailable,
      tab_order++,
      can_use_torch
          ? std::optional<StateTokenModel>{engine_rules_token()}
          : std::optional<StateTokenModel>{unavailable_token(
                navigation_context ? "No usable torch"
                                   : "Torch use is unavailable now")}));
  result.back().torch_source = torch_source;

  ActionAvailability scroll_availability =
      ActionAvailability::deferred_to_engine;
  std::optional<StateTokenModel> scroll_reason = engine_rules_token();
  if (!selected) {
    scroll_availability = ActionAvailability::unavailable;
    scroll_reason = unavailable_token("Select a party member first");
  } else if (!navigation_context || !selected->use_scroll_available) {
    scroll_availability = ActionAvailability::unavailable;
    scroll_reason = unavailable_token("Scroll use is unavailable now");
  }
  result.emplace_back(action(
      ActionIntent::open_scroll_case,
      "action.scroll_case.open",
      "Use scroll",
      scroll_availability,
      tab_order++,
      std::move(scroll_reason)));
  result.back().party_member = selected_member;

  ActionAvailability character_sheet_availability =
      ActionAvailability::deferred_to_engine;
  std::optional<StateTokenModel> character_sheet_reason =
      engine_rules_token();
  if (!selected) {
    character_sheet_availability = ActionAvailability::unavailable;
    character_sheet_reason = unavailable_token("Select a party member first");
  } else if (!navigation_context) {
    character_sheet_availability = ActionAvailability::unavailable;
    character_sheet_reason =
        unavailable_token("Character sheet is unavailable now");
  }
  result.emplace_back(action(
      ActionIntent::open_character_sheet,
      "action.character_sheet.open",
      "Character",
      character_sheet_availability,
      tab_order++,
      std::move(character_sheet_reason)));
  result.back().party_member = selected_member;

  if (snapshot.screen == ScreenContext::combat) {
    const CombatantView* acting = nullptr;
    if (snapshot.combat && snapshot.combat->active &&
        snapshot.combat->acting_combatant) {
      const auto acting_id = *snapshot.combat->acting_combatant;
      const auto match = std::ranges::find(
          snapshot.combat->combatants,
          acting_id,
          &CombatantView::id);
      if (match != snapshot.combat->combatants.end()) {
        acting = &*match;
      }
    }
    const bool can_act = acting &&
        (acting->kind == CombatantKind::party_member) && acting->active &&
        acting->targetable && (acting->stamina.current > 0);
    const PartyMemberView* acting_party_member = nullptr;
    if (can_act && (acting->id >= 0) && (acting->id <= 0xFF)) {
      acting_party_member = snapshot.party.member(
          static_cast<PartyMemberId>(acting->id));
    }
    const bool can_delay = acting_party_member &&
        (acting_party_member->movement ==
            acting_party_member->movement_maximum);
    result.emplace_back(action(
        ActionIntent::guard,
        "action.combat.guard",
        "Guard",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::finish,
        "action.combat.finish",
        "Finish",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::delay,
        "action.combat.delay",
        "Delay",
        can_delay ? ActionAvailability::deferred_to_engine
                  : ActionAvailability::unavailable,
        tab_order++,
        can_delay
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{unavailable_token(
                  can_act ? "Delay is only available before moving"
                          : "Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::center_active,
        "action.combat.center",
        "Center",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::switch_weapon,
        "action.combat.weapon.switch",
        "Switch weapon",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::center_previous,
        "action.combat.center.previous",
        "Previous",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::center_next,
        "action.combat.center.next",
        "Next",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    const bool can_open_combat_items = can_act && selected;
    result.emplace_back(action(
        ActionIntent::combat_items,
        "action.combat.items",
        "Items",
        can_open_combat_items ? ActionAvailability::deferred_to_engine
                              : ActionAvailability::unavailable,
        tab_order++,
        can_open_combat_items
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{unavailable_token(
                  can_act ? "Select a party member first"
                          : "Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    if (selected) {
      result.back().party_member = selected->id;
    }
    result.emplace_back(action(
        ActionIntent::auto_combatant,
        "action.combat.auto",
        "Auto",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::show_combat_range,
        "action.combat.range",
        "Range",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    const bool can_bandage = can_act && snapshot.combat->bandage_available;
    result.emplace_back(action(
        ActionIntent::bandage_combatant,
        "action.combat.bandage",
        "Bandage",
        can_bandage ? ActionAvailability::deferred_to_engine
                    : ActionAvailability::unavailable,
        tab_order++,
        can_bandage
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{unavailable_token(
                  can_act ? "Bandage is unavailable now"
                          : "Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    const bool can_undo = can_act && snapshot.combat->undo_available;
    result.emplace_back(action(
        ActionIntent::undo_combatant,
        "action.combat.undo",
        "Undo",
        can_undo ? ActionAvailability::deferred_to_engine
                 : ActionAvailability::unavailable,
        tab_order++,
        can_undo
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{unavailable_token(
                  can_act ? "Undo is unavailable now"
                          : "Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    const bool can_open_combat_spellbook =
        can_act && snapshot.combat->cast_spell_available;
    result.emplace_back(action(
        ActionIntent::open_combat_spellbook,
        "action.combat.spellbook.open",
        "Cast spell",
        can_open_combat_spellbook
            ? ActionAvailability::deferred_to_engine
            : ActionAvailability::unavailable,
        tab_order++,
        can_open_combat_spellbook
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{unavailable_token(
                  can_act ? "Spell casting is unavailable now"
                          : "Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    const bool can_open_combat_targeting =
        can_act && snapshot.combat->target_available;
    result.emplace_back(action(
        ActionIntent::open_combat_targeting,
        "action.combat.targeting.open",
        "Target",
        can_open_combat_targeting
            ? ActionAvailability::deferred_to_engine
            : ActionAvailability::unavailable,
        tab_order++,
        can_open_combat_targeting
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{unavailable_token(
                  can_act ? "Targeting is unavailable now"
                          : "Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::escape_combat,
        "action.combat.escape",
        "Escape",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    const bool can_open_combat_scroll_case =
        can_act && snapshot.combat->use_scroll_available;
    result.emplace_back(action(
        ActionIntent::open_combat_scroll_case,
        "action.combat.scroll_case.open",
        "Use scroll",
        can_open_combat_scroll_case
            ? ActionAvailability::deferred_to_engine
            : ActionAvailability::unavailable,
        tab_order++,
        can_open_combat_scroll_case
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{unavailable_token(
                  can_act ? "Scroll use is unavailable now"
                          : "Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
    result.emplace_back(action(
        ActionIntent::center_combat_cursor,
        "action.combat.center.cursor",
        "Center Cursor",
        can_act ? ActionAvailability::deferred_to_engine
                : ActionAvailability::unavailable,
        tab_order++,
        can_act
            ? std::optional<StateTokenModel>{engine_rules_token()}
            : std::optional<StateTokenModel>{
                  unavailable_token("Wait for an active party member")}));
    if (can_act) {
      result.back().combatant = acting->id;
    }
  }

  if (encounter_active) {
    for (const auto& choice : snapshot.encounter->choices) {
      auto choice_action = action(
          ActionIntent::encounter_choice,
          "encounter.choice." + std::to_string(choice.id),
          choice.label,
          choice.enabled ? ActionAvailability::available
                         : ActionAvailability::unavailable,
          tab_order++,
          choice.enabled
              ? std::nullopt
              : std::optional<StateTokenModel>{
                    unavailable_token("This choice is unavailable")});
      choice_action.encounter_choice = choice.id;
      result.emplace_back(std::move(choice_action));
    }
    if (snapshot.encounter->can_cancel) {
      result.emplace_back(action(
          ActionIntent::cancel,
          "action.cancel",
          "Cancel",
          ActionAvailability::available,
          tab_order++));
    }
  }
  return result;
}

StateTokenModel message_state(MessageSeverity severity) {
  switch (severity) {
    case MessageSeverity::information:
      return token(
          "message.information",
          "Information",
          StateEmphasis::information,
          StateMarker::information);
    case MessageSeverity::success:
      return token(
          "message.success",
          "Success",
          StateEmphasis::positive,
          StateMarker::check);
    case MessageSeverity::warning:
      return token(
          "message.warning",
          "Warning",
          StateEmphasis::caution,
          StateMarker::alert);
    case MessageSeverity::error:
      return token(
          "message.error",
          "Error",
          StateEmphasis::critical,
          StateMarker::stop);
  }
  return token(
      "message.information",
      "Information",
      StateEmphasis::information,
      StateMarker::information);
}

EventLogModel build_event_log(
    std::span<const GameEvent> events,
    size_t limit) {
  EventLogModel result;
  for (const auto& event : events) {
    if (const auto* message = std::get_if<MessageEvent>(&event.payload)) {
      result.entries.emplace_back(EventLogEntryModel{
          .sequence = event.sequence,
          .severity = message->severity,
          .text = message->text,
          .state = message_state(message->severity),
      });
    }
  }
  std::ranges::stable_sort(
      result.entries,
      {},
      &EventLogEntryModel::sequence);
  if (result.entries.size() > limit) {
    result.entries.erase(
        result.entries.begin(),
        result.entries.end() - static_cast<std::ptrdiff_t>(limit));
  }
  for (const auto& entry : result.entries) {
    if (entry.severity == MessageSeverity::warning) {
      ++result.caution_count;
    } else if (entry.severity == MessageSeverity::error) {
      ++result.critical_count;
    }
  }
  return result;
}

TypographyModel typography(double requested_scale) {
  constexpr double kMinimumScale = 0.75;
  constexpr double kMaximumScale = 2.0;
  const double finite_scale = std::isfinite(requested_scale)
      ? requested_scale
      : 1.0;
  const double scale = std::clamp(
      finite_scale,
      kMinimumScale,
      kMaximumScale);
  const auto style = [scale](double points, double line_height) {
    return TextStyleModel{
        .point_size = points * scale,
        .line_height = line_height * scale,
    };
  };
  return {
      .scale = scale,
      .caption = style(13.0, 17.0),
      .body = style(16.0, 22.0),
      .heading = style(20.0, 26.0),
  };
}

SelectedPartyDetailsModel selected_details(
    const GameSnapshot& snapshot,
    const PartyRailModel& party_rail) {
  SelectedPartyDetailsModel result;
  if (!party_rail.selected_member) {
    return result;
  }
  const auto* member = snapshot.party.member(*party_rail.selected_member);
  if (!member) {
    return result;
  }
  const auto rail_member = std::ranges::find(
      party_rail.members,
      member->id,
      &PartyRailMemberModel::id);
  if (rail_member == party_rail.members.end()) {
    return result;
  }
  result.member = member->id;
  result.name = member->name;
  result.level = member->level;
  result.armor_class = member->armor_class;
  result.movement = member->movement;
  result.movement_maximum = member->movement_maximum;
  result.stamina = rail_member->stamina;
  result.spell_points = rail_member->spell_points;
  result.states = selected_detail_states(*rail_member);
  result.conscious = rail_member->conscious;
  return result;
}

DrawerModel build_drawers(
    const ShellViewPreferences& preferences,
    const EventLogModel& event_log) {
  DrawerModel result{
      .collapsed = preferences.panels_collapsed,
      .active_panel = preferences.panels_collapsed
          ? preferences.active_drawer
          : std::nullopt,
  };
  if (!result.collapsed) {
    return result;
  }
  const size_t log_badge = event_log.caution_count +
      event_log.critical_count;
  result.tabs = {
      DrawerTabModel{
          .panel = DrawerPanel::details,
          .label = "Details",
          .command = "drawer.details.toggle",
          .focus_identifier = "focus.drawer.details",
          .tab_order = kDrawerTabStart,
          .active = result.active_panel == DrawerPanel::details,
      },
      DrawerTabModel{
          .panel = DrawerPanel::event_log,
          .label = "Event log",
          .command = "drawer.event_log.toggle",
          .focus_identifier = "focus.drawer.event_log",
          .tab_order = kDrawerTabStart + 1,
          .badge_count = log_badge,
          .active = result.active_panel == DrawerPanel::event_log,
      },
  };
  return result;
}

std::vector<AnimationCueModel> build_animation_cues(
    std::span<const GameEvent> events,
    bool reduced_motion) {
  std::vector<AnimationCueModel> result;
  for (const auto& event : events) {
    if (const auto* cue = std::get_if<AnimationCueEvent>(&event.payload)) {
      result.emplace_back(AnimationCueModel{
          .sequence = event.sequence,
          .cue = cue->cue,
          .target = cue->target,
          .essential_motion = cue->essential_motion,
          .should_animate = cue->essential_motion || !reduced_motion,
      });
    }
  }
  std::ranges::stable_sort(
      result,
      {},
      &AnimationCueModel::sequence);
  return result;
}

std::vector<KeyboardTargetModel> build_keyboard_order(
    const PartyRailModel& party,
    const std::vector<ActionControlModel>& actions,
    const DrawerModel& drawers) {
  std::vector<KeyboardTargetModel> result;
  result.reserve(party.members.size() + actions.size() + drawers.tabs.size());
  for (const auto& member : party.members) {
    result.emplace_back(KeyboardTargetModel{
        .focus_identifier = member.focus_identifier,
        .tab_order = member.tab_order,
        .command = member.select_command,
    });
  }
  for (const auto& control : actions) {
    result.emplace_back(KeyboardTargetModel{
        .focus_identifier = control.focus_identifier,
        .tab_order = control.tab_order,
        .command = control.command,
        .enabled = control.can_invoke(),
    });
  }
  for (const auto& drawer : drawers.tabs) {
    result.emplace_back(KeyboardTargetModel{
        .focus_identifier = drawer.focus_identifier,
        .tab_order = drawer.tab_order,
        .command = drawer.command,
    });
  }
  std::ranges::stable_sort(result, {}, &KeyboardTargetModel::tab_order);
  return result;
}

} // namespace

PartyRailModel build_party_rail_model(const GameSnapshot& snapshot) {
  PartyRailModel result{
      .revision = snapshot.revision,
      .selected_member = resolved_selected_member(snapshot.party),
      .pooled_money = snapshot.party.pooled_money,
      .fatigue = snapshot.party.fatigue,
      .fatigue_state = fatigue_state(snapshot.party.fatigue),
  };
  result.members.reserve(snapshot.party.members.size());
  for (size_t index = 0; index < snapshot.party.members.size(); ++index) {
    const auto& member = snapshot.party.members[index];
    const bool selected = result.selected_member == member.id;
    auto stamina = meter_model(member.stamina, "stamina");
    result.members.emplace_back(PartyRailMemberModel{
        .id = member.id,
        .name = member.name,
        .level = member.level,
        .portrait_id = member.portrait_id,
        .stamina = stamina,
        .spell_points = meter_model(member.spell_points, "spell_points"),
        .states = member_states(member, selected, stamina),
        .selected = selected,
        .conscious = member.conscious,
        .focus_identifier = "focus.party.member." +
            std::to_string(member.id) + "." + std::to_string(index),
        .select_command = "party.select." + std::to_string(member.id),
        .tab_order = kPartyTabStart + static_cast<int32_t>(index),
    });
  }
  return result;
}

PresentationShellModel build_presentation_shell_model(
    const GameSnapshot& snapshot,
    std::span<const GameEvent> events,
    const ShellViewPreferences& preferences) {
  PresentationShellModel result;
  result.revision = snapshot.revision;
  result.screen = snapshot.screen;
  result.world_action_page =
      has_world_navigation(snapshot.screen) &&
          is_valid_world_action_page_transition(
              WorldActionPage::travel, preferences.world_action_page)
      ? preferences.world_action_page
      : WorldActionPage::travel;
  result.combat_action_page =
      (snapshot.screen == ScreenContext::combat) &&
          ((preferences.combat_action_page == CombatActionPage::primary) ||
              (preferences.combat_action_page == CombatActionPage::secondary) ||
              (preferences.combat_action_page == CombatActionPage::utility) ||
              (preferences.combat_action_page == CombatActionPage::special))
      ? preferences.combat_action_page
      : CombatActionPage::primary;
  result.party_rail = build_party_rail_model(snapshot);
  result.selected_details = selected_details(
      snapshot,
      result.party_rail);
  result.actions = build_actions(snapshot, result.party_rail.selected_member);
  result.event_log = build_event_log(events, preferences.event_log_limit);
  result.drawers = build_drawers(preferences, result.event_log);
  result.typography = typography(preferences.text_scale);
  result.motion = {
      .reduced_motion = preferences.reduced_motion,
      .allow_nonessential_motion = !preferences.reduced_motion,
  };
  result.animation_cues = build_animation_cues(
      events,
      preferences.reduced_motion);
  result.keyboard_tab_order = build_keyboard_order(
      result.party_rail,
      result.actions,
      result.drawers);
  return result;
}

} // namespace realmz::presentation
