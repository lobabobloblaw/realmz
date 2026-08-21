#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

#include "presentation/AdaptiveShell.hpp"
#include "presentation/ShellControlLayout.hpp"

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

bool interiors_overlap(LogicalRect first, LogicalRect second) {
  return std::max(first.x, second.x) < std::min(first.right(), second.right()) &&
      std::max(first.y, second.y) < std::min(first.bottom(), second.bottom());
}

const LogicalRect& action_panel_for(LogicalSize window) {
  static LogicalRect result;
  const auto plan = compute_adaptive_shell_plan({
      .mode = PresentationMode::remastered,
      .screen = ScreenContext::exploration,
      .window_size = window,
  });
  result = plan.adaptive_layout->action_bar;
  return result;
}

void verify_layout(
    const ShellControlLayoutRequest& request,
    size_t expected_count) {
  const auto controls = compute_shell_control_layout(request);
  CHECK(controls.size() == expected_count);
  std::set<uint32_t> regions;
  std::set<std::string> focus_ids;
  int32_t previous_tab = -1;
  for (size_t index = 0; index < controls.size(); ++index) {
    const auto& control = controls[index];
    CHECK(control.region.is_valid());
    CHECK(regions.emplace(control.region.value).second);
    CHECK(focus_ids.emplace(control.focus_identifier).second);
    CHECK(request.action_panel.contains(control.bounds));
    CHECK(control.bounds.width >= 44.0);
    CHECK(control.bounds.height >= 44.0);
    CHECK(control.enabled == request.navigation_available);
    CHECK(control.kind == ShellControlKind::movement);
    CHECK(std::holds_alternative<MovePartyAction>(control.payload));
    CHECK(control.tab_order > previous_tab);
    previous_tab = control.tab_order;
    for (size_t prior = 0; prior < index; ++prior) {
      CHECK(!interiors_overlap(control.bounds, controls[prior].bounds));
    }
  }
}

void test_canonical_sizes() {
  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1359.0, 900.0},
      LogicalSize{1360.0, 768.0},
      LogicalSize{1440.0, 900.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto size : sizes) {
    const auto panel = action_panel_for(size);
    verify_layout({
        .screen = ScreenContext::exploration,
        .world_presentation = WorldPresentation::outdoor,
        .action_panel = panel,
        .navigation_available = true,
    }, 8);
    verify_layout({
        .screen = ScreenContext::dungeon,
        .world_presentation = WorldPresentation::dungeon_first_person,
        .action_panel = panel,
        .navigation_available = true,
    }, 4);
  }
}

void test_payload_order_and_disabled_state() {
  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  const auto outdoor = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = false,
  });
  constexpr std::array expected{
      MovementCommand::northwest,
      MovementCommand::north,
      MovementCommand::northeast,
      MovementCommand::west,
      MovementCommand::east,
      MovementCommand::southwest,
      MovementCommand::south,
      MovementCommand::southeast,
  };
  constexpr std::array<uint32_t, 8> expected_outdoor_regions{
      1011, 1004, 1005, 1010, 1006, 1009, 1008, 1007};
  CHECK(outdoor.size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    CHECK(!outdoor[index].enabled);
    CHECK(std::get<MovePartyAction>(outdoor[index].payload).command ==
        expected[index]);
    CHECK(outdoor[index].region.value == expected_outdoor_regions[index]);
  }

  const auto dungeon = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_map,
      .action_panel = panel,
      .navigation_available = true,
  });
  CHECK(std::get<MovePartyAction>(dungeon[0].payload).command ==
      MovementCommand::turn_left);
  CHECK(std::get<MovePartyAction>(dungeon[1].payload).command ==
      MovementCommand::step_forward);
  CHECK(std::get<MovePartyAction>(dungeon[2].payload).command ==
      MovementCommand::step_backward);
  CHECK(std::get<MovePartyAction>(dungeon[3].payload).command ==
      MovementCommand::turn_right);
  constexpr std::array<uint32_t, 4> expected_dungeon_regions{
      1002, 1000, 1001, 1003};
  for (size_t index = 0; index < dungeon.size(); ++index) {
    CHECK(dungeon[index].region.value == expected_dungeon_regions[index]);
  }

  const auto first_person = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .navigation_available = true,
  });
  CHECK(first_person.size() == expected_dungeon_regions.size());
  for (size_t index = 0; index < first_person.size(); ++index) {
    CHECK(first_person[index].region.value == expected_dungeon_regions[index]);
    CHECK(first_person[index].payload == dungeon[index].payload);
  }

  std::set<uint32_t> all_semantic_regions;
  for (const auto& control : outdoor) {
    CHECK(all_semantic_regions.emplace(control.region.value).second);
  }
  for (const auto& control : dungeon) {
    CHECK(all_semantic_regions.emplace(control.region.value).second);
  }
  CHECK(all_semantic_regions.size() == 12);
}

void test_open_inventory_control() {
  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1359.0, 900.0},
      LogicalSize{1360.0, 768.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto size : sizes) {
    const auto panel = action_panel_for(size);
    for (const auto& request : {
             ShellControlLayoutRequest{
                 .screen = ScreenContext::exploration,
                 .world_presentation = WorldPresentation::outdoor,
                 .action_panel = panel,
                 .navigation_available = true,
                 .inventory_member = PartyMemberId{2},
                 .inventory_available = true,
             },
             ShellControlLayoutRequest{
                 .screen = ScreenContext::dungeon,
                 .world_presentation = WorldPresentation::dungeon_map,
                 .action_panel = panel,
                 .navigation_available = true,
                 .inventory_member = PartyMemberId{2},
                 .inventory_available = true,
             },
         }) {
      const auto controls = compute_shell_control_layout(request);
      const size_t expected_count =
          request.screen == ScreenContext::exploration ? 9U : 5U;
      CHECK(controls.size() == expected_count);
      const auto& inventory = controls.back();
      CHECK(inventory.region.value == 1100U);
      CHECK(inventory.kind == ShellControlKind::open_inventory);
      CHECK(inventory.label == "ITEMS");
      CHECK(inventory.accessibility_label == "Open inventory");
      CHECK(inventory.focus_identifier == "focus.action.inventory.open");
      CHECK(inventory.tab_order == 1100);
      CHECK(inventory.enabled);
      CHECK(std::holds_alternative<OpenInventoryAction>(inventory.payload));
      CHECK(std::get<OpenInventoryAction>(inventory.payload).member == 2);
      CHECK(request.action_panel.contains(inventory.bounds));
      CHECK(inventory.bounds.width >= 44.0);
      CHECK(inventory.bounds.height >= 44.0);
      for (size_t index = 0; index + 1U < controls.size(); ++index) {
        CHECK(controls[index].kind == ShellControlKind::movement);
        CHECK(!interiors_overlap(controls[index].bounds, inventory.bounds));
      }
    }
  }

  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  const auto disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .inventory_member = PartyMemberId{4},
      .inventory_available = false,
  });
  CHECK(disabled.size() == 9U);
  CHECK(!disabled.back().enabled);
  CHECK(std::get<OpenInventoryAction>(disabled.back().payload).member == 4);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .inventory_available = true,
  }).empty());
}

