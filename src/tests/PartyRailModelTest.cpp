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

  snapshot.party.selected_member.reset();
  for (auto& member : snapshot.party.members) {
    member.selected = false;
  }
  model = build_presentation_shell_model(snapshot);
  CHECK(!action_with(model, ActionIntent::open_inventory).can_invoke());
  CHECK(!action_with(model, ActionIntent::cast_spell).can_invoke());

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
  CHECK(model.actions.size() == 8);
  CHECK(model.actions[5].command == "encounter.choice.11");
  CHECK(model.actions[5].can_invoke());
  CHECK(model.actions[6].command == "encounter.choice.12");
  CHECK(!model.actions[6].can_invoke());
  CHECK(model.actions[7].intent == ActionIntent::cancel);
  CHECK(model.actions[7].can_invoke());
}

void test_combat_actions_track_the_active_party_combatant() {
  auto snapshot = sample_snapshot();
  snapshot.screen = ScreenContext::combat;
  snapshot.combat = CombatView{
      .active = true,
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
  CHECK(model.combat_action_page == CombatActionPage::primary);

  const auto secondary_page_model = build_presentation_shell_model(
      snapshot,
      {},
      ShellViewPreferences{
          .combat_action_page = CombatActionPage::secondary,
      });
  CHECK(secondary_page_model.combat_action_page ==
      CombatActionPage::secondary);

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
  snapshot.party = sample_snapshot().party;

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
          .combat_action_page = CombatActionPage::secondary,
      });
  CHECK(noncombat_page.combat_action_page == CombatActionPage::primary);
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
  CHECK(model.keyboard_tab_order.back().command == "action.game.load");
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
    test_action_availability_is_conservative();
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
