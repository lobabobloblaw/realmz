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

void test_combat_guard_tracks_the_active_party_combatant() {
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

  snapshot.combat->acting_combatant = 10;
  snapshot.combat->combatants[0].active = false;
  snapshot.combat->combatants[1].active = true;
  model = build_presentation_shell_model(snapshot);
  const auto& monster_turn = action_with(model, ActionIntent::guard);
  CHECK(!monster_turn.can_invoke());
  CHECK(!monster_turn.combatant);
  CHECK(monster_turn.availability_reason->label ==
      "Wait for an active party member");

  snapshot.combat.reset();
  model = build_presentation_shell_model(snapshot);
  CHECK(!action_with(model, ActionIntent::guard).can_invoke());
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
    test_combat_guard_tracks_the_active_party_combatant();
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