void test_spellbook_save_and_load_controls_at_combined_minimum_layout() {
  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1359.0, 900.0},
      LogicalSize{1360.0, 768.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto size : sizes) {
    const auto panel = action_panel_for(size);
    for (const auto& request : {
             ShellControlLayoutRequest{
                 .screen = ScreenContext::exploration,
                 .world_presentation = WorldPresentation::outdoor,
                 .action_panel = panel,
                 .navigation_available = true,
                 .inventory_member = PartyMemberId{2},
                 .inventory_available = true,
                 .spellbook_member = PartyMemberId{2},
                 .spellbook_available = true,
                 .save_control_visible = true,
                 .save_available = true,
                 .load_control_visible = true,
                 .load_available = true,
             },
             ShellControlLayoutRequest{
                 .screen = ScreenContext::dungeon,
                 .world_presentation = WorldPresentation::dungeon_map,
                 .action_panel = panel,
                 .navigation_available = true,
                 .inventory_member = PartyMemberId{2},
                 .inventory_available = true,
                 .spellbook_member = PartyMemberId{2},
                 .spellbook_available = true,
                 .save_control_visible = true,
                 .save_available = true,
                 .load_control_visible = true,
                 .load_available = true,
             },
         }) {
      const auto controls = compute_shell_control_layout(request);
      const size_t expected_count =
          request.screen == ScreenContext::exploration ? 12U : 8U;
      CHECK(controls.size() == expected_count);
      const auto& inventory = controls[controls.size() - 4U];
      const auto& spellbook = controls[controls.size() - 3U];
      const auto& save = controls[controls.size() - 2U];
      const auto& load = controls.back();
      CHECK(inventory.kind == ShellControlKind::open_inventory);
      CHECK(spellbook.region.value == 1101U);
      CHECK(spellbook.kind == ShellControlKind::open_spellbook);
      CHECK(spellbook.label == "SPELLS");
      CHECK(spellbook.accessibility_label == "Cast spell");
      CHECK(spellbook.focus_identifier == "focus.action.spellbook.open");
      CHECK(spellbook.tab_order == 1101);
      CHECK(spellbook.enabled);
      CHECK(std::holds_alternative<OpenSpellbookAction>(spellbook.payload));
      CHECK(std::get<OpenSpellbookAction>(spellbook.payload).member == 2);
      CHECK(request.action_panel.contains(spellbook.bounds));
      CHECK(spellbook.bounds.width >= 44.0);
      CHECK(spellbook.bounds.height >= 44.0);
      CHECK(!interiors_overlap(inventory.bounds, spellbook.bounds));
      CHECK(save.region.value == 1102U);
      CHECK(save.kind == ShellControlKind::open_save_game);
      CHECK(save.label == "SAVE");
      CHECK(save.accessibility_label == "Open save dialog");
      CHECK(save.focus_identifier == "focus.action.save.open");
      CHECK(save.tab_order == 1102);
      CHECK(save.enabled);
      CHECK(std::holds_alternative<OpenSaveGameAction>(save.payload));
      CHECK(request.action_panel.contains(save.bounds));
      CHECK(save.bounds.width >= 44.0);
      CHECK(save.bounds.height >= 44.0);
      CHECK(load.region.value == 1103U);
      CHECK(load.kind == ShellControlKind::open_load_game);
      CHECK(load.label == "LOAD");
      CHECK(load.accessibility_label == "Open load dialog");
      CHECK(load.focus_identifier == "focus.action.load.open");
      CHECK(load.tab_order == 1103);
      CHECK(load.enabled);
      CHECK(std::holds_alternative<OpenLoadGameAction>(load.payload));
      CHECK(request.action_panel.contains(load.bounds));
      CHECK(load.bounds.width >= 44.0);
      CHECK(load.bounds.height >= 44.0);
      for (size_t index = 0; index + 3U < controls.size(); ++index) {
        CHECK(!interiors_overlap(controls[index].bounds, spellbook.bounds));
      }
      for (size_t index = 0; index + 2U < controls.size(); ++index) {
        CHECK(!interiors_overlap(controls[index].bounds, save.bounds));
      }
      for (size_t index = 0; index + 1U < controls.size(); ++index) {
        CHECK(!interiors_overlap(controls[index].bounds, load.bounds));
      }
    }
  }

  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  const auto disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .spellbook_member = PartyMemberId{4},
      .spellbook_available = false,
  });
  CHECK(disabled.size() == 9U);
  CHECK(disabled.back().kind == ShellControlKind::open_spellbook);
  CHECK(!disabled.back().enabled);
  CHECK(std::get<OpenSpellbookAction>(disabled.back().payload).member == 4);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .spellbook_available = true,
  }).empty());

  const auto save_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .save_control_visible = true,
      .save_available = false,
  });
  CHECK(save_disabled.size() == 9U);
  CHECK(save_disabled.back().kind == ShellControlKind::open_save_game);
  CHECK(!save_disabled.back().enabled);
  CHECK(std::holds_alternative<OpenSaveGameAction>(
      save_disabled.back().payload));

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .save_available = true,
  }).empty());

  const auto load_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .load_control_visible = true,
      .load_available = false,
  });
  CHECK(load_disabled.size() == 9U);
  CHECK(load_disabled.back().kind == ShellControlKind::open_load_game);
  CHECK(!load_disabled.back().enabled);
  CHECK(std::holds_alternative<OpenLoadGameAction>(
      load_disabled.back().payload));

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .load_available = true,
  }).empty());
}

void test_fail_closed_inputs() {
  const LogicalRect usable{0.0, 0.0, 800.0, 150.0};
  for (const auto screen : {
           ScreenContext::title,
           ScreenContext::party_selection,
           ScreenContext::party_creation,
           ScreenContext::combat,
           ScreenContext::inventory,
           ScreenContext::shop,
           ScreenContext::encounter,
           ScreenContext::ending,
       }) {
    CHECK(compute_shell_control_layout({
        .screen = screen,
        .world_presentation = WorldPresentation::outdoor,
        .action_panel = usable,
        .navigation_available = true,
    }).empty());
  }
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::dungeon_map,
      .action_panel = usable,
      .navigation_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = usable,
      .navigation_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = {0.0, 0.0, 300.0, 90.0},
      .navigation_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = {0.0, 0.0, -1.0, 150.0},
      .navigation_available = true,
  }).empty());
}

