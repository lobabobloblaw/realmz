#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "presentation/PartyRailModel.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

const ActionControlModel& action_with(
    const PresentationShellModel& model,
    ActionIntent intent) {
  for (const auto& candidate : model.actions) {
    if (candidate.intent == intent) {
      return candidate;
    }
  }
  throw std::runtime_error("action not found");
}

GameSnapshot sample_snapshot() {
  GameSnapshot snapshot;
  snapshot.revision = 77;
  snapshot.screen = ScreenContext::exploration;
  snapshot.party.selected_member = 2;
  snapshot.party.pooled_money = {15, 20, 25};
  snapshot.party.fatigue = 4;
  snapshot.party.members = {
      PartyMemberView{
          .id = 1,
          .name = "Arin",
          .level = 6,
          .portrait_id = 260,
          .stamina = {21, 24},
          .spell_points = {9, 12},
          .armor_class = 7,
          .movement = 4,
          .movement_maximum = 9,
          .conditions = {9},
      },
      PartyMemberView{
          .id = 2,
          .name = "Bryn",
          .level = 4,
          .portrait_id = 261,
          .stamina = {4, 20},
          .spell_points = {0, 5},
          .armor_class = 5,
          .movement = 2,
          .movement_maximum = 8,
          .conditions = {7, 3, 7},
          .selected = true,
          .conscious = false,
      },
  };
  snapshot.world.usable_torch_source = TorchSource{.member = 1, .slot = 7};
  return snapshot;
}

void test_party_rail_and_non_color_states() {
  const auto snapshot = sample_snapshot();
  const auto model = build_party_rail_model(snapshot);

  CHECK(model.revision == 77);
  CHECK(model.members.size() == 2);
  CHECK(model.selected_member == 2);
  CHECK((model.pooled_money == std::array<int32_t, 3>{15, 20, 25}));
  CHECK(model.fatigue_state.identifier == "fatigue.present");
  CHECK(model.fatigue_state.label == "Fatigued");
  CHECK(model.fatigue_state.marker == StateMarker::alert);
  CHECK(!model.members[0].selected);
  CHECK(model.members[1].selected);
  CHECK(model.members[1].stamina.fill_fraction == 0.2);
  CHECK(model.members[1].stamina.state.identifier == "stamina.critical");
  CHECK(model.members[1].states.size() == 4);
  CHECK(model.members[1].states[0].identifier == "status.selected");
  CHECK(model.members[1].states[0].marker == StateMarker::selection);
  CHECK(model.members[1].states[1].identifier == "status.unconscious");
  CHECK(model.members[1].states[1].label == "Unconscious");
  CHECK(model.members[1].states[2].identifier == "status.condition.3");
  CHECK(model.members[1].states[2].label == "Cursed");
  CHECK(model.members[1].states[3].identifier == "status.condition.7");
  CHECK(model.members[1].states[3].label == "Shielded from Hits");
  CHECK(model.members[0].states.back().label == "Poisoned");
  CHECK(model.members[1].select_command == "party.select.2");
  CHECK(model.members[0].focus_identifier != model.members[1].focus_identifier);
}

void test_selection_fallback_and_meter_bounds() {
  auto snapshot = sample_snapshot();
  snapshot.party.selected_member = 99;
  snapshot.party.members[0].selected = true;
  snapshot.party.members[0].stamina = {30, 20};
  snapshot.party.members[0].spell_points = {-4, 8};
  const auto model = build_party_rail_model(snapshot);

  CHECK(model.selected_member == 1);
  CHECK(model.members[0].selected);
  CHECK(!model.members[1].selected);
  CHECK(model.members[0].stamina.fill_fraction == 1.0);
  CHECK(model.members[0].spell_points.fill_fraction == 0.0);
  CHECK(model.members[0].spell_points.state.identifier ==
      "spell_points.depleted");

  snapshot.party.members[0].spell_points = {0, 0};
  CHECK(build_party_rail_model(snapshot)
            .members[0]
            .spell_points.state.identifier == "spell_points.unavailable");
}

void test_selected_details_retain_complete_member_status() {
  auto snapshot = sample_snapshot();
  auto shell = build_presentation_shell_model(snapshot);

  CHECK(shell.selected_details.member == 2);
  CHECK(shell.selected_details.name == "Bryn");
  CHECK(shell.selected_details.level == 4);
  CHECK(shell.selected_details.armor_class == 5);
  CHECK(shell.selected_details.movement == 2);
  CHECK(shell.selected_details.movement_maximum == 8);
  CHECK(shell.selected_details.stamina == shell.party_rail.members[1].stamina);
  CHECK(shell.selected_details.spell_points ==
      shell.party_rail.members[1].spell_points);
  CHECK(!shell.selected_details.conscious);
  CHECK(shell.selected_details.states.size() == 3U);
  CHECK(shell.selected_details.states[0].identifier == "status.unconscious");
  CHECK(shell.selected_details.states[0].marker == StateMarker::stop);
  CHECK(shell.selected_details.states[1].identifier == "status.condition.3");
  CHECK(shell.selected_details.states[2].identifier == "status.condition.7");
  CHECK(std::ranges::none_of(
      shell.selected_details.states,
      [](const StateTokenModel& candidate) {
        return candidate.identifier == "status.selected";
      }));

  snapshot.party.selected_member = 1;
  snapshot.party.members[0].selected = true;
  snapshot.party.members[1].selected = false;
  snapshot.party.members[0].conditions = {9, -1, 3, 9, -1};
  shell = build_presentation_shell_model(snapshot);
  CHECK(shell.selected_details.member == 1);
  CHECK(shell.selected_details.conscious);
  CHECK(shell.selected_details.stamina.current == 21);
  CHECK(shell.selected_details.stamina.maximum == 24);
  CHECK(shell.selected_details.spell_points.current == 9);
  CHECK(shell.selected_details.spell_points.maximum == 12);
  CHECK(shell.selected_details.states.size() == 4U);
  CHECK(shell.selected_details.states[0].identifier == "status.conscious");
  CHECK(shell.selected_details.states[0].label == "Conscious");
  CHECK(shell.selected_details.states[0].marker == StateMarker::check);
  CHECK(shell.selected_details.states[1].identifier == "status.condition.-1");
  CHECK(shell.selected_details.states[2].identifier == "status.condition.3");
  CHECK(shell.selected_details.states[3].identifier == "status.condition.9");

  snapshot.party.selected_member.reset();
  for (auto& member : snapshot.party.members) {
    member.selected = false;
  }
  shell = build_presentation_shell_model(snapshot);
  CHECK(!shell.selected_details.member);
  CHECK(shell.selected_details.name.empty());
  CHECK(shell.selected_details.states.empty());
  CHECK(shell.selected_details.stamina == MeterModel{});
  CHECK(shell.selected_details.spell_points == MeterModel{});
}