void test_combat_primary_and_secondary_action_pages() {
  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1359.0, 900.0},
      LogicalSize{1360.0, 768.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto size : sizes) {
    const auto panel = action_panel_for(size);
    const auto controls = compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .world_presentation = WorldPresentation::none,
        .action_panel = panel,
        .guard_combatant = CombatantId{2},
        .guard_available = true,
        .finish_combatant = CombatantId{2},
        .finish_available = true,
        .delay_combatant = CombatantId{2},
        .delay_available = true,
        .center_active_combatant = CombatantId{2},
        .center_active_available = true,
        .switch_weapon_combatant = CombatantId{2},
        .switch_weapon_available = true,
        .center_previous_combatant = CombatantId{2},
        .center_previous_available = true,
        .center_next_combatant = CombatantId{2},
        .center_next_available = true,
        .combat_items = OpenCombatItemsAction{2, 4},
        .combat_items_available = true,
        .auto_combatant = CombatantId{2},
        .auto_combatant_available = true,
        .show_combat_range_combatant = CombatantId{2},
        .show_combat_range_available = true,
        .bandage_combatant = CombatantId{2},
        .bandage_combatant_available = true,
    });
    CHECK(controls.size() == 5U);
    const auto& guard = controls[0];
    const auto& finish = controls[1];
    const auto& delay = controls[2];
    const auto& center = controls[3];
    const auto& more = controls[4];
    CHECK(guard.region.value == 1104U);
    CHECK(guard.kind == ShellControlKind::guard_combatant);
    CHECK(guard.label == "GUARD");
    CHECK(guard.accessibility_label == "Guard active combatant");
    CHECK(guard.focus_identifier == "focus.action.combat.guard");
    CHECK(guard.tab_order == 1104);
    CHECK(guard.enabled);
    CHECK(std::get<GuardCombatantAction>(guard.payload).combatant == 2);
    CHECK(finish.region.value == 1105U);
    CHECK(finish.kind == ShellControlKind::finish_combatant);
    CHECK(finish.label == "FINISH");
    CHECK(finish.accessibility_label ==
        "Finish active combatant's turn");
    CHECK(finish.focus_identifier == "focus.action.combat.finish");
    CHECK(finish.tab_order == 1105);
    CHECK(finish.enabled);
    CHECK(std::get<FinishCombatantAction>(finish.payload).combatant == 2);
    CHECK(delay.region.value == 1106U);
    CHECK(delay.kind == ShellControlKind::delay_combatant);
    CHECK(delay.label == "DELAY");
    CHECK(delay.accessibility_label ==
        "Delay active combatant's turn");
    CHECK(delay.focus_identifier == "focus.action.combat.delay");
    CHECK(delay.tab_order == 1106);
    CHECK(delay.enabled);
    CHECK(std::get<DelayCombatantAction>(delay.payload).combatant == 2);
    CHECK(center.region.value == 1107U);
    CHECK(center.kind == ShellControlKind::center_active_combatant);
    CHECK(center.label == "CENTER");
    CHECK(center.accessibility_label ==
        "Center view on active combatant");
    CHECK(center.focus_identifier == "focus.action.combat.center");
    CHECK(center.tab_order == 1107);
    CHECK(center.enabled);
    CHECK(std::get<CenterActiveCombatantAction>(center.payload).combatant == 2);
    CHECK(more.region.value == 1108U);
    CHECK(more.kind == ShellControlKind::combat_action_page);
    CHECK(more.label == "MORE");
    CHECK(more.accessibility_label == "Open more combat actions");
    CHECK(more.focus_identifier == "focus.action.combat.more");
    CHECK(more.tab_order == 1108);
    CHECK(more.enabled);
    CHECK(std::get<SetCombatActionPageAction>(more.payload).page ==
        CombatActionPage::secondary);
    CHECK(is_valid_combat_action_page_transition(
        CombatActionPage::primary,
        std::get<SetCombatActionPageAction>(more.payload).page));
    for (const auto& control : controls) {
      CHECK(panel.contains(control.bounds));
      CHECK(control.bounds.width >= 44.0);
      CHECK(control.bounds.width <= 160.0);
      CHECK(control.bounds.height >= 44.0);
    }
    CHECK(!interiors_overlap(guard.bounds, finish.bounds));
    CHECK(!interiors_overlap(guard.bounds, delay.bounds));
    CHECK(!interiors_overlap(guard.bounds, center.bounds));
    CHECK(!interiors_overlap(finish.bounds, delay.bounds));
    CHECK(!interiors_overlap(finish.bounds, center.bounds));
    CHECK(!interiors_overlap(delay.bounds, center.bounds));
    CHECK(!interiors_overlap(center.bounds, more.bounds));
    CHECK(finish.bounds.x > guard.bounds.x);
    CHECK(delay.bounds.x > finish.bounds.x);
    CHECK(center.bounds.x > delay.bounds.x);
    const LogicalPoint center_pointer{
        center.bounds.x + center.bounds.width / 2.0,
        center.bounds.y + center.bounds.height / 2.0,
    };
    CHECK(center.bounds.contains(center_pointer));
    CHECK(!guard.bounds.contains(center_pointer));
    CHECK(!finish.bounds.contains(center_pointer));
    CHECK(!delay.bounds.contains(center_pointer));

    const auto secondary = compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .world_presentation = WorldPresentation::none,
        .action_panel = panel,
        .guard_combatant = CombatantId{2},
        .guard_available = true,
        .finish_combatant = CombatantId{2},
        .finish_available = true,
        .delay_combatant = CombatantId{2},
        .delay_available = true,
        .center_active_combatant = CombatantId{2},
        .center_active_available = true,
        .combat_action_page = CombatActionPage::secondary,
        .switch_weapon_combatant = CombatantId{2},
        .switch_weapon_available = true,
        .center_previous_combatant = CombatantId{2},
        .center_previous_available = true,
        .center_next_combatant = CombatantId{2},
        .center_next_available = true,
        .combat_items = OpenCombatItemsAction{2, 4},
        .combat_items_available = true,
        .auto_combatant = CombatantId{2},
        .auto_combatant_available = true,
        .show_combat_range_combatant = CombatantId{2},
        .show_combat_range_available = true,
        .bandage_combatant = CombatantId{2},
        .bandage_combatant_available = true,
    });
    CHECK(secondary.size() == 6U);
    const auto& back = secondary[0];
    const auto& weapon = secondary[1];
    const auto& previous = secondary[2];
    const auto& next = secondary[3];
    const auto& items = secondary[4];
    const auto& utility_more = secondary[5];
    CHECK(back.region.value == 1108U);
    CHECK(back.kind == ShellControlKind::combat_action_page);
    CHECK(back.label == "BACK");
    CHECK(back.accessibility_label ==
        "Return to primary combat actions");
    CHECK(back.focus_identifier == "focus.action.combat.more");
    CHECK(back.tab_order == 1108);
    CHECK(back.enabled);
    CHECK(std::get<SetCombatActionPageAction>(back.payload).page ==
        CombatActionPage::primary);
    CHECK(is_valid_combat_action_page_transition(
        CombatActionPage::secondary,
        std::get<SetCombatActionPageAction>(back.payload).page));
    CHECK(weapon.region.value == 1109U);
    CHECK(weapon.kind == ShellControlKind::switch_weapon_set);
    CHECK(weapon.label == "WEAPON");
    CHECK(weapon.accessibility_label ==
        "Switch active combatant's weapon set");
    CHECK(weapon.focus_identifier == "focus.action.combat.weapon");
    CHECK(weapon.tab_order == 1109);
    CHECK(weapon.enabled);
    CHECK(std::get<SwitchWeaponSetAction>(weapon.payload).combatant == 2);
    CHECK(previous.region.value == 1110U);
    CHECK(previous.kind == ShellControlKind::cycle_combat_focus);
    CHECK(previous.label == "PREV");
    CHECK(previous.accessibility_label ==
        "Center view on previous combatant");
    CHECK(previous.focus_identifier ==
        "focus.action.combat.center.previous");
    CHECK(previous.tab_order == 1110);
    CHECK(previous.enabled);
    const auto& previous_action =
        std::get<CycleCombatFocusAction>(previous.payload);
    CHECK(previous_action.combatant == 2);
    CHECK(previous_action.direction == CombatFocusDirection::previous);
    CHECK(next.region.value == 1111U);
    CHECK(next.kind == ShellControlKind::cycle_combat_focus);
    CHECK(next.label == "NEXT");
    CHECK(next.accessibility_label == "Center view on next combatant");
    CHECK(next.focus_identifier == "focus.action.combat.center.next");
    CHECK(next.tab_order == 1111);
    CHECK(next.enabled);
    const auto& next_action =
        std::get<CycleCombatFocusAction>(next.payload);
    CHECK(next_action.combatant == 2);
    CHECK(next_action.direction == CombatFocusDirection::next);
    CHECK(items.region.value == 1112U);
    CHECK(items.kind == ShellControlKind::open_combat_items);
    CHECK(items.label == "ITEMS");
    CHECK(items.accessibility_label == "Open combat items");
    CHECK(items.focus_identifier == "focus.action.combat.items");
    CHECK(items.tab_order == 1112);
    CHECK(items.enabled);
    const auto& items_action =
        std::get<OpenCombatItemsAction>(items.payload);
    CHECK(items_action.combatant == 2);
    CHECK(items_action.member == 4);
    CHECK(items_action.combatant != items_action.member);
    CHECK(utility_more.region.value == 1113U);
    CHECK(utility_more.kind == ShellControlKind::combat_action_page);
    CHECK(utility_more.label == "MORE");
    CHECK(utility_more.accessibility_label ==
        "Open utility combat actions");
    CHECK(utility_more.focus_identifier ==
        "focus.action.combat.utility");
    CHECK(utility_more.tab_order == 1113);
    CHECK(utility_more.enabled);
    CHECK(std::get<SetCombatActionPageAction>(utility_more.payload).page ==
        CombatActionPage::utility);
    CHECK(is_valid_combat_action_page_transition(
        CombatActionPage::secondary,
        std::get<SetCombatActionPageAction>(utility_more.payload).page));
    CHECK(panel.contains(back.bounds));
    CHECK(panel.contains(weapon.bounds));
    CHECK(panel.contains(previous.bounds));
    CHECK(panel.contains(next.bounds));
    CHECK(panel.contains(items.bounds));
    CHECK(panel.contains(utility_more.bounds));
    CHECK(back.bounds.width == 44.0);
    CHECK(back.bounds.height == 44.0);
    CHECK(weapon.bounds.width >= 44.0);
    CHECK(weapon.bounds.width <= 160.0);
    CHECK(weapon.bounds.height >= 44.0);
    CHECK(!interiors_overlap(back.bounds, weapon.bounds));
    CHECK(previous.bounds.width >= 44.0);
    CHECK(previous.bounds.width <= 160.0);
    CHECK(previous.bounds.height >= 44.0);
    CHECK(next.bounds.width >= 44.0);
    CHECK(next.bounds.width <= 160.0);
    CHECK(next.bounds.height >= 44.0);
    CHECK(items.bounds.width >= 44.0);
    CHECK(items.bounds.width <= 160.0);
    CHECK(items.bounds.height >= 44.0);
    CHECK(utility_more.bounds.width == 44.0);
    CHECK(utility_more.bounds.height == 44.0);
    CHECK(!interiors_overlap(weapon.bounds, previous.bounds));
    CHECK(!interiors_overlap(weapon.bounds, next.bounds));
    CHECK(!interiors_overlap(previous.bounds, next.bounds));
    CHECK(!interiors_overlap(weapon.bounds, items.bounds));
    CHECK(!interiors_overlap(previous.bounds, items.bounds));
    CHECK(!interiors_overlap(next.bounds, items.bounds));
    CHECK(!interiors_overlap(back.bounds, utility_more.bounds));
    CHECK(!interiors_overlap(utility_more.bounds, weapon.bounds));
    CHECK(previous.bounds.x > weapon.bounds.x);
    CHECK(next.bounds.x > previous.bounds.x);
    CHECK(items.bounds.x > next.bounds.x);
    const LogicalPoint previous_pointer{
        previous.bounds.x + previous.bounds.width / 2.0,
        previous.bounds.y + previous.bounds.height / 2.0,
    };
    const LogicalPoint next_pointer{
        next.bounds.x + next.bounds.width / 2.0,
        next.bounds.y + next.bounds.height / 2.0,
    };
    const LogicalPoint items_pointer{
        items.bounds.x + items.bounds.width / 2.0,
        items.bounds.y + items.bounds.height / 2.0,
    };
    CHECK(previous.bounds.contains(previous_pointer));
    CHECK(!weapon.bounds.contains(previous_pointer));
    CHECK(!next.bounds.contains(previous_pointer));
    CHECK(!items.bounds.contains(previous_pointer));
    CHECK(next.bounds.contains(next_pointer));
    CHECK(!weapon.bounds.contains(next_pointer));
    CHECK(!previous.bounds.contains(next_pointer));
    CHECK(!items.bounds.contains(next_pointer));
    CHECK(items.bounds.contains(items_pointer));
    CHECK(!weapon.bounds.contains(items_pointer));
    CHECK(!previous.bounds.contains(items_pointer));
    CHECK(!next.bounds.contains(items_pointer));

    const auto utility = compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .world_presentation = WorldPresentation::none,
        .action_panel = panel,
        .guard_combatant = CombatantId{2},
        .guard_available = true,
        .finish_combatant = CombatantId{2},
        .finish_available = true,
        .delay_combatant = CombatantId{2},
        .delay_available = true,
        .center_active_combatant = CombatantId{2},
        .center_active_available = true,
        .combat_action_page = CombatActionPage::utility,
        .switch_weapon_combatant = CombatantId{2},
        .switch_weapon_available = true,
        .center_previous_combatant = CombatantId{2},
        .center_previous_available = true,
        .center_next_combatant = CombatantId{2},
        .center_next_available = true,
        .combat_items = OpenCombatItemsAction{2, 4},
        .combat_items_available = true,
        .auto_combatant = CombatantId{2},
        .auto_combatant_available = true,
        .show_combat_range_combatant = CombatantId{2},
        .show_combat_range_available = true,
        .bandage_combatant = CombatantId{2},
        .bandage_combatant_available = true,
        .undo_combatant = CombatantId{2},
        .undo_combatant_available = true,
        .open_combat_spellbook = CombatantId{2},
        .open_combat_spellbook_available = true,
        .open_combat_targeting = CombatantId{2},
        .open_combat_targeting_available = true,
    });
    CHECK(utility.size() == 6U);
    const auto& utility_back = utility[0];
    const auto& auto_control = utility[1];
    const auto& range_control = utility[2];
    const auto& bandage_control = utility[3];
    const auto& undo_control = utility[4];
    const auto& special_more = utility[5];
    CHECK(utility_back.region.value == 1108U);
    CHECK(utility_back.kind == ShellControlKind::combat_action_page);
    CHECK(utility_back.label == "BACK");
    CHECK(utility_back.accessibility_label ==
        "Return to more combat actions");
    CHECK(utility_back.focus_identifier ==
        "focus.action.combat.more");
    CHECK(utility_back.tab_order == 1108);
    CHECK(utility_back.enabled);
    CHECK(std::get<SetCombatActionPageAction>(utility_back.payload).page ==
        CombatActionPage::secondary);
    CHECK(is_valid_combat_action_page_transition(
        CombatActionPage::utility,
        std::get<SetCombatActionPageAction>(utility_back.payload).page));
    CHECK(auto_control.region.value == 1114U);
    CHECK(auto_control.kind == ShellControlKind::auto_combatant);
    CHECK(auto_control.label == "AUTO");
    CHECK(auto_control.accessibility_label ==
        "Auto-play active combatant's turn");
    CHECK(auto_control.focus_identifier == "focus.action.combat.auto");
    CHECK(auto_control.tab_order == 1114);
    CHECK(auto_control.enabled);
    CHECK(std::get<AutoCombatantAction>(auto_control.payload).combatant == 2);
    CHECK(range_control.region.value == 1115U);
    CHECK(range_control.kind == ShellControlKind::show_combat_range);
    CHECK(range_control.label == "RANGE");
    CHECK(range_control.accessibility_label ==
        "Show combat ranges; press any key to close");
    CHECK(range_control.focus_identifier == "focus.action.combat.range");
    CHECK(range_control.tab_order == 1115);
    CHECK(range_control.enabled);
    CHECK(std::get<ShowCombatRangeAction>(range_control.payload).combatant ==
        2);
    CHECK(bandage_control.region.value == 1116U);
    CHECK(bandage_control.kind == ShellControlKind::bandage_combatant);
    CHECK(bandage_control.label == "BANDAGE");
    CHECK(bandage_control.accessibility_label ==
        "Choose a party member to bandage");
    CHECK(bandage_control.focus_identifier ==
        "focus.action.combat.bandage");
    CHECK(bandage_control.tab_order == 1116);
    CHECK(bandage_control.enabled);
    CHECK(std::get<BandageCombatantAction>(
        bandage_control.payload).combatant == 2);
    CHECK(undo_control.region.value == 1117U);
    CHECK(undo_control.kind == ShellControlKind::undo_combatant);
    CHECK(undo_control.label == "UNDO");
    CHECK(undo_control.accessibility_label ==
        "Undo active combatant's movement");
    CHECK(undo_control.focus_identifier == "focus.action.combat.undo");
    CHECK(undo_control.tab_order == 1117);
    CHECK(undo_control.enabled);
    CHECK(std::get<UndoCombatantAction>(
        undo_control.payload).combatant == 2);
    CHECK(special_more.region.value == 1118U);
    CHECK(special_more.kind == ShellControlKind::combat_action_page);
    CHECK(special_more.label == "MORE");
    CHECK(special_more.accessibility_label == "Open special combat actions");
    CHECK(special_more.focus_identifier == "focus.action.combat.special");
    CHECK(special_more.tab_order == 1118);
    CHECK(special_more.enabled);
    CHECK(std::get<SetCombatActionPageAction>(special_more.payload).page ==
        CombatActionPage::special);
    CHECK(is_valid_combat_action_page_transition(
        CombatActionPage::utility, CombatActionPage::special));
    CHECK(panel.contains(utility_back.bounds));
    CHECK(panel.contains(auto_control.bounds));
    CHECK(panel.contains(range_control.bounds));
    CHECK(panel.contains(bandage_control.bounds));
    CHECK(panel.contains(undo_control.bounds));
    CHECK(panel.contains(special_more.bounds));
    CHECK(utility_back.bounds.width == 44.0);
    CHECK(utility_back.bounds.height == 44.0);
    CHECK(auto_control.bounds.width >= 44.0);
    CHECK(auto_control.bounds.width <= 160.0);
    CHECK(auto_control.bounds.height >= 44.0);
    CHECK(range_control.bounds.width >= 44.0);
    CHECK(range_control.bounds.width <= 160.0);
    CHECK(range_control.bounds.height >= 44.0);
    CHECK(bandage_control.bounds.width >= 44.0);
    CHECK(bandage_control.bounds.width <= 160.0);
    CHECK(bandage_control.bounds.height >= 44.0);
    CHECK(undo_control.bounds.width >= 44.0);
    CHECK(undo_control.bounds.width <= 160.0);
    CHECK(undo_control.bounds.height >= 44.0);
    CHECK(!interiors_overlap(utility_back.bounds, auto_control.bounds));
    CHECK(!interiors_overlap(utility_back.bounds, range_control.bounds));
    CHECK(!interiors_overlap(auto_control.bounds, range_control.bounds));
    CHECK(!interiors_overlap(auto_control.bounds, bandage_control.bounds));
    CHECK(!interiors_overlap(range_control.bounds, bandage_control.bounds));
    CHECK(!interiors_overlap(auto_control.bounds, undo_control.bounds));
    CHECK(!interiors_overlap(range_control.bounds, undo_control.bounds));
    CHECK(!interiors_overlap(bandage_control.bounds, undo_control.bounds));
    CHECK(range_control.bounds.x > auto_control.bounds.x);
    CHECK(bandage_control.bounds.x > range_control.bounds.x);
    CHECK(undo_control.bounds.x > bandage_control.bounds.x);
    const LogicalPoint auto_pointer{
        auto_control.bounds.x + auto_control.bounds.width / 2.0,
        auto_control.bounds.y + auto_control.bounds.height / 2.0,
    };
    CHECK(auto_control.bounds.contains(auto_pointer));
    CHECK(!utility_back.bounds.contains(auto_pointer));
    CHECK(!range_control.bounds.contains(auto_pointer));
    CHECK(!bandage_control.bounds.contains(auto_pointer));
    CHECK(!undo_control.bounds.contains(auto_pointer));
    const LogicalPoint range_pointer{
        range_control.bounds.x + range_control.bounds.width / 2.0,
        range_control.bounds.y + range_control.bounds.height / 2.0,
    };
    CHECK(range_control.bounds.contains(range_pointer));
    CHECK(!utility_back.bounds.contains(range_pointer));
    CHECK(!auto_control.bounds.contains(range_pointer));
    CHECK(!bandage_control.bounds.contains(range_pointer));
    CHECK(!undo_control.bounds.contains(range_pointer));
    const LogicalPoint bandage_pointer{
        bandage_control.bounds.x + bandage_control.bounds.width / 2.0,
        bandage_control.bounds.y + bandage_control.bounds.height / 2.0,
    };
    CHECK(bandage_control.bounds.contains(bandage_pointer));
    CHECK(!utility_back.bounds.contains(bandage_pointer));
    CHECK(!auto_control.bounds.contains(bandage_pointer));
    CHECK(!range_control.bounds.contains(bandage_pointer));
    CHECK(!undo_control.bounds.contains(bandage_pointer));
    const LogicalPoint undo_pointer{
        undo_control.bounds.x + undo_control.bounds.width / 2.0,
        undo_control.bounds.y + undo_control.bounds.height / 2.0,
    };
    CHECK(undo_control.bounds.contains(undo_pointer));
    CHECK(!utility_back.bounds.contains(undo_pointer));
    CHECK(!auto_control.bounds.contains(undo_pointer));
    CHECK(!range_control.bounds.contains(undo_pointer));
    CHECK(!bandage_control.bounds.contains(undo_pointer));
  }

  {
    const LogicalRect special_panel{16.0, 600.0, 900.0, 150.0};
    const auto special = compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .world_presentation = WorldPresentation::none,
        .action_panel = special_panel,
        .combat_action_page = CombatActionPage::special,
        .open_combat_spellbook = CombatantId{2},
        .open_combat_spellbook_available = true,
        .open_combat_targeting = CombatantId{2},
        .open_combat_targeting_available = true,
    });
    CHECK(special.size() == 3U);
    const auto& special_back = special[0];
    const auto& cast = special[1];
    const auto& target = special[2];
    CHECK(special_back.region.value == 1118U);
    CHECK(special_back.kind == ShellControlKind::combat_action_page);
    CHECK(special_back.label == "BACK");
    CHECK(special_back.accessibility_label ==
        "Return to utility combat actions");
    CHECK(special_back.focus_identifier == "focus.action.combat.special");
    CHECK(special_back.tab_order == 1118);
    CHECK(std::get<SetCombatActionPageAction>(special_back.payload).page ==
        CombatActionPage::utility);
    CHECK(is_valid_combat_action_page_transition(
        CombatActionPage::special, CombatActionPage::utility));
    CHECK(cast.region.value == 1119U);
    CHECK(cast.kind == ShellControlKind::open_combat_spellbook);
    CHECK(cast.label == "CAST");
    CHECK(cast.accessibility_label == "Open combat spell chooser");
    CHECK(cast.focus_identifier == "focus.action.combat.spellbook.open");
    CHECK(cast.tab_order == 1119);
    CHECK(cast.enabled);
    CHECK(std::get<OpenCombatSpellbookAction>(cast.payload).combatant == 2);
    CHECK(target.region.value == 1120U);
    CHECK(target.kind == ShellControlKind::open_combat_targeting);
    CHECK(target.label == "TARGET");
    CHECK(target.accessibility_label == "Begin combat targeting");
    CHECK(target.focus_identifier == "focus.action.combat.targeting.open");
    CHECK(target.tab_order == 1120);
    CHECK(target.enabled);
    CHECK(std::get<OpenCombatTargetingAction>(target.payload).combatant == 2);
    CHECK(special_panel.contains(special_back.bounds));
    CHECK(special_panel.contains(cast.bounds));
    CHECK(special_panel.contains(target.bounds));
    CHECK(!interiors_overlap(special_back.bounds, cast.bounds));
    CHECK(!interiors_overlap(special_back.bounds, target.bounds));
    CHECK(!interiors_overlap(cast.bounds, target.bounds));
    const LogicalPoint target_pointer{
        target.bounds.x + target.bounds.width / 2.0,
        target.bounds.y + target.bounds.height / 2.0,
    };
    CHECK(target.bounds.contains(target_pointer));
    CHECK(!special_back.bounds.contains(target_pointer));
    CHECK(!cast.bounds.contains(target_pointer));
  }

  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};

  const auto disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{2},
  });
  CHECK(disabled.size() == 5U);
  CHECK(!disabled[0].enabled);
  CHECK(!disabled[1].enabled);
  CHECK(!disabled[2].enabled);
  CHECK(!disabled[3].enabled);
  CHECK(disabled[4].enabled);
  CHECK(std::get<CenterActiveCombatantAction>(
      disabled[3].payload).combatant == 2);
  const LogicalPoint disabled_center_pointer{
      disabled[3].bounds.x + disabled[3].bounds.width / 2.0,
      disabled[3].bounds.y + disabled[3].bounds.height / 2.0,
  };
  CHECK(std::ranges::none_of(
      disabled,
      [disabled_center_pointer](const auto& control) {
        return control.enabled &&
            control.bounds.contains(disabled_center_pointer);
      }));

  const auto guard_only = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .guard_available = true,
  });
  CHECK(guard_only.size() == 1U);
  CHECK(guard_only.front().kind == ShellControlKind::guard_combatant);
  CHECK(std::ranges::none_of(
      guard_only,
      [](const auto& control) {
        return control.kind == ShellControlKind::combat_action_page;
      }));

  const auto finish_only = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .finish_combatant = CombatantId{2},
      .finish_available = true,
  });
  CHECK(finish_only.size() == 1U);
  CHECK(finish_only.front().kind == ShellControlKind::finish_combatant);

  const auto delay_only = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .delay_combatant = CombatantId{2},
      .delay_available = true,
  });
  CHECK(delay_only.size() == 1U);
  CHECK(delay_only.front().kind == ShellControlKind::delay_combatant);
  CHECK(delay_only.front().enabled);
  CHECK(std::get<DelayCombatantAction>(delay_only.front().payload).combatant ==
      2);

  const auto center_only = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .center_active_combatant = CombatantId{2},
      .center_active_available = true,
  });
  CHECK(center_only.size() == 1U);
  CHECK(center_only.front().kind ==
      ShellControlKind::center_active_combatant);
  CHECK(center_only.front().enabled);
  CHECK(std::get<CenterActiveCombatantAction>(
      center_only.front().payload).combatant == 2);

  const auto weapon_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
  });
  CHECK(weapon_disabled.size() == 2U);
  CHECK(weapon_disabled[0].kind == ShellControlKind::combat_action_page);
  CHECK(weapon_disabled[1].kind == ShellControlKind::switch_weapon_set);
  CHECK(!weapon_disabled[1].enabled);
  CHECK(std::get<SwitchWeaponSetAction>(
      weapon_disabled[1].payload).combatant == 2);

  const auto previous_only = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .center_previous_combatant = CombatantId{2},
      .center_previous_available = true,
  });
  CHECK(previous_only.size() == 2U);
  CHECK(previous_only[0].kind == ShellControlKind::combat_action_page);
  CHECK(previous_only[1].kind == ShellControlKind::cycle_combat_focus);
  CHECK(std::get<CycleCombatFocusAction>(
      previous_only[1].payload).direction == CombatFocusDirection::previous);

  const auto focus_pair = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .center_previous_combatant = CombatantId{2},
      .center_previous_available = true,
      .center_next_combatant = CombatantId{2},
      .center_next_available = true,
  });
  CHECK(focus_pair.size() == 3U);
  CHECK(focus_pair[0].kind == ShellControlKind::combat_action_page);
  CHECK(std::get<CycleCombatFocusAction>(
      focus_pair[1].payload).direction == CombatFocusDirection::previous);
  CHECK(std::get<CycleCombatFocusAction>(
      focus_pair[2].payload).direction == CombatFocusDirection::next);

  const auto items_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .combat_items = OpenCombatItemsAction{2, 4},
  });
  CHECK(items_disabled.size() == 2U);
  CHECK(items_disabled[0].kind == ShellControlKind::combat_action_page);
  CHECK(items_disabled[1].kind == ShellControlKind::open_combat_items);
  CHECK(!items_disabled[1].enabled);
  CHECK(std::get<OpenCombatItemsAction>(
      items_disabled[1].payload) == (OpenCombatItemsAction{2, 4}));

  const auto auto_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
  });
  CHECK(auto_disabled.size() == 2U);
  CHECK(auto_disabled[0].kind == ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      auto_disabled[0].payload).page == CombatActionPage::secondary);
  CHECK(auto_disabled[1].kind == ShellControlKind::auto_combatant);
  CHECK(!auto_disabled[1].enabled);
  CHECK(std::get<AutoCombatantAction>(
      auto_disabled[1].payload).combatant == 2);

  const auto range_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .show_combat_range_combatant = CombatantId{2},
  });
  CHECK(range_disabled.size() == 2U);
  CHECK(range_disabled[0].kind == ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      range_disabled[0].payload).page == CombatActionPage::secondary);
  CHECK(range_disabled[1].kind == ShellControlKind::show_combat_range);
  CHECK(!range_disabled[1].enabled);
  CHECK(std::get<ShowCombatRangeAction>(
      range_disabled[1].payload).combatant == 2);

  const auto bandage_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .bandage_combatant = CombatantId{2},
  });
  CHECK(bandage_disabled.size() == 2U);
  CHECK(bandage_disabled[0].kind == ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      bandage_disabled[0].payload).page == CombatActionPage::secondary);
  CHECK(bandage_disabled[1].kind == ShellControlKind::bandage_combatant);
  CHECK(!bandage_disabled[1].enabled);
  CHECK(std::get<BandageCombatantAction>(
      bandage_disabled[1].payload).combatant == 2);

  const auto undo_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .undo_combatant = CombatantId{2},
  });
  CHECK(undo_disabled.size() == 2U);
  CHECK(undo_disabled[0].kind == ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      undo_disabled[0].payload).page == CombatActionPage::secondary);
  CHECK(undo_disabled[1].kind == ShellControlKind::undo_combatant);
  CHECK(!undo_disabled[1].enabled);
  CHECK(std::get<UndoCombatantAction>(
      undo_disabled[1].payload).combatant == 2);

  const auto combat_spellbook_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{2},
  });
  CHECK(combat_spellbook_disabled.size() == 2U);
  CHECK(combat_spellbook_disabled[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      combat_spellbook_disabled[0].payload).page ==
      CombatActionPage::utility);
  CHECK(combat_spellbook_disabled[1].kind ==
      ShellControlKind::open_combat_spellbook);
  CHECK(!combat_spellbook_disabled[1].enabled);
  CHECK(std::get<OpenCombatSpellbookAction>(
      combat_spellbook_disabled[1].payload).combatant == 2);

  const auto combat_targeting_disabled = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_targeting = CombatantId{2},
  });
  CHECK(combat_targeting_disabled.size() == 2U);
  CHECK(combat_targeting_disabled[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      combat_targeting_disabled[0].payload).page ==
      CombatActionPage::utility);
  CHECK(combat_targeting_disabled[1].kind ==
      ShellControlKind::open_combat_targeting);
  CHECK(!combat_targeting_disabled[1].enabled);
  CHECK(std::get<OpenCombatTargetingAction>(
      combat_targeting_disabled[1].payload).combatant == 2);

  const auto auto_only_primary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
  });
  CHECK(auto_only_primary.size() == 1U);
  CHECK(auto_only_primary[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      auto_only_primary[0].payload).page == CombatActionPage::secondary);

  const auto auto_only_secondary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
  });
  CHECK(auto_only_secondary.size() == 2U);
  CHECK(std::get<SetCombatActionPageAction>(
      auto_only_secondary[0].payload).page == CombatActionPage::primary);
  CHECK(std::get<SetCombatActionPageAction>(
      auto_only_secondary[1].payload).page == CombatActionPage::utility);
  CHECK(auto_only_secondary[0].bounds.width == 44.0);
  CHECK(auto_only_secondary[1].bounds.width == 44.0);
  CHECK(!interiors_overlap(
      auto_only_secondary[0].bounds,
      auto_only_secondary[1].bounds));

  const auto range_only_primary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
  });
  CHECK(range_only_primary.size() == 1U);
  CHECK(range_only_primary[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      range_only_primary[0].payload).page == CombatActionPage::secondary);

  const auto range_only_secondary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
  });
  CHECK(range_only_secondary.size() == 2U);
  CHECK(std::get<SetCombatActionPageAction>(
      range_only_secondary[0].payload).page == CombatActionPage::primary);
  CHECK(std::get<SetCombatActionPageAction>(
      range_only_secondary[1].payload).page == CombatActionPage::utility);

  const auto bandage_only_primary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = true,
  });
  CHECK(bandage_only_primary.size() == 1U);
  CHECK(bandage_only_primary[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      bandage_only_primary[0].payload).page == CombatActionPage::secondary);

  const auto bandage_only_secondary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = true,
  });
  CHECK(bandage_only_secondary.size() == 2U);
  CHECK(std::get<SetCombatActionPageAction>(
      bandage_only_secondary[0].payload).page == CombatActionPage::primary);
  CHECK(std::get<SetCombatActionPageAction>(
      bandage_only_secondary[1].payload).page == CombatActionPage::utility);

  const auto undo_only_primary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .undo_combatant = CombatantId{2},
      .undo_combatant_available = true,
  });
  CHECK(undo_only_primary.size() == 1U);
  CHECK(undo_only_primary[0].kind == ShellControlKind::combat_action_page);
  CHECK(std::get<SetCombatActionPageAction>(
      undo_only_primary[0].payload).page == CombatActionPage::secondary);

  const auto undo_only_secondary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .undo_combatant = CombatantId{2},
      .undo_combatant_available = true,
  });
  CHECK(undo_only_secondary.size() == 2U);
  CHECK(std::get<SetCombatActionPageAction>(
      undo_only_secondary[0].payload).page == CombatActionPage::primary);
  CHECK(std::get<SetCombatActionPageAction>(
      undo_only_secondary[1].payload).page == CombatActionPage::utility);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .combat_action_page = CombatActionPage::utility,
  }).empty());

  const auto weapon_and_items = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
      .switch_weapon_available = true,
      .combat_items = OpenCombatItemsAction{2, 4},
      .combat_items_available = true,
  });
  CHECK(weapon_and_items.size() == 3U);
  CHECK(weapon_and_items[1].kind == ShellControlKind::switch_weapon_set);
  CHECK(weapon_and_items[2].kind == ShellControlKind::open_combat_items);

  const auto more_without_weapon = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .guard_available = true,
      .center_next_combatant = CombatantId{2},
      .center_next_available = true,
  });
  CHECK(more_without_weapon.size() == 2U);
  CHECK(more_without_weapon[0].kind == ShellControlKind::guard_combatant);
  CHECK(more_without_weapon[1].kind ==
      ShellControlKind::combat_action_page);

  const auto more_with_only_items = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .guard_available = true,
      .combat_items = OpenCombatItemsAction{2, 4},
  });
  CHECK(more_with_only_items.size() == 2U);
  CHECK(more_with_only_items[0].kind == ShellControlKind::guard_combatant);
  CHECK(more_with_only_items[1].kind ==
      ShellControlKind::combat_action_page);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .finish_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_available = true,
      .finish_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .delay_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .delay_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .delay_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .center_active_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .center_active_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .center_active_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .center_active_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .center_previous_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .center_previous_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .center_next_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .center_next_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .combat_items_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .combat_items = OpenCombatItemsAction{-1, 2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .combat_items = OpenCombatItemsAction{256, 2},
  }).empty());
  const auto combat_items_member_boundaries = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::secondary,
      .combat_items = OpenCombatItemsAction{2, PartyMemberId{0xFF}},
      .combat_items_available = true,
  });
  CHECK(combat_items_member_boundaries.size() == 2U);
  CHECK(std::get<OpenCombatItemsAction>(
      combat_items_member_boundaries[1].payload).member == 0xFF);
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .show_combat_range_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .show_combat_range_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .show_combat_range_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .bandage_combatant_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .bandage_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .bandage_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .undo_combatant_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .undo_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .undo_combatant = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_targeting_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_targeting = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_targeting = CombatantId{256},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{-1},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .delay_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .finish_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .finish_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .delay_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .center_active_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{2},
      .center_previous_combatant = CombatantId{2},
      .center_next_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{2},
      .center_previous_combatant = CombatantId{2},
      .center_next_combatant = CombatantId{2},
      .combat_items = OpenCombatItemsAction{3, 4},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .auto_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .show_combat_range_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .show_combat_range_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .bandage_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .show_combat_range_combatant = CombatantId{2},
      .bandage_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .undo_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::utility,
      .bandage_combatant = CombatantId{2},
      .undo_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .open_combat_spellbook = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .open_combat_targeting = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{2},
      .open_combat_targeting = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .navigation_available = true,
      .guard_combatant = CombatantId{2},
      .guard_available = true,
      .finish_combatant = CombatantId{2},
      .finish_available = true,
      .delay_combatant = CombatantId{2},
      .delay_available = true,
      .center_active_combatant = CombatantId{2},
      .center_active_available = true,
      .switch_weapon_combatant = CombatantId{2},
      .switch_weapon_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .inventory_member = PartyMemberId{2},
      .inventory_available = true,
      .delay_combatant = CombatantId{2},
      .delay_available = true,
      .center_active_combatant = CombatantId{2},
      .center_active_available = true,
      .switch_weapon_combatant = CombatantId{2},
      .switch_weapon_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .guard_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .finish_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .delay_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .center_active_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .switch_weapon_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .center_previous_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .center_next_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .combat_items = OpenCombatItemsAction{2, 4},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .auto_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .show_combat_range_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .bandage_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .undo_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .combat_action_page = CombatActionPage::secondary,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .combat_action_page = static_cast<CombatActionPage>(255),
  }).empty());

  const LogicalRect narrow_panel{0.0, 0.0, 110.0, 150.0};
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = narrow_panel,
      .guard_combatant = CombatantId{2},
  }).size() == 1U);
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = narrow_panel,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
  }).empty());

  const LogicalRect one_minimum{0.0, 0.0, 72.0, 120.0};
  const auto one_control = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = one_minimum,
      .delay_combatant = CombatantId{2},
  });
  CHECK(one_control.size() == 1U);
  CHECK(one_control.front().bounds.width == 44.0);
  CHECK(one_control.front().bounds.height == 44.0);

  const LogicalRect two_minimum{0.0, 0.0, 122.0, 120.0};
  const auto two_controls = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
  });
  CHECK(two_controls.size() == 2U);
  CHECK(two_controls[0].bounds.width == 44.0);
  CHECK(two_controls[1].bounds.width == 44.0);

  const LogicalRect three_minimum{0.0, 0.0, 172.0, 120.0};
  const auto three_controls = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = three_minimum,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
  });
  CHECK(three_controls.size() == 3U);
  for (const auto& control : three_controls) {
    CHECK(control.bounds.width == 44.0);
    CHECK(control.bounds.height == 44.0);
  }
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 171.0, 120.0},
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
  }).empty());

  const LogicalRect four_minimum{0.0, 0.0, 222.0, 120.0};
  const auto four_controls = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = four_minimum,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{2},
  });
  CHECK(four_controls.size() == 5U);
  for (const auto& control : std::span{four_controls}.first(4)) {
    CHECK(control.bounds.width == 44.0);
    CHECK(control.bounds.height == 44.0);
  }
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 221.0, 120.0},
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{2},
      .switch_weapon_combatant = CombatantId{2},
  }).empty());

  const auto primary_without_secondary = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = four_minimum,
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{2},
      .delay_combatant = CombatantId{2},
      .center_active_combatant = CombatantId{2},
  });
  CHECK(primary_without_secondary.size() == 4U);
  CHECK(std::ranges::none_of(
      primary_without_secondary,
      [](const auto& control) {
        return control.kind == ShellControlKind::combat_action_page;
      }));

  const auto secondary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = one_minimum,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
      .switch_weapon_available = true,
  });
  CHECK(secondary_minimum.size() == 2U);
  CHECK(secondary_minimum[0].kind == ShellControlKind::combat_action_page);
  CHECK(secondary_minimum[0].bounds.width == 44.0);
  CHECK(secondary_minimum[0].bounds.height == 44.0);
  CHECK(secondary_minimum[1].kind == ShellControlKind::switch_weapon_set);
  CHECK(secondary_minimum[1].bounds.width == 44.0);
  CHECK(secondary_minimum[1].bounds.height == 44.0);
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 71.0, 120.0},
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
  }).empty());

  const auto auto_primary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
  });
  CHECK(auto_primary_minimum.size() == 1U);
  CHECK(auto_primary_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(auto_primary_minimum[0].bounds.width == 44.0);
  CHECK(auto_primary_minimum[0].bounds.height == 44.0);
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 121.0, 120.0},
      .auto_combatant = CombatantId{2},
  }).empty());

  const auto utility_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
  });
  CHECK(utility_minimum.size() == 2U);
  CHECK(utility_minimum[0].kind == ShellControlKind::combat_action_page);
  CHECK(utility_minimum[0].bounds.width == 44.0);
  CHECK(utility_minimum[0].bounds.height == 44.0);
  CHECK(utility_minimum[1].kind == ShellControlKind::auto_combatant);
  CHECK(utility_minimum[1].bounds.width >= 44.0);
  CHECK(utility_minimum[1].bounds.height == 44.0);
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 121.0, 120.0},
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
  }).empty());

  const auto utility_pair_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
  });
  CHECK(utility_pair_minimum.size() == 3U);
  CHECK(utility_pair_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(utility_pair_minimum[1].kind == ShellControlKind::auto_combatant);
  CHECK(utility_pair_minimum[2].kind ==
      ShellControlKind::show_combat_range);
  CHECK(utility_pair_minimum[1].bounds.width == 44.0);
  CHECK(utility_pair_minimum[2].bounds.width == 44.0);
  CHECK(utility_pair_minimum[1].bounds.height == 44.0);
  CHECK(utility_pair_minimum[2].bounds.height == 44.0);
  CHECK(!interiors_overlap(
      utility_pair_minimum[1].bounds,
      utility_pair_minimum[2].bounds));
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 121.0, 120.0},
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .show_combat_range_combatant = CombatantId{2},
  }).empty());

  const auto utility_trio_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = three_minimum,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = true,
  });
  CHECK(utility_trio_minimum.size() == 4U);
  CHECK(utility_trio_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(utility_trio_minimum[1].kind == ShellControlKind::auto_combatant);
  CHECK(utility_trio_minimum[2].kind ==
      ShellControlKind::show_combat_range);
  CHECK(utility_trio_minimum[3].kind ==
      ShellControlKind::bandage_combatant);
  for (const auto& control : std::span{utility_trio_minimum}.subspan(1)) {
    CHECK(control.bounds.width == 44.0);
    CHECK(control.bounds.height == 44.0);
  }
  CHECK(!interiors_overlap(
      utility_trio_minimum[1].bounds,
      utility_trio_minimum[2].bounds));
  CHECK(!interiors_overlap(
      utility_trio_minimum[1].bounds,
      utility_trio_minimum[3].bounds));
  CHECK(!interiors_overlap(
      utility_trio_minimum[2].bounds,
      utility_trio_minimum[3].bounds));
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 171.0, 120.0},
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .show_combat_range_combatant = CombatantId{2},
      .bandage_combatant = CombatantId{2},
  }).empty());

  const auto utility_quartet_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = four_minimum,
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = true,
      .undo_combatant = CombatantId{2},
      .undo_combatant_available = true,
  });
  CHECK(utility_quartet_minimum.size() == 5U);
  CHECK(utility_quartet_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(utility_quartet_minimum[1].kind == ShellControlKind::auto_combatant);
  CHECK(utility_quartet_minimum[2].kind ==
      ShellControlKind::show_combat_range);
  CHECK(utility_quartet_minimum[3].kind ==
      ShellControlKind::bandage_combatant);
  CHECK(utility_quartet_minimum[4].kind ==
      ShellControlKind::undo_combatant);
  for (const auto& control : std::span{utility_quartet_minimum}.subspan(1)) {
    CHECK(control.bounds.width == 44.0);
    CHECK(control.bounds.height == 44.0);
  }
  for (std::size_t index = 1; index < utility_quartet_minimum.size(); ++index) {
    for (std::size_t prior = 1; prior < index; ++prior) {
      CHECK(!interiors_overlap(
          utility_quartet_minimum[index].bounds,
          utility_quartet_minimum[prior].bounds));
    }
  }
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 221.0, 120.0},
      .combat_action_page = CombatActionPage::utility,
      .auto_combatant = CombatantId{2},
      .show_combat_range_combatant = CombatantId{2},
      .bandage_combatant = CombatantId{2},
      .undo_combatant = CombatantId{2},
  }).empty());

  const auto range_primary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
  });
  CHECK(range_primary_minimum.size() == 1U);
  CHECK(range_primary_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  const auto range_secondary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::secondary,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
  });
  CHECK(range_secondary_minimum.size() == 2U);
  CHECK(range_secondary_minimum[0].bounds.width == 44.0);
  CHECK(range_secondary_minimum[1].bounds.width == 44.0);
  CHECK(!interiors_overlap(
      range_secondary_minimum[0].bounds,
      range_secondary_minimum[1].bounds));
  const auto range_utility_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::utility,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = true,
  });
  CHECK(range_utility_minimum.size() == 2U);
  CHECK(range_utility_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(range_utility_minimum[1].kind ==
      ShellControlKind::show_combat_range);
  CHECK(range_utility_minimum[1].bounds.width >= 44.0);
  for (const auto page : {
           CombatActionPage::primary,
           CombatActionPage::secondary,
           CombatActionPage::utility,
       }) {
    CHECK(compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .action_panel = {0.0, 0.0, 121.0, 120.0},
        .combat_action_page = page,
        .show_combat_range_combatant = CombatantId{2},
    }).empty());
  }

  const auto bandage_primary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = true,
  });
  CHECK(bandage_primary_minimum.size() == 1U);
  CHECK(bandage_primary_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  const auto bandage_secondary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::secondary,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = true,
  });
  CHECK(bandage_secondary_minimum.size() == 2U);
  CHECK(bandage_secondary_minimum[0].bounds.width == 44.0);
  CHECK(bandage_secondary_minimum[1].bounds.width == 44.0);
  CHECK(!interiors_overlap(
      bandage_secondary_minimum[0].bounds,
      bandage_secondary_minimum[1].bounds));
  const auto bandage_utility_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::utility,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = true,
  });
  CHECK(bandage_utility_minimum.size() == 2U);
  CHECK(bandage_utility_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(bandage_utility_minimum[1].kind ==
      ShellControlKind::bandage_combatant);
  CHECK(bandage_utility_minimum[1].bounds.width >= 44.0);
  for (const auto page : {
           CombatActionPage::primary,
           CombatActionPage::secondary,
           CombatActionPage::utility,
       }) {
    CHECK(compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .action_panel = {0.0, 0.0, 121.0, 120.0},
        .combat_action_page = page,
        .bandage_combatant = CombatantId{2},
    }).empty());
  }

  const auto undo_primary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .undo_combatant = CombatantId{2},
      .undo_combatant_available = true,
  });
  CHECK(undo_primary_minimum.size() == 1U);
  CHECK(undo_primary_minimum[0].kind == ShellControlKind::combat_action_page);
  const auto undo_secondary_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::secondary,
      .undo_combatant = CombatantId{2},
      .undo_combatant_available = true,
  });
  CHECK(undo_secondary_minimum.size() == 2U);
  CHECK(undo_secondary_minimum[0].bounds.width == 44.0);
  CHECK(undo_secondary_minimum[1].bounds.width == 44.0);
  CHECK(!interiors_overlap(
      undo_secondary_minimum[0].bounds,
      undo_secondary_minimum[1].bounds));
  const auto undo_utility_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::utility,
      .undo_combatant = CombatantId{2},
      .undo_combatant_available = true,
  });
  CHECK(undo_utility_minimum.size() == 2U);
  CHECK(undo_utility_minimum[0].kind == ShellControlKind::combat_action_page);
  CHECK(undo_utility_minimum[1].kind == ShellControlKind::undo_combatant);
  CHECK(undo_utility_minimum[1].bounds.width >= 44.0);
  for (const auto page : {
           CombatActionPage::primary,
           CombatActionPage::secondary,
           CombatActionPage::utility,
       }) {
    CHECK(compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .action_panel = {0.0, 0.0, 121.0, 120.0},
        .combat_action_page = page,
        .undo_combatant = CombatantId{2},
    }).empty());
  }

  const auto combat_spellbook_special_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{2},
      .open_combat_spellbook_available = true,
  });
  CHECK(combat_spellbook_special_minimum.size() == 2U);
  CHECK(combat_spellbook_special_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(combat_spellbook_special_minimum[1].kind ==
      ShellControlKind::open_combat_spellbook);
  CHECK(combat_spellbook_special_minimum[0].bounds.width == 44.0);
  CHECK(combat_spellbook_special_minimum[1].bounds.width >= 44.0);
  CHECK(combat_spellbook_special_minimum[1].bounds.width <= 160.0);
  CHECK(!interiors_overlap(
      combat_spellbook_special_minimum[0].bounds,
      combat_spellbook_special_minimum[1].bounds));
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 121.0, 120.0},
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{2},
  }).empty());

  const auto targeting_special_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::special,
      .open_combat_targeting = CombatantId{2},
      .open_combat_targeting_available = true,
  });
  CHECK(targeting_special_minimum.size() == 2U);
  CHECK(targeting_special_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(targeting_special_minimum[1].kind ==
      ShellControlKind::open_combat_targeting);
  CHECK(targeting_special_minimum[0].bounds.width == 44.0);
  CHECK(targeting_special_minimum[1].bounds.width >= 44.0);
  CHECK(!interiors_overlap(
      targeting_special_minimum[0].bounds,
      targeting_special_minimum[1].bounds));

  const auto complete_special_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{2},
      .open_combat_spellbook_available = true,
      .open_combat_targeting = CombatantId{2},
      .open_combat_targeting_available = true,
  });
  CHECK(complete_special_minimum.size() == 3U);
  CHECK(complete_special_minimum[0].kind ==
      ShellControlKind::combat_action_page);
  CHECK(complete_special_minimum[1].kind ==
      ShellControlKind::open_combat_spellbook);
  CHECK(complete_special_minimum[2].kind ==
      ShellControlKind::open_combat_targeting);
  for (const auto& control : complete_special_minimum) {
    CHECK(control.bounds.width >= 44.0);
    CHECK(control.bounds.height == 44.0);
  }
  CHECK(!interiors_overlap(
      complete_special_minimum[0].bounds,
      complete_special_minimum[1].bounds));
  CHECK(!interiors_overlap(
      complete_special_minimum[1].bounds,
      complete_special_minimum[2].bounds));
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 121.0, 120.0},
      .combat_action_page = CombatActionPage::special,
      .open_combat_spellbook = CombatantId{2},
      .open_combat_targeting = CombatantId{2},
  }).empty());

  const auto auto_navigation_minimum = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = two_minimum,
      .combat_action_page = CombatActionPage::secondary,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = true,
  });
  CHECK(auto_navigation_minimum.size() == 2U);
  CHECK(auto_navigation_minimum[0].bounds.width == 44.0);
  CHECK(auto_navigation_minimum[1].bounds.width == 44.0);
  CHECK(!interiors_overlap(
      auto_navigation_minimum[0].bounds,
      auto_navigation_minimum[1].bounds));
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 121.0, 120.0},
      .combat_action_page = CombatActionPage::secondary,
      .auto_combatant = CombatantId{2},
  }).empty());

  const auto three_secondary_controls = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = three_minimum,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
      .switch_weapon_available = true,
      .center_previous_combatant = CombatantId{2},
      .center_previous_available = true,
      .center_next_combatant = CombatantId{2},
      .center_next_available = true,
  });
  CHECK(three_secondary_controls.size() == 4U);
  CHECK(three_secondary_controls[0].kind ==
      ShellControlKind::combat_action_page);
  for (const auto& control :
      std::span{three_secondary_controls}.subspan(1)) {
    CHECK(control.bounds.width == 44.0);
    CHECK(control.bounds.height == 44.0);
  }
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 171.0, 120.0},
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
      .center_previous_combatant = CombatantId{2},
      .center_next_combatant = CombatantId{2},
  }).empty());

  const auto four_secondary_controls = compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = four_minimum,
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
      .switch_weapon_available = true,
      .center_previous_combatant = CombatantId{2},
      .center_previous_available = true,
      .center_next_combatant = CombatantId{2},
      .center_next_available = true,
      .combat_items = OpenCombatItemsAction{2, 4},
      .combat_items_available = true,
  });
  CHECK(four_secondary_controls.size() == 5U);
  CHECK(four_secondary_controls[0].kind ==
      ShellControlKind::combat_action_page);
  for (const auto& control :
      std::span{four_secondary_controls}.subspan(1)) {
    CHECK(control.bounds.width == 44.0);
    CHECK(control.bounds.height == 44.0);
  }
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = {0.0, 0.0, 221.0, 120.0},
      .combat_action_page = CombatActionPage::secondary,
      .switch_weapon_combatant = CombatantId{2},
      .center_previous_combatant = CombatantId{2},
      .center_next_combatant = CombatantId{2},
      .combat_items = OpenCombatItemsAction{2, 4},
  }).empty());
}

} // namespace

int main() {
  try {
    test_canonical_sizes();
    test_payload_order_and_disabled_state();
    test_open_inventory_control();
    test_spellbook_save_and_load_controls_at_combined_minimum_layout();
    test_combat_primary_and_secondary_action_pages();
    test_fail_closed_inputs();
    std::cout << "ShellControlLayoutTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ShellControlLayoutTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