void test_action_availability_is_conservative() {
  auto snapshot = sample_snapshot();
  auto model = build_presentation_shell_model(snapshot);

  const auto& navigation = action_with(model, ActionIntent::navigate);
  CHECK(navigation.availability == ActionAvailability::deferred_to_engine);
  CHECK(navigation.can_invoke());
  CHECK(navigation.availability_reason->identifier ==
      "availability.engine_rules");
  const auto& inventory = action_with(model, ActionIntent::open_inventory);
  CHECK(inventory.can_invoke());
  CHECK(inventory.party_member == 2);
  const auto& casting = action_with(model, ActionIntent::cast_spell);
  CHECK(!casting.can_invoke());
  CHECK(casting.availability_reason->label ==
      "The selected member is unconscious");
  const auto& unavailable_scroll =
      action_with(model, ActionIntent::open_scroll_case);
  CHECK(!unavailable_scroll.can_invoke());
  CHECK(unavailable_scroll.party_member == 2);
  CHECK(unavailable_scroll.command == "action.scroll_case.open");
  CHECK(unavailable_scroll.label == "Use scroll");
  CHECK(unavailable_scroll.availability_reason->label ==
      "Scroll use is unavailable now");
  const auto& character_sheet =
      action_with(model, ActionIntent::open_character_sheet);
  CHECK(character_sheet.can_invoke());
  CHECK(character_sheet.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(character_sheet.party_member == 2);
  CHECK(character_sheet.command == "action.character_sheet.open");
  CHECK(character_sheet.label == "Character");
  CHECK(character_sheet.availability_reason->label == "Game rules apply");
  const auto& rest = action_with(model, ActionIntent::rest);
  CHECK(!rest.can_invoke());
  CHECK(rest.availability == ActionAvailability::unavailable);
  CHECK(rest.command == "action.party.rest");
  CHECK(rest.label == "Rest");
  CHECK(rest.availability_reason->label == "Camp first");
  const auto& make_camp =
      action_with(model, ActionIntent::set_camp_state);
  CHECK(make_camp.can_invoke());
  CHECK(make_camp.availability == ActionAvailability::deferred_to_engine);
  CHECK(make_camp.command == "action.party.camp");
  CHECK(make_camp.label == "Camp");
  CHECK(make_camp.desired_in_camp == true);
  CHECK(make_camp.availability_reason->label == "Game rules apply");
  const auto& start_search =
      action_with(model, ActionIntent::set_search_state);
  CHECK(start_search.can_invoke());
  CHECK(start_search.availability == ActionAvailability::deferred_to_engine);
  CHECK(start_search.command == "action.party.search");
  CHECK(start_search.label == "Search");
  CHECK(start_search.desired_searching == true);
  CHECK(start_search.tab_order == make_camp.tab_order + 1);
  CHECK(start_search.availability_reason->label == "Game rules apply");
  const auto& use_torch = action_with(model, ActionIntent::use_torch);
  CHECK(use_torch.can_invoke());
  CHECK(use_torch.availability == ActionAvailability::deferred_to_engine);
  CHECK(use_torch.command == "action.party.torch");
  CHECK(use_torch.label == "Use torch");
  CHECK((use_torch.torch_source ==
      std::optional<TorchSource>{TorchSource{.member = 1, .slot = 7}}));
  CHECK(use_torch.tab_order == start_search.tab_order + 1);
  CHECK(use_torch.availability_reason->label == "Game rules apply");
  const auto& area_search =
      action_with(model, ActionIntent::contextual_overview);
  CHECK(area_search.can_invoke());
  CHECK(area_search.availability == ActionAvailability::deferred_to_engine);
  CHECK(area_search.command == "action.party.overview");
  CHECK(area_search.label == "Area search");
  CHECK(area_search.contextual_overview_mode ==
      std::optional<ContextualOverviewMode>{
          ContextualOverviewMode::area_search});
  CHECK(!area_search.party_member);
  CHECK(area_search.tab_order == use_torch.tab_order + 1);
  CHECK(area_search.availability_reason->label == "Game rules apply");

  snapshot.world.usable_torch_source.reset();
  model = build_presentation_shell_model(snapshot);
  const auto& no_torch = action_with(model, ActionIntent::use_torch);
  CHECK(!no_torch.can_invoke());
  CHECK(!no_torch.torch_source);
  CHECK(no_torch.availability_reason->label == "No usable torch");
  snapshot.world.usable_torch_source = TorchSource{.member = 6, .slot = 0};
  model = build_presentation_shell_model(snapshot);
  CHECK(!action_with(model, ActionIntent::use_torch).can_invoke());
  CHECK(!action_with(model, ActionIntent::use_torch).torch_source);
  snapshot.world.usable_torch_source = TorchSource{.member = 5, .slot = 30};
  model = build_presentation_shell_model(snapshot);
  CHECK(!action_with(model, ActionIntent::use_torch).can_invoke());
  CHECK(!action_with(model, ActionIntent::use_torch).torch_source);
  snapshot.world.usable_torch_source = TorchSource{.member = 1, .slot = 7};

  snapshot.world.in_camp = true;
  snapshot.party.members[1].use_scroll_available = true;
  model = build_presentation_shell_model(snapshot);
  const auto& available_rest = action_with(model, ActionIntent::rest);
  CHECK(available_rest.can_invoke());
  CHECK(available_rest.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(available_rest.availability_reason->label == "Game rules apply");
  const auto& break_camp =
      action_with(model, ActionIntent::set_camp_state);
  CHECK(break_camp.can_invoke());
  CHECK(break_camp.availability == ActionAvailability::deferred_to_engine);
  CHECK(break_camp.command == "action.party.camp");
  CHECK(break_camp.label == "Break camp");
  CHECK(break_camp.desired_in_camp == false);
  CHECK(break_camp.tab_order == available_rest.tab_order + 1);
  const auto& search_while_camped =
      action_with(model, ActionIntent::set_search_state);
  CHECK(search_while_camped.can_invoke());
  CHECK(search_while_camped.desired_searching == true);
  CHECK(search_while_camped.tab_order == break_camp.tab_order + 1);
  const auto& torch_while_camped =
      action_with(model, ActionIntent::use_torch);
  CHECK(torch_while_camped.can_invoke());
  CHECK((torch_while_camped.torch_source ==
      std::optional<TorchSource>{TorchSource{.member = 1, .slot = 7}}));
  CHECK(torch_while_camped.tab_order == search_while_camped.tab_order + 1);
  const auto& make_scroll =
      action_with(model, ActionIntent::contextual_overview);
  CHECK(make_scroll.can_invoke());
  CHECK(make_scroll.availability == ActionAvailability::deferred_to_engine);
  CHECK(make_scroll.command == "action.party.overview");
  CHECK(make_scroll.label == "Make scroll");
  CHECK(make_scroll.contextual_overview_mode ==
      std::optional<ContextualOverviewMode>{
          ContextualOverviewMode::make_scroll});
  CHECK(make_scroll.party_member == 2);
  CHECK(make_scroll.tab_order == torch_while_camped.tab_order + 1);

  snapshot.party.members[1].use_scroll_available = false;
  model = build_presentation_shell_model(snapshot);
  const auto& unavailable_make_scroll =
      action_with(model, ActionIntent::contextual_overview);
  CHECK(!unavailable_make_scroll.can_invoke());
  CHECK(unavailable_make_scroll.party_member == 2);
  CHECK(unavailable_make_scroll.availability_reason->label ==
      "Make scroll is unavailable for the selected member");
  snapshot.party.members[1].use_scroll_available = true;

  snapshot.world.searching = true;
  model = build_presentation_shell_model(snapshot);
  const auto& stop_search =
      action_with(model, ActionIntent::set_search_state);
  CHECK(stop_search.can_invoke());
  CHECK(stop_search.label == "Stop search");
  CHECK(stop_search.desired_searching == false);
  CHECK(action_with(model, ActionIntent::use_torch).can_invoke());
  CHECK(action_with(model, ActionIntent::contextual_overview).can_invoke());

  snapshot.screen = ScreenContext::dungeon;
  snapshot.world.searching = false;
  model = build_presentation_shell_model(snapshot);
  const auto& dungeon_search =
      action_with(model, ActionIntent::set_search_state);
  CHECK(dungeon_search.can_invoke());
  CHECK(dungeon_search.desired_searching == true);
  CHECK(action_with(model, ActionIntent::rest).can_invoke());
  CHECK(action_with(model, ActionIntent::use_torch).can_invoke());
  CHECK(action_with(model, ActionIntent::contextual_overview).can_invoke());
  snapshot.screen = ScreenContext::exploration;
  snapshot.world.in_camp = false;
  snapshot.world.searching = false;

  snapshot.party.members[1].use_scroll_available = true;
  model = build_presentation_shell_model(snapshot);
  const auto& available_scroll =
      action_with(model, ActionIntent::open_scroll_case);
  CHECK(available_scroll.can_invoke());
  CHECK(available_scroll.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(available_scroll.party_member == 2);
  CHECK(available_scroll.availability_reason->label == "Game rules apply");
  snapshot.party.members[1].use_scroll_available = false;

  snapshot.party.selected_member.reset();
  for (auto& member : snapshot.party.members) {
    member.selected = false;
  }
  model = build_presentation_shell_model(snapshot);
  CHECK(!action_with(model, ActionIntent::open_inventory).can_invoke());
  CHECK(!action_with(model, ActionIntent::cast_spell).can_invoke());
  const auto& scroll_without_selection =
      action_with(model, ActionIntent::open_scroll_case);
  CHECK(!scroll_without_selection.can_invoke());
  CHECK(!scroll_without_selection.party_member);
  CHECK(scroll_without_selection.availability_reason->label ==
      "Select a party member first");
  const auto& character_without_selection =
      action_with(model, ActionIntent::open_character_sheet);
  CHECK(!character_without_selection.can_invoke());
  CHECK(!character_without_selection.party_member);
  CHECK(character_without_selection.availability_reason->label ==
      "Select a party member first");
  const auto& area_search_without_selection =
      action_with(model, ActionIntent::contextual_overview);
  CHECK(area_search_without_selection.can_invoke());
  CHECK(!area_search_without_selection.party_member);
  CHECK(area_search_without_selection.contextual_overview_mode ==
      std::optional<ContextualOverviewMode>{
          ContextualOverviewMode::area_search});

  snapshot.world.in_camp = true;
  model = build_presentation_shell_model(snapshot);
  const auto& make_scroll_without_selection =
      action_with(model, ActionIntent::contextual_overview);
  CHECK(!make_scroll_without_selection.can_invoke());
  CHECK(!make_scroll_without_selection.party_member);
  CHECK(make_scroll_without_selection.contextual_overview_mode ==
      std::optional<ContextualOverviewMode>{
          ContextualOverviewMode::make_scroll});
  CHECK(make_scroll_without_selection.availability_reason->label ==
      "Select a party member first");
  snapshot.world.in_camp = false;

  snapshot.screen = ScreenContext::encounter;
  snapshot.encounter = EncounterView{
      .active = true,
      .encounter_id = 8,
      .prompt = "Choose",
      .choices = {
          EncounterChoiceView{.id = 11, .label = "Accept"},
          EncounterChoiceView{.id = 12, .label = "Refuse", .enabled = false},
      },
      .can_cancel = true,
  };
  model = build_presentation_shell_model(snapshot);
  CHECK(!action_with(model, ActionIntent::navigate).can_invoke());
  const auto& character_in_encounter =
      action_with(model, ActionIntent::open_character_sheet);
  CHECK(!character_in_encounter.can_invoke());
  CHECK(character_in_encounter.availability_reason->label ==
      "Select a party member first");
  CHECK(action_with(model, ActionIntent::rest)
            .availability_reason->label == "Camp first");
  const auto& camp_in_encounter =
      action_with(model, ActionIntent::set_camp_state);
  CHECK(!camp_in_encounter.can_invoke());
  CHECK(camp_in_encounter.desired_in_camp == true);
  CHECK(camp_in_encounter.availability_reason->label ==
      "Camp is unavailable now");
  const auto& search_in_encounter =
      action_with(model, ActionIntent::set_search_state);
  CHECK(!search_in_encounter.can_invoke());
  CHECK(search_in_encounter.desired_searching == true);
  CHECK(search_in_encounter.availability_reason->label ==
      "Search is unavailable now");
  const auto& torch_in_encounter =
      action_with(model, ActionIntent::use_torch);
  CHECK(!torch_in_encounter.can_invoke());
  CHECK((torch_in_encounter.torch_source ==
      std::optional<TorchSource>{TorchSource{.member = 1, .slot = 7}}));
  CHECK(torch_in_encounter.availability_reason->label ==
      "Torch use is unavailable now");
  const auto& overview_in_encounter =
      action_with(model, ActionIntent::contextual_overview);
  CHECK(!overview_in_encounter.can_invoke());
  CHECK(overview_in_encounter.contextual_overview_mode ==
      std::optional<ContextualOverviewMode>{
          ContextualOverviewMode::area_search});
  CHECK(overview_in_encounter.availability_reason->label ==
      "Area search is unavailable now");
  CHECK(model.actions.size() == 15);
  CHECK(model.actions[12].command == "encounter.choice.11");
  CHECK(model.actions[12].can_invoke());
  CHECK(model.actions[13].command == "encounter.choice.12");
  CHECK(!model.actions[13].can_invoke());
  CHECK(model.actions[14].intent == ActionIntent::cancel);
  CHECK(model.actions[14].can_invoke());
}

void test_world_action_page_preferences_are_normalized() {
  auto snapshot = sample_snapshot();
  CHECK(build_presentation_shell_model(snapshot).world_action_page ==
      WorldActionPage::travel);

  for (const auto page : {
           WorldActionPage::travel,
           WorldActionPage::party,
           WorldActionPage::game,
       }) {
    const auto exploration = build_presentation_shell_model(
        snapshot,
        {},
        ShellViewPreferences{.world_action_page = page});
    CHECK(exploration.world_action_page == page);

    snapshot.screen = ScreenContext::dungeon;
    const auto dungeon = build_presentation_shell_model(
        snapshot,
        {},
        ShellViewPreferences{.world_action_page = page});
    CHECK(dungeon.world_action_page == page);
    snapshot.screen = ScreenContext::exploration;
  }

  const auto invalid = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{
          .world_action_page = static_cast<WorldActionPage>(255),
      });
  CHECK(invalid.world_action_page == WorldActionPage::travel);

  for (const auto screen : {
           ScreenContext::title,
           ScreenContext::combat,
           ScreenContext::inventory,
           ScreenContext::encounter,
       }) {
    snapshot.screen = screen;
    const auto non_world = build_presentation_shell_model(
        snapshot,
        {},
        ShellViewPreferences{.world_action_page = WorldActionPage::game});
    CHECK(non_world.world_action_page == WorldActionPage::travel);
  }
}

void test_combat_actions_track_the_active_party_combatant() {
  auto snapshot = sample_snapshot();
  snapshot.screen = ScreenContext::combat;
  snapshot.combat = CombatView{
      .active = true,
      .bandage_available = true,
      .undo_available = true,
      .cast_spell_available = true,
      .target_available = true,
      .use_scroll_available = true,
      .round = 4,
      .acting_combatant = 1,
      .combatants = {
          CombatantView{
              .id = 1,
              .kind = CombatantKind::party_member,
              .name = "Arin",
              .stamina = {12, 20},
              .active = true,
              .targetable = true,
          },
          CombatantView{
              .id = 10,
              .kind = CombatantKind::monster,
              .name = "Goblin",
              .stamina = {8, 8},
              .targetable = true,
          },
      },
  };

  auto model = build_presentation_shell_model(snapshot);
  CHECK(model.actions.size() == 29U);
  const auto& camp_in_combat =
      action_with(model, ActionIntent::set_camp_state);
  CHECK(!camp_in_combat.can_invoke());
  CHECK(camp_in_combat.desired_in_camp == true);
  CHECK(camp_in_combat.availability_reason->label ==
      "Camp is unavailable now");
  const auto& search_in_combat =
      action_with(model, ActionIntent::set_search_state);
  CHECK(!search_in_combat.can_invoke());
  CHECK(search_in_combat.desired_searching == true);
  CHECK(search_in_combat.availability_reason->label ==
      "Search is unavailable now");
  const auto& torch_in_combat =
      action_with(model, ActionIntent::use_torch);
  CHECK(!torch_in_combat.can_invoke());
  CHECK((torch_in_combat.torch_source ==
      std::optional<TorchSource>{TorchSource{.member = 1, .slot = 7}}));
  CHECK(torch_in_combat.availability_reason->label ==
      "Torch use is unavailable now");
  const auto& overview_in_combat =
      action_with(model, ActionIntent::contextual_overview);
  CHECK(!overview_in_combat.can_invoke());
  CHECK(overview_in_combat.contextual_overview_mode ==
      std::optional<ContextualOverviewMode>{
          ContextualOverviewMode::area_search});
  CHECK(!overview_in_combat.party_member);
  CHECK(overview_in_combat.availability_reason->label ==
      "Area search is unavailable now");
  const auto& noncombat_scroll =
      action_with(model, ActionIntent::open_scroll_case);
  CHECK(!noncombat_scroll.can_invoke());
  CHECK(noncombat_scroll.party_member == 2);
  CHECK(noncombat_scroll.availability_reason->label ==
      "Scroll use is unavailable now");
  const auto& noncombat_character_sheet =
      action_with(model, ActionIntent::open_character_sheet);
  CHECK(!noncombat_character_sheet.can_invoke());
  CHECK(noncombat_character_sheet.party_member == 2);
  CHECK(noncombat_character_sheet.availability_reason->label ==
      "Character sheet is unavailable now");
  const auto& guard = action_with(model, ActionIntent::guard);
  CHECK(guard.can_invoke());
  CHECK(guard.availability == ActionAvailability::deferred_to_engine);
  CHECK(guard.command == "action.combat.guard");
  CHECK(guard.combatant == 1);
  const auto& finish = action_with(model, ActionIntent::finish);
  CHECK(finish.can_invoke());
  CHECK(finish.availability == ActionAvailability::deferred_to_engine);
  CHECK(finish.command == "action.combat.finish");
  CHECK(finish.combatant == guard.combatant);
  CHECK(finish.tab_order == guard.tab_order + 1);
  CHECK(finish.focus_identifier != guard.focus_identifier);
  const auto& delay = action_with(model, ActionIntent::delay);
  CHECK(!delay.can_invoke());
  CHECK(delay.availability == ActionAvailability::unavailable);
  CHECK(delay.command == "action.combat.delay");
  CHECK(delay.combatant == guard.combatant);
  CHECK(delay.tab_order == finish.tab_order + 1);
  CHECK(delay.focus_identifier != finish.focus_identifier);
  CHECK(delay.availability_reason->label ==
      "Delay is only available before moving");
  const auto& center = action_with(model, ActionIntent::center_active);
  CHECK(center.can_invoke());
  CHECK(center.availability == ActionAvailability::deferred_to_engine);
  CHECK(center.command == "action.combat.center");
  CHECK(center.combatant == guard.combatant);
  CHECK(center.tab_order == delay.tab_order + 1);
  CHECK(center.focus_identifier != delay.focus_identifier);
  CHECK(center.availability_reason->label == "Game rules apply");
  const auto& weapon = action_with(model, ActionIntent::switch_weapon);
  CHECK(weapon.can_invoke());
  CHECK(weapon.availability == ActionAvailability::deferred_to_engine);
  CHECK(weapon.command == "action.combat.weapon.switch");
  CHECK(weapon.combatant == guard.combatant);
  CHECK(weapon.tab_order == center.tab_order + 1);
  CHECK(weapon.focus_identifier != center.focus_identifier);
  CHECK(weapon.availability_reason->label == "Game rules apply");
  const auto& previous = action_with(model, ActionIntent::center_previous);
  CHECK(previous.can_invoke());
  CHECK(previous.availability == ActionAvailability::deferred_to_engine);
  CHECK(previous.command == "action.combat.center.previous");
  CHECK(previous.label == "Previous");
  CHECK(previous.combatant == guard.combatant);
  CHECK(previous.tab_order == weapon.tab_order + 1);
  CHECK(previous.focus_identifier != weapon.focus_identifier);
  CHECK(previous.availability_reason->label == "Game rules apply");
  const auto& next = action_with(model, ActionIntent::center_next);
  CHECK(next.can_invoke());
  CHECK(next.availability == ActionAvailability::deferred_to_engine);
  CHECK(next.command == "action.combat.center.next");
  CHECK(next.label == "Next");
  CHECK(next.combatant == guard.combatant);
  CHECK(next.tab_order == previous.tab_order + 1);
  CHECK(next.focus_identifier != previous.focus_identifier);
  CHECK(next.availability_reason->label == "Game rules apply");
  const auto& combat_items = action_with(model, ActionIntent::combat_items);
  CHECK(combat_items.can_invoke());
  CHECK(combat_items.availability == ActionAvailability::deferred_to_engine);
  CHECK(combat_items.command == "action.combat.items");
  CHECK(combat_items.label == "Items");
  CHECK(combat_items.combatant == guard.combatant);
  CHECK(combat_items.party_member == 2);
  CHECK(combat_items.combatant != combat_items.party_member);
  CHECK(combat_items.tab_order == next.tab_order + 1);
  CHECK(combat_items.focus_identifier != next.focus_identifier);
  CHECK(combat_items.availability_reason->label == "Game rules apply");
  const auto& auto_combatant =
      action_with(model, ActionIntent::auto_combatant);
  CHECK(auto_combatant.can_invoke());
  CHECK(auto_combatant.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(auto_combatant.command == "action.combat.auto");
  CHECK(auto_combatant.label == "Auto");
  CHECK(auto_combatant.combatant == guard.combatant);
  CHECK(auto_combatant.tab_order == combat_items.tab_order + 1);
  CHECK(auto_combatant.focus_identifier !=
      combat_items.focus_identifier);
  CHECK(auto_combatant.availability_reason->label == "Game rules apply");
  const auto& combat_range =
      action_with(model, ActionIntent::show_combat_range);
  CHECK(combat_range.can_invoke());
  CHECK(combat_range.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(combat_range.command == "action.combat.range");
  CHECK(combat_range.label == "Range");
  CHECK(combat_range.combatant == guard.combatant);
  CHECK(combat_range.tab_order == auto_combatant.tab_order + 1);
  CHECK(combat_range.focus_identifier != auto_combatant.focus_identifier);
  CHECK(combat_range.availability_reason->label == "Game rules apply");
  const auto& bandage =
      action_with(model, ActionIntent::bandage_combatant);
  CHECK(bandage.can_invoke());
  CHECK(bandage.availability == ActionAvailability::deferred_to_engine);
  CHECK(bandage.command == "action.combat.bandage");
  CHECK(bandage.label == "Bandage");
  CHECK(bandage.combatant == guard.combatant);
  CHECK(bandage.tab_order == combat_range.tab_order + 1);
  CHECK(bandage.focus_identifier != combat_range.focus_identifier);
  CHECK(bandage.availability_reason->label == "Game rules apply");
  const auto& undo = action_with(model, ActionIntent::undo_combatant);
  CHECK(undo.can_invoke());
  CHECK(undo.availability == ActionAvailability::deferred_to_engine);
  CHECK(undo.command == "action.combat.undo");
  CHECK(undo.label == "Undo");
  CHECK(undo.combatant == guard.combatant);
  CHECK(undo.tab_order == bandage.tab_order + 1);
  CHECK(undo.focus_identifier != bandage.focus_identifier);
  CHECK(undo.availability_reason->label == "Game rules apply");
  const auto& combat_spellbook =
      action_with(model, ActionIntent::open_combat_spellbook);
  CHECK(combat_spellbook.can_invoke());
  CHECK(combat_spellbook.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(combat_spellbook.command == "action.combat.spellbook.open");
  CHECK(combat_spellbook.label == "Cast spell");
  CHECK(combat_spellbook.combatant == guard.combatant);
  CHECK(combat_spellbook.tab_order == undo.tab_order + 1);
  CHECK(combat_spellbook.focus_identifier != undo.focus_identifier);
  CHECK(combat_spellbook.availability_reason->label == "Game rules apply");
  const auto& combat_targeting =
      action_with(model, ActionIntent::open_combat_targeting);
  CHECK(combat_targeting.can_invoke());
  CHECK(combat_targeting.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(combat_targeting.command == "action.combat.targeting.open");
  CHECK(combat_targeting.label == "Target");
  CHECK(combat_targeting.combatant == guard.combatant);
  CHECK(combat_targeting.tab_order == combat_spellbook.tab_order + 1);
  CHECK(combat_targeting.focus_identifier !=
      combat_spellbook.focus_identifier);
  CHECK(combat_targeting.availability_reason->label == "Game rules apply");
  const auto& escape_combat =
      action_with(model, ActionIntent::escape_combat);
  CHECK(escape_combat.can_invoke());
  CHECK(escape_combat.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(escape_combat.command == "action.combat.escape");
  CHECK(escape_combat.label == "Escape");
  CHECK(escape_combat.combatant == guard.combatant);
  CHECK(escape_combat.tab_order == combat_targeting.tab_order + 1);
  CHECK(escape_combat.focus_identifier !=
      combat_targeting.focus_identifier);
  CHECK(escape_combat.availability_reason->label == "Game rules apply");
  const auto& combat_scroll_case =
      action_with(model, ActionIntent::open_combat_scroll_case);
  CHECK(combat_scroll_case.can_invoke());
  CHECK(combat_scroll_case.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(combat_scroll_case.command == "action.combat.scroll_case.open");
  CHECK(combat_scroll_case.label == "Use scroll");
  CHECK(combat_scroll_case.combatant == guard.combatant);
  CHECK(combat_scroll_case.tab_order == escape_combat.tab_order + 1);
  CHECK(combat_scroll_case.focus_identifier !=
      escape_combat.focus_identifier);
  CHECK(combat_scroll_case.availability_reason->label == "Game rules apply");
  const auto& center_combat_cursor =
      action_with(model, ActionIntent::center_combat_cursor);
  CHECK(center_combat_cursor.can_invoke());
  CHECK(center_combat_cursor.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(center_combat_cursor.command == "action.combat.center.cursor");
  CHECK(center_combat_cursor.label == "Center Cursor");
  CHECK(center_combat_cursor.combatant == guard.combatant);
  CHECK(center_combat_cursor.tab_order == combat_scroll_case.tab_order + 1);
  CHECK(center_combat_cursor.focus_identifier !=
      combat_scroll_case.focus_identifier);
  CHECK(center_combat_cursor.availability_reason->label ==
      "Game rules apply");
  CHECK(model.combat_action_page == CombatActionPage::primary);

  const auto secondary_page_model = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{
          .combat_action_page = CombatActionPage::secondary,
      });
  CHECK(secondary_page_model.combat_action_page ==
      CombatActionPage::secondary);
  const auto utility_page_model = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{
          .combat_action_page = CombatActionPage::utility,
      });
  CHECK(utility_page_model.combat_action_page ==
      CombatActionPage::utility);
  const auto special_page_model = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{
          .combat_action_page = CombatActionPage::special,
      });
  CHECK(special_page_model.combat_action_page ==
      CombatActionPage::special);

  snapshot.combat->bandage_available = false;
  model = build_presentation_shell_model(snapshot);
  const auto& unavailable_bandage =
      action_with(model, ActionIntent::bandage_combatant);
  CHECK(!unavailable_bandage.can_invoke());
  CHECK(unavailable_bandage.availability == ActionAvailability::unavailable);
  CHECK(unavailable_bandage.combatant == 1);
  CHECK(unavailable_bandage.availability_reason->label ==
      "Bandage is unavailable now");
  snapshot.combat->bandage_available = true;

  snapshot.combat->undo_available = false;
  model = build_presentation_shell_model(snapshot);
  const auto& unavailable_undo =
      action_with(model, ActionIntent::undo_combatant);
  CHECK(!unavailable_undo.can_invoke());
  CHECK(unavailable_undo.availability == ActionAvailability::unavailable);
  CHECK(unavailable_undo.combatant == 1);
  CHECK(unavailable_undo.availability_reason->label ==
      "Undo is unavailable now");
  snapshot.combat->undo_available = true;

  snapshot.combat->cast_spell_available = false;
  model = build_presentation_shell_model(snapshot);
  const auto& unavailable_combat_spellbook =
      action_with(model, ActionIntent::open_combat_spellbook);
  CHECK(!unavailable_combat_spellbook.can_invoke());
  CHECK(unavailable_combat_spellbook.availability ==
      ActionAvailability::unavailable);
  CHECK(unavailable_combat_spellbook.combatant == 1);
  CHECK(unavailable_combat_spellbook.availability_reason->label ==
      "Spell casting is unavailable now");
  snapshot.combat->cast_spell_available = true;

  snapshot.combat->target_available = false;
  model = build_presentation_shell_model(snapshot);
  const auto& unavailable_combat_targeting =
      action_with(model, ActionIntent::open_combat_targeting);
  CHECK(!unavailable_combat_targeting.can_invoke());
  CHECK(unavailable_combat_targeting.availability ==
      ActionAvailability::unavailable);
  CHECK(unavailable_combat_targeting.combatant == 1);
  CHECK(unavailable_combat_targeting.availability_reason->label ==
      "Targeting is unavailable now");
  snapshot.combat->target_available = true;

  snapshot.combat->use_scroll_available = false;
  model = build_presentation_shell_model(snapshot);
  const auto& unavailable_combat_scroll_case =
      action_with(model, ActionIntent::open_combat_scroll_case);
  CHECK(!unavailable_combat_scroll_case.can_invoke());
  CHECK(unavailable_combat_scroll_case.availability ==
      ActionAvailability::unavailable);
  CHECK(unavailable_combat_scroll_case.combatant == 1);
  CHECK(unavailable_combat_scroll_case.availability_reason->label ==
      "Scroll use is unavailable now");
  snapshot.combat->use_scroll_available = true;

  snapshot.party.members[0].movement =
      snapshot.party.members[0].movement_maximum;
  model = build_presentation_shell_model(snapshot);
  const auto& available_delay = action_with(model, ActionIntent::delay);
  CHECK(available_delay.can_invoke());
  CHECK(available_delay.availability ==
      ActionAvailability::deferred_to_engine);
  CHECK(available_delay.combatant == 1);
  CHECK(available_delay.availability_reason->label == "Game rules apply");
  const auto& center_before_movement =
      action_with(model, ActionIntent::center_active);
  CHECK(center_before_movement.can_invoke());
  CHECK(center_before_movement.combatant == available_delay.combatant);
  const auto& weapon_before_movement =
      action_with(model, ActionIntent::switch_weapon);
  CHECK(weapon_before_movement.can_invoke());
  CHECK(weapon_before_movement.combatant == available_delay.combatant);
  const auto& previous_before_movement =
      action_with(model, ActionIntent::center_previous);
  CHECK(previous_before_movement.can_invoke());
  CHECK(previous_before_movement.combatant == available_delay.combatant);
  const auto& next_before_movement =
      action_with(model, ActionIntent::center_next);
  CHECK(next_before_movement.can_invoke());
  CHECK(next_before_movement.combatant == available_delay.combatant);
  const auto& items_before_movement =
      action_with(model, ActionIntent::combat_items);
  CHECK(items_before_movement.can_invoke());
  CHECK(items_before_movement.combatant == available_delay.combatant);
  CHECK(items_before_movement.party_member == 2);
  const auto& auto_before_movement =
      action_with(model, ActionIntent::auto_combatant);
  CHECK(auto_before_movement.can_invoke());
  CHECK(auto_before_movement.combatant == available_delay.combatant);
  const auto& range_before_movement =
      action_with(model, ActionIntent::show_combat_range);
  CHECK(range_before_movement.can_invoke());
  CHECK(range_before_movement.combatant == available_delay.combatant);
  const auto& bandage_before_movement =
      action_with(model, ActionIntent::bandage_combatant);
  CHECK(bandage_before_movement.can_invoke());
  CHECK(bandage_before_movement.combatant == available_delay.combatant);
  const auto& undo_before_movement =
      action_with(model, ActionIntent::undo_combatant);
  CHECK(undo_before_movement.can_invoke());
  CHECK(undo_before_movement.combatant == available_delay.combatant);
  const auto& combat_spellbook_before_movement =
      action_with(model, ActionIntent::open_combat_spellbook);
  CHECK(combat_spellbook_before_movement.can_invoke());
  CHECK(combat_spellbook_before_movement.combatant ==
      available_delay.combatant);
  const auto& combat_targeting_before_movement =
      action_with(model, ActionIntent::open_combat_targeting);
  CHECK(combat_targeting_before_movement.can_invoke());
  CHECK(combat_targeting_before_movement.combatant ==
      available_delay.combatant);
  const auto& escape_before_movement =
      action_with(model, ActionIntent::escape_combat);
  CHECK(escape_before_movement.can_invoke());
  CHECK(escape_before_movement.combatant == available_delay.combatant);
  const auto& scroll_before_movement =
      action_with(model, ActionIntent::open_combat_scroll_case);
  CHECK(scroll_before_movement.can_invoke());
  CHECK(scroll_before_movement.combatant == available_delay.combatant);
  const auto& cursor_before_movement =
      action_with(model, ActionIntent::center_combat_cursor);
  CHECK(cursor_before_movement.can_invoke());
  CHECK(cursor_before_movement.combatant == available_delay.combatant);

  snapshot.party.members.erase(snapshot.party.members.begin());
  model = build_presentation_shell_model(snapshot);
  const auto& unmatched_delay = action_with(model, ActionIntent::delay);
  CHECK(!unmatched_delay.can_invoke());
  CHECK(unmatched_delay.combatant == 1);
  CHECK(unmatched_delay.availability_reason->label ==
      "Delay is only available before moving");
  CHECK(action_with(model, ActionIntent::guard).can_invoke());
  CHECK(action_with(model, ActionIntent::finish).can_invoke());
  const auto& unmatched_center =
      action_with(model, ActionIntent::center_active);
  CHECK(unmatched_center.can_invoke());
  CHECK(unmatched_center.combatant == unmatched_delay.combatant);
  const auto& unmatched_weapon =
      action_with(model, ActionIntent::switch_weapon);
  CHECK(unmatched_weapon.can_invoke());
  CHECK(unmatched_weapon.combatant == unmatched_delay.combatant);
  const auto& unmatched_previous =
      action_with(model, ActionIntent::center_previous);
  CHECK(unmatched_previous.can_invoke());
  CHECK(unmatched_previous.combatant == unmatched_delay.combatant);
  const auto& unmatched_next =
      action_with(model, ActionIntent::center_next);
  CHECK(unmatched_next.can_invoke());
  CHECK(unmatched_next.combatant == unmatched_delay.combatant);
  const auto& unmatched_items =
      action_with(model, ActionIntent::combat_items);
  CHECK(unmatched_items.can_invoke());
  CHECK(unmatched_items.combatant == unmatched_delay.combatant);
  CHECK(unmatched_items.party_member == 2);
  const auto& unmatched_auto =
      action_with(model, ActionIntent::auto_combatant);
  CHECK(unmatched_auto.can_invoke());
  CHECK(unmatched_auto.combatant == unmatched_delay.combatant);
  const auto& unmatched_range =
      action_with(model, ActionIntent::show_combat_range);
  CHECK(unmatched_range.can_invoke());
  CHECK(unmatched_range.combatant == unmatched_delay.combatant);
  const auto& unmatched_bandage =
      action_with(model, ActionIntent::bandage_combatant);
  CHECK(unmatched_bandage.can_invoke());
  CHECK(unmatched_bandage.combatant == unmatched_delay.combatant);
  const auto& unmatched_undo =
      action_with(model, ActionIntent::undo_combatant);
  CHECK(unmatched_undo.can_invoke());
  CHECK(unmatched_undo.combatant == unmatched_delay.combatant);
  const auto& unmatched_combat_spellbook =
      action_with(model, ActionIntent::open_combat_spellbook);
  CHECK(unmatched_combat_spellbook.can_invoke());
  CHECK(unmatched_combat_spellbook.combatant == unmatched_delay.combatant);
  const auto& unmatched_combat_targeting =
      action_with(model, ActionIntent::open_combat_targeting);
  CHECK(unmatched_combat_targeting.can_invoke());
  CHECK(unmatched_combat_targeting.combatant == unmatched_delay.combatant);
  const auto& unmatched_escape =
      action_with(model, ActionIntent::escape_combat);
  CHECK(unmatched_escape.can_invoke());
  CHECK(unmatched_escape.combatant == unmatched_delay.combatant);
  const auto& unmatched_scroll =
      action_with(model, ActionIntent::open_combat_scroll_case);
  CHECK(unmatched_scroll.can_invoke());
  CHECK(unmatched_scroll.combatant == unmatched_delay.combatant);
  const auto& unmatched_cursor =
      action_with(model, ActionIntent::center_combat_cursor);
  CHECK(unmatched_cursor.can_invoke());
  CHECK(unmatched_cursor.combatant == unmatched_delay.combatant);
  snapshot.party = sample_snapshot().party;

  auto no_selection = snapshot;
  no_selection.party.selected_member.reset();
  for (auto& member : no_selection.party.members) {
    member.selected = false;
  }
  const auto no_selection_model =
      build_presentation_shell_model(no_selection);
  CHECK(action_with(no_selection_model, ActionIntent::guard).can_invoke());
  const auto& items_without_selection =
      action_with(no_selection_model, ActionIntent::combat_items);
  CHECK(!items_without_selection.can_invoke());
  CHECK(items_without_selection.combatant == 1);
  CHECK(!items_without_selection.party_member);
  CHECK(items_without_selection.availability_reason->label ==
      "Select a party member first");
  const auto& auto_without_selection =
      action_with(no_selection_model, ActionIntent::auto_combatant);
  CHECK(auto_without_selection.can_invoke());
  CHECK(auto_without_selection.combatant == 1);
  const auto& range_without_selection =
      action_with(no_selection_model, ActionIntent::show_combat_range);
  CHECK(range_without_selection.can_invoke());
  CHECK(range_without_selection.combatant == 1);
  const auto& bandage_without_selection =
      action_with(no_selection_model, ActionIntent::bandage_combatant);
  CHECK(bandage_without_selection.can_invoke());
  CHECK(bandage_without_selection.combatant == 1);
  const auto& undo_without_selection =
      action_with(no_selection_model, ActionIntent::undo_combatant);
  CHECK(undo_without_selection.can_invoke());
  CHECK(undo_without_selection.combatant == 1);
  const auto& combat_spellbook_without_selection =
      action_with(no_selection_model, ActionIntent::open_combat_spellbook);
  CHECK(combat_spellbook_without_selection.can_invoke());
  CHECK(combat_spellbook_without_selection.combatant == 1);
  const auto& combat_targeting_without_selection =
      action_with(no_selection_model, ActionIntent::open_combat_targeting);
  CHECK(combat_targeting_without_selection.can_invoke());
  CHECK(combat_targeting_without_selection.combatant == 1);
  const auto& escape_without_selection =
      action_with(no_selection_model, ActionIntent::escape_combat);
  CHECK(escape_without_selection.can_invoke());
  CHECK(escape_without_selection.combatant == 1);
  const auto& scroll_without_selection =
      action_with(no_selection_model, ActionIntent::open_combat_scroll_case);
  CHECK(scroll_without_selection.can_invoke());
  CHECK(scroll_without_selection.combatant == 1);
  const auto& cursor_without_selection =
      action_with(no_selection_model, ActionIntent::center_combat_cursor);
  CHECK(cursor_without_selection.can_invoke());
  CHECK(cursor_without_selection.combatant == 1);

  const auto check_combat_actions_unavailable = [&snapshot]() {
    const auto unavailable = build_presentation_shell_model(snapshot);
    for (const auto intent : {
             ActionIntent::guard,
             ActionIntent::finish,
             ActionIntent::delay,
             ActionIntent::center_active,
             ActionIntent::switch_weapon,
             ActionIntent::center_previous,
             ActionIntent::center_next,
             ActionIntent::combat_items,
             ActionIntent::auto_combatant,
             ActionIntent::show_combat_range,
             ActionIntent::bandage_combatant,
             ActionIntent::undo_combatant,
             ActionIntent::open_combat_spellbook,
             ActionIntent::open_combat_targeting,
             ActionIntent::escape_combat,
             ActionIntent::open_combat_scroll_case,
             ActionIntent::center_combat_cursor,
         }) {
      const auto& combat_action = action_with(unavailable, intent);
      CHECK(!combat_action.can_invoke());
      CHECK(!combat_action.combatant);
      CHECK(combat_action.availability_reason->label ==
          "Wait for an active party member");
    }
  };

  snapshot.combat->acting_combatant = 10;
  snapshot.combat->combatants[0].active = false;
  snapshot.combat->combatants[1].active = true;
  check_combat_actions_unavailable();

  snapshot.combat->acting_combatant = 1;
  snapshot.combat->combatants[1].active = false;
  check_combat_actions_unavailable();

  snapshot.combat->combatants[0].active = true;
  snapshot.combat->combatants[0].targetable = false;
  check_combat_actions_unavailable();

  snapshot.combat->combatants[0].targetable = true;
  snapshot.combat->combatants[0].stamina.current = 0;
  check_combat_actions_unavailable();

  snapshot.combat->combatants[0].stamina.current = 12;
  snapshot.combat->active = false;
  check_combat_actions_unavailable();

  snapshot.combat.reset();
  check_combat_actions_unavailable();

  snapshot.screen = ScreenContext::exploration;
  const auto noncombat_page = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{
          .combat_action_page = CombatActionPage::utility,
      });
  CHECK(noncombat_page.combat_action_page == CombatActionPage::primary);
  const auto noncombat_special_page = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{
          .combat_action_page = CombatActionPage::special,
      });
  CHECK(noncombat_special_page.combat_action_page ==
      CombatActionPage::primary);
}

void test_events_drawers_motion_and_log_limit() {
  const auto snapshot = sample_snapshot();
  const std::vector<GameEvent> events{
      GameEvent{.sequence = 9,
                .payload = MessageEvent{MessageSeverity::error, "Blocked"}},
      GameEvent{.sequence = 3,
                .payload = MessageEvent{MessageSeverity::warning, "Careful"}},
      GameEvent{.sequence = 3,
                .payload = MessageEvent{MessageSeverity::success, "Found"}},
      GameEvent{.sequence = 4,
                .payload = AnimationCueEvent{
                    .cue = "spark",
                    .target = ActionTarget::party_member(1),
                    .essential_motion = false}},
      GameEvent{.sequence = 5,
                .payload = AnimationCueEvent{
                    .cue = "turn",
                    .target = ActionTarget::none(),
                    .essential_motion = true}},
      GameEvent{.sequence = 2,
                .payload = AudioCueEvent{"bell", AudioBus::effect, false}},
  };
  const ShellViewPreferences preferences{
      .text_scale = 1.25,
      .reduced_motion = true,
      .panels_collapsed = true,
      .active_drawer = DrawerPanel::event_log,
      .event_log_limit = 2,
  };
  const auto model = build_presentation_shell_model(
      snapshot,
      events,
      preferences);

  CHECK(model.event_log.entries.size() == 2);
  CHECK(model.event_log.entries[0].text == "Found");
  CHECK(model.event_log.entries[1].text == "Blocked");
  CHECK(model.event_log.caution_count == 0);
  CHECK(model.event_log.critical_count == 1);
  CHECK(model.event_log.entries[1].state.label == "Error");
  CHECK(model.event_log.entries[1].state.marker == StateMarker::stop);
  CHECK(model.drawers.collapsed);
  CHECK(model.drawers.active_panel == DrawerPanel::event_log);
  CHECK(model.drawers.tabs.size() == 2);
  CHECK(model.drawers.tabs[1].active);
  CHECK(model.drawers.tabs[1].badge_count == 1);
  CHECK(model.motion.reduced_motion);
  CHECK(!model.motion.allow_nonessential_motion);
  CHECK(model.animation_cues.size() == 2);
  CHECK(!model.animation_cues[0].should_animate);
  CHECK(model.animation_cues[1].should_animate);
}

void test_typography_keyboard_order_and_remappable_ids() {
  auto snapshot = sample_snapshot();
  auto model = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{.text_scale = 5.0});
  CHECK(model.typography.scale == 2.0);
  CHECK(model.typography.caption.point_size == 26.0);
  CHECK(model.typography.body.point_size == 32.0);
  CHECK(model.typography.heading.line_height == 52.0);

  model = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{.text_scale =
          std::numeric_limits<double>::quiet_NaN()});
  CHECK(model.typography.scale == 1.0);
  CHECK(!model.keyboard_tab_order.empty());
  for (size_t index = 1; index < model.keyboard_tab_order.size(); ++index) {
    CHECK(model.keyboard_tab_order[index - 1].tab_order <
        model.keyboard_tab_order[index].tab_order);
  }
  CHECK(model.keyboard_tab_order.front().command == "party.select.1");
  CHECK(model.keyboard_tab_order[1].command == "party.select.2");
  CHECK(model.drawers.tabs.empty());
  CHECK(model.keyboard_tab_order.back().command ==
      "action.character_sheet.open");
  CHECK(!model.keyboard_tab_order[4].enabled);
}

void test_determinism_and_no_input_mutation() {
  const auto snapshot = sample_snapshot();
  const std::vector<GameEvent> events{
      GameEvent{.sequence = 2,
                .payload = MessageEvent{MessageSeverity::information, "One"}},
      GameEvent{.sequence = 1,
                .payload = AnimationCueEvent{"pulse", {}, false}},
  };
  const auto snapshot_before = snapshot;
  const auto events_before = events;
  const ShellViewPreferences preferences{
      .text_scale = 0.25,
      .event_log_limit = 0,
  };

  const auto first = build_presentation_shell_model(
      snapshot,
      events,
      preferences);
  const auto second = build_presentation_shell_model(
      snapshot,
      events,
      preferences);
  CHECK(first == second);
  CHECK(snapshot == snapshot_before);
  CHECK(events == events_before);
  CHECK(first.typography.scale == 0.75);
  CHECK(first.event_log.entries.empty());
}

} // namespace

int main() {
  try {
    test_party_rail_and_non_color_states();
    test_selection_fallback_and_meter_bounds();
    test_selected_details_retain_complete_member_status();
    test_action_availability_is_conservative();
    test_world_action_page_preferences_are_normalized();
    test_combat_actions_track_the_active_party_combatant();
    test_events_drawers_motion_and_log_limit();
    test_typography_keyboard_order_and_remappable_ids();
    test_determinism_and_no_input_mutation();
    std::cout << "Party rail model tests passed: " << checks_run
              << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
