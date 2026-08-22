#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

constexpr std::array kWorldPages{
    WorldActionPage::travel,
    WorldActionPage::party,
    WorldActionPage::game,
};

void verify_world_tabs(
    const std::vector<ShellControlPlacement>& controls,
    LogicalRect panel,
    WorldActionPage active_page) {
  constexpr std::array<const char*, 3> labels{
      "TRAVEL", "PARTY", "GAME"};
  constexpr std::array<const char*, 3> accessibility_labels{
      "Travel commands tab",
      "Party commands tab",
      "Game commands tab",
  };
  constexpr std::array<const char*, 3> focus_identifiers{
      "focus.action.world.page.travel",
      "focus.action.world.page.party",
      "focus.action.world.page.game",
  };

  CHECK(controls.size() >= kWorldPages.size());
  std::set<uint32_t> regions;
  std::set<std::string> focus_ids;
  size_t selected_count = 0U;
  int32_t previous_tab_order = -1;
  for (size_t index = 0; index < controls.size(); ++index) {
    const auto& control = controls[index];
    CHECK(control.region.is_valid());
    CHECK(regions.emplace(control.region.value).second);
    CHECK(focus_ids.emplace(control.focus_identifier).second);
    CHECK(panel.contains(control.bounds));
    CHECK(control.bounds.width >= 44.0);
    CHECK(control.bounds.height >= 44.0);
    CHECK(control.tab_order > previous_tab_order);
    previous_tab_order = control.tab_order;
    for (size_t prior = 0; prior < index; ++prior) {
      CHECK(!interiors_overlap(control.bounds, controls[prior].bounds));
    }
  }

  for (size_t index = 0; index < kWorldPages.size(); ++index) {
    const auto& tab = controls[index];
    CHECK(tab.region.value == 1210U + index);
    CHECK(tab.kind == ShellControlKind::world_action_page);
    CHECK(tab.label == labels[index]);
    const std::string expected_accessibility_label =
        std::string(accessibility_labels[index]) +
        (active_page == kWorldPages[index] ? ", selected" : "");
    CHECK(tab.accessibility_label == expected_accessibility_label);
    CHECK(tab.focus_identifier == focus_identifiers[index]);
    CHECK(tab.tab_order == 900 + static_cast<int32_t>(index));
    CHECK(tab.enabled);
    CHECK(tab.selected == (active_page == kWorldPages[index]));
    CHECK(tab.bounds.width <= 112.0);
    selected_count += tab.selected ? 1U : 0U;
    CHECK(std::holds_alternative<SetWorldActionPageAction>(tab.payload));
    CHECK(action_name(tab.payload) == "set_world_action_page");
    CHECK(std::get<SetWorldActionPageAction>(tab.payload).page ==
        kWorldPages[index]);
    CHECK(is_valid_world_action_page_transition(
        active_page,
        std::get<SetWorldActionPageAction>(tab.payload).page));
  }
  CHECK(selected_count == 1U);
  for (size_t index = kWorldPages.size(); index < controls.size(); ++index) {
    CHECK(!controls[index].selected);
  }
}

void verify_layout(
    const ShellControlLayoutRequest& request,
    size_t expected_count) {
  const auto controls = compute_shell_control_layout(request);
  CHECK(controls.size() == kWorldPages.size() + expected_count);
  verify_world_tabs(controls, request.action_panel, request.world_action_page);
  for (size_t index = kWorldPages.size(); index < controls.size(); ++index) {
    const auto& control = controls[index];
    CHECK(control.enabled == request.navigation_available);
    CHECK(control.kind == ShellControlKind::movement);
    CHECK(std::holds_alternative<MovePartyAction>(control.payload));
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
  CHECK(outdoor.size() == kWorldPages.size() + expected.size());
  verify_world_tabs(outdoor, panel, WorldActionPage::travel);
  for (size_t index = 0; index < expected.size(); ++index) {
    const auto& movement = outdoor[kWorldPages.size() + index];
    CHECK(!movement.enabled);
    CHECK(std::get<MovePartyAction>(movement.payload).command ==
        expected[index]);
    CHECK(movement.region.value == expected_outdoor_regions[index]);
  }

  const auto dungeon = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_map,
      .action_panel = panel,
      .navigation_available = true,
  });
  verify_world_tabs(dungeon, panel, WorldActionPage::travel);
  CHECK(std::get<MovePartyAction>(dungeon[3].payload).command ==
      MovementCommand::turn_left);
  CHECK(std::get<MovePartyAction>(dungeon[4].payload).command ==
      MovementCommand::step_forward);
  CHECK(std::get<MovePartyAction>(dungeon[5].payload).command ==
      MovementCommand::step_backward);
  CHECK(std::get<MovePartyAction>(dungeon[6].payload).command ==
      MovementCommand::turn_right);
  constexpr std::array<uint32_t, 4> expected_dungeon_regions{
      1002, 1000, 1001, 1003};
  for (size_t index = 0; index < expected_dungeon_regions.size(); ++index) {
    CHECK(dungeon[kWorldPages.size() + index].region.value ==
        expected_dungeon_regions[index]);
  }

  const auto first_person = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .navigation_available = true,
  });
  CHECK(first_person.size() ==
      kWorldPages.size() + expected_dungeon_regions.size());
  verify_world_tabs(first_person, panel, WorldActionPage::travel);
  for (size_t index = 0; index < expected_dungeon_regions.size(); ++index) {
    const size_t control_index = kWorldPages.size() + index;
    CHECK(first_person[control_index].region.value ==
        expected_dungeon_regions[index]);
    CHECK(first_person[control_index].payload == dungeon[control_index].payload);
  }

  std::set<uint32_t> all_semantic_regions;
  for (size_t index = kWorldPages.size(); index < outdoor.size(); ++index) {
    CHECK(all_semantic_regions.emplace(outdoor[index].region.value).second);
  }
  for (size_t index = kWorldPages.size(); index < dungeon.size(); ++index) {
    CHECK(all_semantic_regions.emplace(dungeon[index].region.value).second);
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
                 .world_action_page = WorldActionPage::party,
                 .navigation_available = true,
                 .inventory_member = PartyMemberId{2},
                 .inventory_available = true,
             },
             ShellControlLayoutRequest{
                 .screen = ScreenContext::dungeon,
                 .world_presentation = WorldPresentation::dungeon_map,
                 .action_panel = panel,
                 .world_action_page = WorldActionPage::party,
                 .navigation_available = true,
                 .inventory_member = PartyMemberId{2},
                 .inventory_available = true,
             },
      }) {
      const auto controls = compute_shell_control_layout(request);
      CHECK(controls.size() == 4U);
      verify_world_tabs(controls, request.action_panel, WorldActionPage::party);
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
    }
  }

  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  const auto disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .inventory_member = PartyMemberId{4},
      .inventory_available = false,
  });
  CHECK(disabled.size() == 4U);
  verify_world_tabs(disabled, panel, WorldActionPage::party);
  CHECK(!disabled.back().enabled);
  CHECK(std::get<OpenInventoryAction>(disabled.back().payload).member == 4);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .inventory_available = true,
  }).empty());
}

void test_open_scroll_case_control_is_distinct_and_visible_when_disabled() {
  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  for (const auto& request : {
           ShellControlLayoutRequest{
               .screen = ScreenContext::exploration,
               .world_presentation = WorldPresentation::outdoor,
               .action_panel = panel,
               .world_action_page = WorldActionPage::party,
               .navigation_available = true,
               .scroll_case_member = PartyMemberId{2},
               .scroll_case_available = true,
           },
           ShellControlLayoutRequest{
               .screen = ScreenContext::dungeon,
               .world_presentation = WorldPresentation::dungeon_first_person,
               .action_panel = panel,
               .world_action_page = WorldActionPage::party,
               .navigation_available = true,
               .scroll_case_member = PartyMemberId{2},
               .scroll_case_available = true,
           },
       }) {
    const auto controls = compute_shell_control_layout(request);
    CHECK(controls.size() == 4U);
    verify_world_tabs(controls, request.action_panel, WorldActionPage::party);
    const auto& scroll = controls.back();
    CHECK(scroll.region.value == 1108U);
    CHECK(scroll.kind == ShellControlKind::open_scroll_case);
    CHECK(scroll.label == "SCROLL");
    CHECK(scroll.accessibility_label == "Use scroll");
    CHECK(scroll.focus_identifier == "focus.action.scroll_case.open");
    CHECK(scroll.tab_order == 1108);
    CHECK(scroll.enabled);
    CHECK(std::holds_alternative<OpenScrollCaseAction>(scroll.payload));
    CHECK(std::get<OpenScrollCaseAction>(scroll.payload).member == 2);
    CHECK(request.action_panel.contains(scroll.bounds));
  }

  const auto disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .scroll_case_member = PartyMemberId{4},
      .scroll_case_available = false,
  });
  CHECK(disabled.size() == 4U);
  verify_world_tabs(disabled, panel, WorldActionPage::party);
  CHECK(disabled.back().kind == ShellControlKind::open_scroll_case);
  CHECK(!disabled.back().enabled);
  CHECK(std::get<OpenScrollCaseAction>(disabled.back().payload).member == 4);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .scroll_case_available = true,
  }).empty());

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .scroll_case_member = PartyMemberId{4},
      .guard_combatant = CombatantId{2},
  }).empty());
}

void test_open_character_sheet_control_is_the_fourth_party_action() {
  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  for (const auto& request : {
           ShellControlLayoutRequest{
               .screen = ScreenContext::exploration,
               .world_presentation = WorldPresentation::outdoor,
               .action_panel = panel,
               .world_action_page = WorldActionPage::party,
               .navigation_available = true,
               .character_sheet_member = PartyMemberId{2},
               .character_sheet_available = true,
           },
           ShellControlLayoutRequest{
               .screen = ScreenContext::dungeon,
               .world_presentation = WorldPresentation::dungeon_first_person,
               .action_panel = panel,
               .world_action_page = WorldActionPage::party,
               .navigation_available = true,
               .character_sheet_member = PartyMemberId{2},
               .character_sheet_available = true,
           },
       }) {
    const auto controls = compute_shell_control_layout(request);
    CHECK(controls.size() == 4U);
    verify_world_tabs(controls, panel, WorldActionPage::party);
    const auto& character = controls.back();
    CHECK(character.region.value == 1113U);
    CHECK(character.kind == ShellControlKind::open_character_sheet);
    CHECK(character.label == "CHARACTER");
    CHECK(character.accessibility_label ==
        "Open selected party member character sheet");
    CHECK(character.focus_identifier == "focus.action.character_sheet.open");
    CHECK(character.tab_order == 1113);
    CHECK(character.enabled);
    CHECK(action_name(character.payload) == "open_character_sheet");
    CHECK(std::holds_alternative<OpenCharacterSheetAction>(
        character.payload));
    CHECK(std::get<OpenCharacterSheetAction>(character.payload).member == 2);
    CHECK(panel.contains(character.bounds));
    CHECK(character.bounds.width >= 44.0);
    CHECK(character.bounds.height >= 44.0);
  }

  const auto disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .character_sheet_member = PartyMemberId{4},
      .character_sheet_available = false,
  });
  CHECK(disabled.size() == 4U);
  verify_world_tabs(disabled, panel, WorldActionPage::party);
  CHECK(disabled.back().kind == ShellControlKind::open_character_sheet);
  CHECK(!disabled.back().enabled);
  CHECK(std::get<OpenCharacterSheetAction>(disabled.back().payload).member ==
      4);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .character_sheet_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_map,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .character_sheet_member = PartyMemberId{6},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .inventory_member = PartyMemberId{2},
      .character_sheet_member = PartyMemberId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .character_sheet_member = PartyMemberId{2},
      .guard_combatant = CombatantId{2},
  }).empty());
}

void test_world_action_controls_at_combined_minimum_layout() {
  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1359.0, 900.0},
      LogicalSize{1360.0, 768.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto size : sizes) {
    const auto panel = action_panel_for(size);
    for (auto request : {
             ShellControlLayoutRequest{
                 .screen = ScreenContext::exploration,
                 .world_presentation = WorldPresentation::outdoor,
                 .action_panel = panel,
                 .navigation_available = true,
                 .inventory_member = PartyMemberId{2},
                 .inventory_available = true,
                 .spellbook_member = PartyMemberId{2},
                 .spellbook_available = true,
                 .scroll_case_member = PartyMemberId{2},
                 .scroll_case_available = true,
                 .character_sheet_member = PartyMemberId{2},
                 .character_sheet_available = true,
                 .save_control_visible = true,
                 .save_available = true,
                 .load_control_visible = true,
                 .load_available = true,
                 .rest_control_visible = true,
                 .rest_available = true,
                 .camp_control_visible = true,
                 .camp_available = true,
                 .camp_desired_in_camp = true,
                 .search_control_visible = true,
                 .search_available = true,
                 .search_desired_searching = true,
                 .torch_control_visible = true,
                 .torch_available = true,
                 .torch_source = TorchSource{.member = 2, .slot = 7},
                 .contextual_overview_control_visible = true,
                 .contextual_overview_available = true,
                 .contextual_overview_mode =
                     ContextualOverviewMode::area_search,
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
                 .scroll_case_member = PartyMemberId{2},
                 .scroll_case_available = true,
                 .character_sheet_member = PartyMemberId{2},
                 .character_sheet_available = true,
                 .save_control_visible = true,
                 .save_available = true,
                 .load_control_visible = true,
                 .load_available = true,
                 .rest_control_visible = true,
                 .rest_available = true,
                 .camp_control_visible = true,
                 .camp_available = true,
                 .camp_desired_in_camp = true,
                 .search_control_visible = true,
                 .search_available = true,
                 .search_desired_searching = true,
                 .torch_control_visible = true,
                 .torch_available = true,
                 .torch_source = TorchSource{.member = 2, .slot = 7},
                 .contextual_overview_control_visible = true,
                 .contextual_overview_available = true,
                 .contextual_overview_mode =
                     ContextualOverviewMode::area_search,
             },
         }) {
      std::array<std::vector<ShellControlPlacement>, 3> layouts;
      for (size_t page_index = 0; page_index < kWorldPages.size(); ++page_index) {
        request.world_action_page = kWorldPages[page_index];
        layouts[page_index] = compute_shell_control_layout(request);
        const size_t action_count = page_index == 0U
            ? (request.screen == ScreenContext::exploration ? 8U : 4U)
            : (page_index == 1U ? 4U : 7U);
        CHECK(layouts[page_index].size() == kWorldPages.size() + action_count);
        verify_world_tabs(
            layouts[page_index], panel, kWorldPages[page_index]);
      }

      // Tabs persist with stable geometry and identity on every page. Only the
      // selected state and its accessibility suffix vary by active page.
      for (size_t page_index = 0; page_index < kWorldPages.size(); ++page_index) {
        for (size_t tab_index = 0; tab_index < kWorldPages.size(); ++tab_index) {
          const auto& reference = layouts[0][tab_index];
          const auto& tab = layouts[page_index][tab_index];
          CHECK(tab.region == reference.region);
          CHECK(tab.bounds == reference.bounds);
          CHECK(tab.label == reference.label);
          CHECK(tab.focus_identifier == reference.focus_identifier);
          CHECK(tab.tab_order == reference.tab_order);
          CHECK(tab.enabled == reference.enabled);
          CHECK(tab.payload == reference.payload);
        }
      }

      const auto& party = layouts[1];
      const auto& inventory = party[3];
      const auto& spellbook = party[4];
      const auto& scroll = party[5];
      const auto& character = party[6];
      CHECK(inventory.region.value == 1100U);
      CHECK(inventory.kind == ShellControlKind::open_inventory);
      CHECK(inventory.label == "ITEMS");
      CHECK(inventory.accessibility_label == "Open inventory");
      CHECK(inventory.focus_identifier == "focus.action.inventory.open");
      CHECK(inventory.tab_order == 1100);
      CHECK(inventory.enabled);
      CHECK(inventory.payload == UIActionPayload{OpenInventoryAction{2}});
      CHECK(spellbook.region.value == 1101U);
      CHECK(spellbook.kind == ShellControlKind::open_spellbook);
      CHECK(spellbook.label == "SPELLS");
      CHECK(spellbook.accessibility_label == "Cast spell");
      CHECK(spellbook.focus_identifier == "focus.action.spellbook.open");
      CHECK(spellbook.tab_order == 1101);
      CHECK(spellbook.enabled);
      CHECK(spellbook.payload == UIActionPayload{OpenSpellbookAction{2}});
      CHECK(scroll.region.value == 1108U);
      CHECK(scroll.kind == ShellControlKind::open_scroll_case);
      CHECK(scroll.label == "SCROLL");
      CHECK(scroll.accessibility_label == "Use scroll");
      CHECK(scroll.focus_identifier == "focus.action.scroll_case.open");
      CHECK(scroll.tab_order == 1108);
      CHECK(scroll.enabled);
      CHECK(scroll.payload == UIActionPayload{OpenScrollCaseAction{2}});
      CHECK(character.region.value == 1113U);
      CHECK(character.kind == ShellControlKind::open_character_sheet);
      CHECK(character.label == "CHARACTER");
      CHECK(character.accessibility_label ==
          "Open selected party member character sheet");
      CHECK(character.focus_identifier ==
          "focus.action.character_sheet.open");
      CHECK(character.tab_order == 1113);
      CHECK(character.enabled);
      CHECK(character.payload == UIActionPayload{OpenCharacterSheetAction{2}});

      const auto& game = layouts[2];
      const auto& save = game[3];
      const auto& load = game[4];
      const auto& rest = game[5];
      const auto& camp = game[6];
      const auto& search = game[7];
      const auto& torch = game[8];
      const auto& overview = game[9];
      CHECK(save.region.value == 1102U);
      CHECK(save.kind == ShellControlKind::open_save_game);
      CHECK(save.label == "SAVE");
      CHECK(save.accessibility_label == "Open save dialog");
      CHECK(save.focus_identifier == "focus.action.save.open");
      CHECK(save.tab_order == 1102);
      CHECK(save.enabled);
      CHECK(save.payload == UIActionPayload{OpenSaveGameAction{}});
      CHECK(load.region.value == 1103U);
      CHECK(load.kind == ShellControlKind::open_load_game);
      CHECK(load.label == "LOAD");
      CHECK(load.accessibility_label == "Open load dialog");
      CHECK(load.focus_identifier == "focus.action.load.open");
      CHECK(load.tab_order == 1103);
      CHECK(load.enabled);
      CHECK(load.payload == UIActionPayload{OpenLoadGameAction{}});
      CHECK(rest.region.value == 1118U);
      CHECK(rest.kind == ShellControlKind::rest_party);
      CHECK(rest.label == "REST");
      CHECK(rest.accessibility_label == "Rest party");
      CHECK(rest.focus_identifier == "focus.action.party.rest");
      CHECK(rest.tab_order == 1118);
      CHECK(rest.enabled);
      CHECK(rest.payload == UIActionPayload{RestPartyAction{}});
      CHECK(camp.region.value == 1124U);
      CHECK(camp.kind == ShellControlKind::set_camp_state);
      CHECK(camp.label == "CAMP");
      CHECK(camp.accessibility_label == "Make camp");
      CHECK(camp.focus_identifier == "focus.action.party.camp");
      CHECK(camp.tab_order == 1124);
      CHECK(camp.enabled);
      CHECK(camp.payload == UIActionPayload{SetCampStateAction{true}});
      CHECK(search.region.value == 1125U);
      CHECK(search.kind == ShellControlKind::set_search_state);
      CHECK(search.label == "SEARCH");
      CHECK(search.accessibility_label == "Start searching");
      CHECK(search.focus_identifier == "focus.action.party.search");
      CHECK(search.tab_order == 1125);
      CHECK(search.enabled);
      CHECK(search.payload == UIActionPayload{SetSearchStateAction{true}});
      CHECK(torch.region.value == 1126U);
      CHECK(torch.kind == ShellControlKind::use_torch);
      CHECK(torch.label == "TORCH");
      CHECK(torch.accessibility_label == "Use torch");
      CHECK(torch.focus_identifier == "focus.action.party.torch");
      CHECK(torch.tab_order == 1126);
      CHECK(torch.enabled);
      CHECK((torch.payload == UIActionPayload{UseTorchAction{
          .source = TorchSource{.member = 2, .slot = 7}}}));
      CHECK(overview.region.value == 1127U);
      CHECK(overview.kind == ShellControlKind::contextual_overview);
      CHECK(overview.label == "AREA SEARCH");
      CHECK(overview.accessibility_label == "Search nearby area");
      CHECK(overview.focus_identifier == "focus.action.party.overview");
      CHECK(overview.tab_order == 1127);
      CHECK(overview.enabled);
      CHECK((overview.payload == UIActionPayload{ContextualOverviewAction{
          .mode = ContextualOverviewMode::area_search,
          .member = std::nullopt,
      }}));

      // Every tab payload is a direct destination, including selecting the
      // already-active page. Recompose each target from every origin.
      for (size_t origin_index = 0; origin_index < kWorldPages.size(); ++origin_index) {
        for (size_t tab_index = 0; tab_index < kWorldPages.size(); ++tab_index) {
          const auto target_page = std::get<SetWorldActionPageAction>(
              layouts[origin_index][tab_index].payload).page;
          auto target_request = request;
          target_request.world_action_page = target_page;
          const auto target = compute_shell_control_layout(target_request);
          CHECK(!target.empty());
          verify_world_tabs(target, panel, target_page);
        }
      }
    }
  }

  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  const auto disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .spellbook_member = PartyMemberId{4},
      .spellbook_available = false,
  });
  CHECK(disabled.size() == 4U);
  verify_world_tabs(disabled, panel, WorldActionPage::party);
  CHECK(disabled.back().kind == ShellControlKind::open_spellbook);
  CHECK(!disabled.back().enabled);
  CHECK(std::get<OpenSpellbookAction>(disabled.back().payload).member == 4);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .spellbook_available = true,
  }).empty());

  const auto save_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .save_control_visible = true,
      .save_available = false,
  });
  CHECK(save_disabled.size() == 4U);
  verify_world_tabs(save_disabled, panel, WorldActionPage::game);
  CHECK(save_disabled.back().kind == ShellControlKind::open_save_game);
  CHECK(!save_disabled.back().enabled);
  CHECK(std::holds_alternative<OpenSaveGameAction>(
      save_disabled.back().payload));

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .save_available = true,
  }).empty());

  const auto load_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .load_control_visible = true,
      .load_available = false,
  });
  CHECK(load_disabled.size() == 4U);
  verify_world_tabs(load_disabled, panel, WorldActionPage::game);
  CHECK(load_disabled.back().kind == ShellControlKind::open_load_game);
  CHECK(!load_disabled.back().enabled);
  CHECK(std::holds_alternative<OpenLoadGameAction>(
      load_disabled.back().payload));

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .load_available = true,
  }).empty());

  const auto rest_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .rest_control_visible = true,
      .rest_available = false,
  });
  CHECK(rest_disabled.size() == 4U);
  verify_world_tabs(rest_disabled, panel, WorldActionPage::game);
  CHECK(rest_disabled.back().region.value == 1118U);
  CHECK(rest_disabled.back().kind == ShellControlKind::rest_party);
  CHECK(rest_disabled.back().label == "REST");
  CHECK(rest_disabled.back().accessibility_label ==
      "Rest party, Camp first");
  CHECK(rest_disabled.back().focus_identifier == "focus.action.party.rest");
  CHECK(rest_disabled.back().tab_order == 1118);
  CHECK(!rest_disabled.back().enabled);
  CHECK(std::holds_alternative<RestPartyAction>(
      rest_disabled.back().payload));

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .rest_available = true,
  }).empty());

  const auto camp_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = false,
      .camp_control_visible = true,
      .camp_available = false,
      .camp_desired_in_camp = true,
  });
  CHECK(camp_disabled.size() == 4U);
  verify_world_tabs(camp_disabled, panel, WorldActionPage::game);
  CHECK(camp_disabled.back().region.value == 1124U);
  CHECK(camp_disabled.back().kind == ShellControlKind::set_camp_state);
  CHECK(camp_disabled.back().label == "CAMP");
  CHECK(camp_disabled.back().accessibility_label == "Make camp");
  CHECK(camp_disabled.back().focus_identifier == "focus.action.party.camp");
  CHECK(camp_disabled.back().tab_order == 1124);
  CHECK(!camp_disabled.back().enabled);
  CHECK(camp_disabled.back().payload ==
      UIActionPayload{SetCampStateAction{true}});

  const auto break_camp = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .camp_control_visible = true,
      .camp_available = true,
      .camp_desired_in_camp = false,
  });
  CHECK(break_camp.size() == 4U);
  verify_world_tabs(break_camp, panel, WorldActionPage::game);
  CHECK(break_camp.back().label == "BREAK CAMP");
  CHECK(break_camp.back().accessibility_label == "Break camp");
  CHECK(break_camp.back().enabled);
  CHECK(break_camp.back().payload ==
      UIActionPayload{SetCampStateAction{false}});

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .camp_available = true,
      .camp_desired_in_camp = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = false,
      .camp_control_visible = true,
      .camp_available = true,
      .camp_desired_in_camp = true,
  }).empty());

  const auto search_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = false,
      .search_control_visible = true,
      .search_available = false,
      .search_desired_searching = true,
  });
  CHECK(search_disabled.size() == 4U);
  verify_world_tabs(search_disabled, panel, WorldActionPage::game);
  CHECK(search_disabled.back().region.value == 1125U);
  CHECK(search_disabled.back().kind == ShellControlKind::set_search_state);
  CHECK(search_disabled.back().label == "SEARCH");
  CHECK(search_disabled.back().accessibility_label == "Start searching");
  CHECK(search_disabled.back().focus_identifier ==
      "focus.action.party.search");
  CHECK(search_disabled.back().tab_order == 1125);
  CHECK(!search_disabled.back().enabled);
  CHECK(search_disabled.back().payload ==
      UIActionPayload{SetSearchStateAction{true}});

  const auto stop_search = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .search_control_visible = true,
      .search_available = true,
      .search_desired_searching = false,
  });
  CHECK(stop_search.size() == 4U);
  verify_world_tabs(stop_search, panel, WorldActionPage::game);
  CHECK(stop_search.back().label == "STOP SEARCH");
  CHECK(stop_search.back().accessibility_label == "Stop searching");
  CHECK(stop_search.back().enabled);
  CHECK(stop_search.back().payload ==
      UIActionPayload{SetSearchStateAction{false}});

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .search_available = true,
      .search_desired_searching = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = false,
      .search_control_visible = true,
      .search_available = true,
      .search_desired_searching = true,
  }).empty());

  const auto torch_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .torch_control_visible = true,
      .torch_available = false,
  });
  CHECK(torch_disabled.size() == 4U);
  verify_world_tabs(torch_disabled, panel, WorldActionPage::game);
  CHECK(torch_disabled.back().region.value == 1126U);
  CHECK(torch_disabled.back().kind == ShellControlKind::use_torch);
  CHECK(torch_disabled.back().label == "TORCH");
  CHECK(torch_disabled.back().accessibility_label ==
      "Use torch, no usable torch");
  CHECK(torch_disabled.back().focus_identifier ==
      "focus.action.party.torch");
  CHECK(torch_disabled.back().tab_order == 1126);
  CHECK(!torch_disabled.back().enabled);
  CHECK(!std::get<UseTorchAction>(torch_disabled.back().payload).source);

  const TorchSource torch_source{.member = 4, .slot = 29};
  const auto torch_enabled = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .torch_control_visible = true,
      .torch_available = true,
      .torch_source = torch_source,
  });
  CHECK(torch_enabled.size() == 4U);
  verify_world_tabs(torch_enabled, panel, WorldActionPage::game);
  CHECK(torch_enabled.back().accessibility_label == "Use torch");
  CHECK(torch_enabled.back().enabled);
  CHECK(std::get<UseTorchAction>(torch_enabled.back().payload).source ==
      std::optional<TorchSource>{torch_source});

  const auto sourced_but_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .torch_control_visible = true,
      .torch_available = false,
      .torch_source = torch_source,
  });
  CHECK(sourced_but_disabled.size() == 4U);
  CHECK(sourced_but_disabled.back().accessibility_label == "Use torch");
  CHECK(!sourced_but_disabled.back().enabled);
  CHECK(std::get<UseTorchAction>(
      sourced_but_disabled.back().payload).source ==
      std::optional<TorchSource>{torch_source});

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .torch_available = true,
      .torch_source = torch_source,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = false,
      .torch_control_visible = true,
      .torch_available = true,
      .torch_source = torch_source,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .torch_control_visible = true,
      .torch_available = true,
  }).empty());

  const auto area_search_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = false,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = false,
      .contextual_overview_mode = ContextualOverviewMode::area_search,
  });
  CHECK(area_search_disabled.size() == 4U);
  verify_world_tabs(
      area_search_disabled, panel, WorldActionPage::game);
  const auto& disabled_area_search = area_search_disabled.back();
  CHECK(disabled_area_search.region.value == 1127U);
  CHECK(disabled_area_search.kind ==
      ShellControlKind::contextual_overview);
  CHECK(disabled_area_search.label == "AREA SEARCH");
  CHECK(disabled_area_search.accessibility_label ==
      "Search nearby area, unavailable");
  CHECK(disabled_area_search.focus_identifier ==
      "focus.action.party.overview");
  CHECK(disabled_area_search.tab_order == 1127);
  CHECK(!disabled_area_search.enabled);
  CHECK((disabled_area_search.payload ==
      UIActionPayload{ContextualOverviewAction{
          .mode = ContextualOverviewMode::area_search,
          .member = std::nullopt,
      }}));

  const auto area_search_enabled = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = true,
      .contextual_overview_mode = ContextualOverviewMode::area_search,
  });
  CHECK(area_search_enabled.size() == 4U);
  verify_world_tabs(area_search_enabled, panel, WorldActionPage::game);
  const auto& enabled_area_search = area_search_enabled.back();
  CHECK(enabled_area_search.accessibility_label == "Search nearby area");
  CHECK(enabled_area_search.enabled);
  CHECK((enabled_area_search.payload ==
      UIActionPayload{ContextualOverviewAction{
          .mode = ContextualOverviewMode::area_search,
          .member = std::nullopt,
      }}));

  const auto make_scroll_no_member = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_first_person,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = false,
      .contextual_overview_mode = ContextualOverviewMode::make_scroll,
  });
  CHECK(make_scroll_no_member.size() == 4U);
  verify_world_tabs(make_scroll_no_member, panel, WorldActionPage::game);
  const auto& disabled_make_scroll_no_member =
      make_scroll_no_member.back();
  CHECK(disabled_make_scroll_no_member.region.value == 1127U);
  CHECK(disabled_make_scroll_no_member.kind ==
      ShellControlKind::contextual_overview);
  CHECK(disabled_make_scroll_no_member.label == "MAKE SCROLL");
  CHECK(disabled_make_scroll_no_member.accessibility_label ==
      "Make scroll, select a party member first");
  CHECK(disabled_make_scroll_no_member.focus_identifier ==
      "focus.action.party.overview");
  CHECK(disabled_make_scroll_no_member.tab_order == 1127);
  CHECK(!disabled_make_scroll_no_member.enabled);
  CHECK((disabled_make_scroll_no_member.payload ==
      UIActionPayload{ContextualOverviewAction{
          .mode = ContextualOverviewMode::make_scroll,
          .member = std::nullopt,
      }}));

  const auto make_scroll_member_disabled = compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = false,
      .contextual_overview_mode = ContextualOverviewMode::make_scroll,
      .contextual_overview_member = PartyMemberId{4},
  });
  CHECK(make_scroll_member_disabled.size() == 4U);
  verify_world_tabs(
      make_scroll_member_disabled, panel, WorldActionPage::game);
  CHECK(make_scroll_member_disabled.back().accessibility_label ==
      "Make scroll, unavailable for selected party member");
  CHECK(!make_scroll_member_disabled.back().enabled);
  CHECK((make_scroll_member_disabled.back().payload ==
      UIActionPayload{ContextualOverviewAction{
          .mode = ContextualOverviewMode::make_scroll,
          .member = PartyMemberId{4},
      }}));

  const auto make_scroll_enabled = compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_map,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = true,
      .contextual_overview_mode = ContextualOverviewMode::make_scroll,
      .contextual_overview_member = PartyMemberId{4},
  });
  CHECK(make_scroll_enabled.size() == 4U);
  verify_world_tabs(make_scroll_enabled, panel, WorldActionPage::game);
  const auto& enabled_make_scroll = make_scroll_enabled.back();
  CHECK(enabled_make_scroll.label == "MAKE SCROLL");
  CHECK(enabled_make_scroll.accessibility_label ==
      "Make scroll for selected party member");
  CHECK(enabled_make_scroll.enabled);
  CHECK((enabled_make_scroll.payload ==
      UIActionPayload{ContextualOverviewAction{
          .mode = ContextualOverviewMode::make_scroll,
          .member = PartyMemberId{4},
      }}));

  // The contextual command changes meaning without moving or changing its
  // keyboard/accessibility identity.
  CHECK(enabled_make_scroll.region == enabled_area_search.region);
  CHECK(enabled_make_scroll.bounds == enabled_area_search.bounds);
  CHECK(enabled_make_scroll.focus_identifier ==
      enabled_area_search.focus_identifier);
  CHECK(enabled_make_scroll.tab_order == enabled_area_search.tab_order);

  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_available = true,
      .contextual_overview_mode = ContextualOverviewMode::area_search,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = false,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = true,
      .contextual_overview_mode = ContextualOverviewMode::area_search,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = false,
      .contextual_overview_mode = ContextualOverviewMode::area_search,
      .contextual_overview_member = PartyMemberId{4},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = true,
      .contextual_overview_mode = ContextualOverviewMode::make_scroll,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_available = false,
      .contextual_overview_mode = ContextualOverviewMode::make_scroll,
      .contextual_overview_member = PartyMemberId{6},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::travel,
      .navigation_available = true,
      .contextual_overview_mode = ContextualOverviewMode::make_scroll,
      .contextual_overview_member = PartyMemberId{0},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .contextual_overview_control_visible = true,
      .contextual_overview_mode =
          static_cast<ContextualOverviewMode>(255),
  }).empty());

  // Dungeon Travel, the reserved four-slot Party page, and the seven-slot GAME
  // page are all reachable at this exact 44-point floor. One point less in
  // either dimension fails the whole persistent deck closed on every page.
  constexpr LogicalRect exact_minimum{0.0, 0.0, 372.0, 120.0};
  for (const auto page : kWorldPages) {
    const ShellControlLayoutRequest exact_request{
        .screen = ScreenContext::dungeon,
        .world_presentation = WorldPresentation::dungeon_map,
        .action_panel = exact_minimum,
        .world_action_page = page,
        .navigation_available = true,
        .inventory_member = PartyMemberId{2},
        .inventory_available = true,
        .spellbook_member = PartyMemberId{2},
        .spellbook_available = true,
        .scroll_case_member = PartyMemberId{2},
        .scroll_case_available = true,
        .character_sheet_member = PartyMemberId{2},
        .character_sheet_available = true,
        .save_control_visible = true,
        .save_available = true,
        .load_control_visible = true,
        .load_available = true,
        .rest_control_visible = true,
        .rest_available = true,
        .camp_control_visible = true,
        .camp_available = true,
        .camp_desired_in_camp = true,
        .search_control_visible = true,
        .search_available = true,
        .search_desired_searching = true,
        .torch_control_visible = true,
        .torch_available = true,
        .torch_source = TorchSource{.member = 2, .slot = 7},
        .contextual_overview_control_visible = true,
        .contextual_overview_available = true,
        .contextual_overview_mode = ContextualOverviewMode::area_search,
    };
    const auto exact = compute_shell_control_layout(exact_request);
    CHECK(!exact.empty());
    verify_world_tabs(exact, exact_minimum, page);
    if (page == WorldActionPage::game) {
      CHECK(exact.size() == kWorldPages.size() + 7U);
      for (size_t index = kWorldPages.size(); index < exact.size(); ++index) {
        CHECK(exact[index].bounds.width == 44.0);
        CHECK(exact[index].bounds.height == 44.0);
      }
      CHECK(exact.back().kind == ShellControlKind::contextual_overview);
    }
    auto narrow_request = exact_request;
    narrow_request.action_panel.width = 371.0;
    CHECK(compute_shell_control_layout(narrow_request).empty());
    auto short_request = exact_request;
    short_request.action_panel.height = 119.0;
    CHECK(compute_shell_control_layout(short_request).empty());
  }
}

void test_fail_closed_inputs() {
  const LogicalRect usable{0.0, 0.0, 800.0, 150.0};
  CHECK(!is_valid_world_action_page_transition(
      static_cast<WorldActionPage>(255), WorldActionPage::travel));
  CHECK(!is_valid_world_action_page_transition(
      WorldActionPage::travel, static_cast<WorldActionPage>(255)));
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
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = usable,
      .world_action_page = static_cast<WorldActionPage>(255),
      .navigation_available = true,
  }).empty());

  // Malformed fields fail the whole request closed even when their owning
  // page is hidden, so page selection cannot expose a partially wired action.
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = usable,
      .world_action_page = WorldActionPage::travel,
      .navigation_available = true,
      .spellbook_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_map,
      .action_panel = usable,
      .world_action_page = WorldActionPage::party,
      .navigation_available = true,
      .save_available = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = usable,
      .world_action_page = WorldActionPage::game,
      .guard_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = usable,
      .guard_combatant = CombatantId{2},
      .search_control_visible = true,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = usable,
      .guard_combatant = CombatantId{2},
      .contextual_overview_control_visible = true,
      .contextual_overview_mode = ContextualOverviewMode::area_search,
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = usable,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .torch_control_visible = true,
      .torch_source = TorchSource{.member = 6, .slot = 0},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::dungeon,
      .world_presentation = WorldPresentation::dungeon_map,
      .action_panel = usable,
      .world_action_page = WorldActionPage::game,
      .navigation_available = true,
      .torch_control_visible = true,
      .torch_source = TorchSource{.member = 5, .slot = 30},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = usable,
      .world_action_page = WorldActionPage::travel,
      .navigation_available = true,
      .torch_source = TorchSource{.member = 0, .slot = 0},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = usable,
      .world_action_page = WorldActionPage::travel,
      .navigation_available = true,
      .inventory_member = PartyMemberId{1},
      .contextual_overview_control_visible = true,
      .contextual_overview_mode = ContextualOverviewMode::make_scroll,
      .contextual_overview_member = PartyMemberId{2},
  }).empty());
}

constexpr std::array kCombatPages{
    CombatActionPage::primary,
    CombatActionPage::secondary,
    CombatActionPage::utility,
    CombatActionPage::special,
};

enum class CombatCommand {
  guard,
  finish,
  delay,
  center_active,
  switch_weapon,
  center_previous,
  center_next,
  combat_items,
  auto_combatant,
  show_range,
  bandage,
  undo,
  cast,
  target,
  escape,
  scroll,
  center_cursor,
};

struct CombatCommandExpectation {
  CombatCommand command;
  CombatActionPage page;
  uint32_t region;
  ShellControlKind kind;
  std::string_view label;
  std::string_view accessibility_label;
  std::string_view focus_identifier;
  int32_t tab_order;
  UIActionPayload payload;
};

const std::array<CombatCommandExpectation, 17> kCombatCommands{{
    {CombatCommand::guard, CombatActionPage::primary, 1104U,
        ShellControlKind::guard_combatant, "GUARD", "Guard active combatant",
        "focus.action.combat.guard", 1104, GuardCombatantAction{2}},
    {CombatCommand::finish, CombatActionPage::primary, 1105U,
        ShellControlKind::finish_combatant, "FINISH",
        "Finish active combatant's turn", "focus.action.combat.finish", 1105,
        FinishCombatantAction{2}},
    {CombatCommand::delay, CombatActionPage::primary, 1106U,
        ShellControlKind::delay_combatant, "DELAY",
        "Delay active combatant's turn", "focus.action.combat.delay", 1106,
        DelayCombatantAction{2}},
    {CombatCommand::center_active, CombatActionPage::primary, 1107U,
        ShellControlKind::center_active_combatant, "CENTER",
        "Center view on active combatant", "focus.action.combat.center", 1107,
        CenterActiveCombatantAction{2}},
    {CombatCommand::switch_weapon, CombatActionPage::secondary, 1109U,
        ShellControlKind::switch_weapon_set, "WEAPON",
        "Switch active combatant's weapon set", "focus.action.combat.weapon",
        1109, SwitchWeaponSetAction{2}},
    {CombatCommand::center_previous, CombatActionPage::secondary, 1110U,
        ShellControlKind::cycle_combat_focus, "PREV",
        "Center view on previous combatant",
        "focus.action.combat.center.previous", 1110,
        CycleCombatFocusAction{2, CombatFocusDirection::previous}},
    {CombatCommand::center_next, CombatActionPage::secondary, 1111U,
        ShellControlKind::cycle_combat_focus, "NEXT",
        "Center view on next combatant", "focus.action.combat.center.next",
        1111, CycleCombatFocusAction{2, CombatFocusDirection::next}},
    {CombatCommand::combat_items, CombatActionPage::secondary, 1112U,
        ShellControlKind::open_combat_items, "ITEMS", "Open combat items",
        "focus.action.combat.items", 1112, OpenCombatItemsAction{2, 4}},
    {CombatCommand::auto_combatant, CombatActionPage::utility, 1114U,
        ShellControlKind::auto_combatant, "AUTO",
        "Auto-play active combatant's turn", "focus.action.combat.auto", 1114,
        AutoCombatantAction{2}},
    {CombatCommand::show_range, CombatActionPage::utility, 1115U,
        ShellControlKind::show_combat_range, "RANGE",
        "Show combat ranges; press any key to close",
        "focus.action.combat.range", 1115, ShowCombatRangeAction{2}},
    {CombatCommand::bandage, CombatActionPage::utility, 1116U,
        ShellControlKind::bandage_combatant, "BANDAGE",
        "Choose a party member to bandage", "focus.action.combat.bandage",
        1116, BandageCombatantAction{2}},
    {CombatCommand::undo, CombatActionPage::utility, 1117U,
        ShellControlKind::undo_combatant, "UNDO",
        "Undo active combatant's movement", "focus.action.combat.undo", 1117,
        UndoCombatantAction{2}},
    {CombatCommand::cast, CombatActionPage::special, 1119U,
        ShellControlKind::open_combat_spellbook, "CAST",
        "Open combat spell chooser", "focus.action.combat.spellbook.open",
        1119, OpenCombatSpellbookAction{2}},
    {CombatCommand::target, CombatActionPage::special, 1120U,
        ShellControlKind::open_combat_targeting, "TARGET",
        "Begin combat targeting", "focus.action.combat.targeting.open", 1120,
        OpenCombatTargetingAction{2}},
    {CombatCommand::escape, CombatActionPage::special, 1121U,
        ShellControlKind::escape_combat, "ESCAPE", "Attempt to escape combat",
        "focus.action.combat.escape", 1121, EscapeCombatAction{2}},
    {CombatCommand::scroll, CombatActionPage::special, 1122U,
        ShellControlKind::open_combat_scroll_case, "SCROLL",
        "Open combat scroll chooser", "focus.action.combat.scroll_case.open",
        1122, OpenCombatScrollCaseAction{2}},
    {CombatCommand::center_cursor, CombatActionPage::special, 1123U,
        ShellControlKind::center_combat_cursor, "CURSOR",
        "Center combat view on cursor", "focus.action.combat.center.cursor",
        1123, CenterCombatCursorAction{2, {42, 17}}},
}};

void populate_combat_command(
    ShellControlLayoutRequest& request,
    CombatCommand command,
    std::optional<CombatantId> combatant,
    bool available) {
  switch (command) {
    case CombatCommand::guard:
      request.guard_combatant = combatant;
      request.guard_available = available;
      return;
    case CombatCommand::finish:
      request.finish_combatant = combatant;
      request.finish_available = available;
      return;
    case CombatCommand::delay:
      request.delay_combatant = combatant;
      request.delay_available = available;
      return;
    case CombatCommand::center_active:
      request.center_active_combatant = combatant;
      request.center_active_available = available;
      return;
    case CombatCommand::switch_weapon:
      request.switch_weapon_combatant = combatant;
      request.switch_weapon_available = available;
      return;
    case CombatCommand::center_previous:
      request.center_previous_combatant = combatant;
      request.center_previous_available = available;
      return;
    case CombatCommand::center_next:
      request.center_next_combatant = combatant;
      request.center_next_available = available;
      return;
    case CombatCommand::combat_items:
      request.combat_items = combatant
          ? std::optional<OpenCombatItemsAction>{
                OpenCombatItemsAction{*combatant, 4}}
          : std::nullopt;
      request.combat_items_available = available;
      return;
    case CombatCommand::auto_combatant:
      request.auto_combatant = combatant;
      request.auto_combatant_available = available;
      return;
    case CombatCommand::show_range:
      request.show_combat_range_combatant = combatant;
      request.show_combat_range_available = available;
      return;
    case CombatCommand::bandage:
      request.bandage_combatant = combatant;
      request.bandage_combatant_available = available;
      return;
    case CombatCommand::undo:
      request.undo_combatant = combatant;
      request.undo_combatant_available = available;
      return;
    case CombatCommand::cast:
      request.open_combat_spellbook = combatant;
      request.open_combat_spellbook_available = available;
      return;
    case CombatCommand::target:
      request.open_combat_targeting = combatant;
      request.open_combat_targeting_available = available;
      return;
    case CombatCommand::escape:
      request.escape_combat = combatant;
      request.escape_combat_available = available;
      return;
    case CombatCommand::scroll:
      request.open_combat_scroll_case = combatant;
      request.open_combat_scroll_case_available = available;
      return;
    case CombatCommand::center_cursor:
      request.center_combat_cursor = combatant
          ? std::optional<CenterCombatCursorAction>{
                CenterCombatCursorAction{*combatant, {42, 17}}}
          : std::nullopt;
      request.center_combat_cursor_available = available;
      return;
  }
}

ShellControlLayoutRequest single_combat_command_request(
    LogicalRect panel,
    const CombatCommandExpectation& expectation,
    std::optional<CombatantId> combatant = CombatantId{2},
    bool available = true) {
  ShellControlLayoutRequest request{
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .action_panel = panel,
      .combat_action_page = expectation.page,
  };
  populate_combat_command(
      request, expectation.command, combatant, available);
  return request;
}

ShellControlLayoutRequest populated_combat_request(
    LogicalRect panel,
    CombatActionPage page,
    bool available = true) {
  return {
      .screen = ScreenContext::combat,
      .world_presentation = WorldPresentation::none,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .guard_available = available,
      .finish_combatant = CombatantId{2},
      .finish_available = available,
      .delay_combatant = CombatantId{2},
      .delay_available = available,
      .center_active_combatant = CombatantId{2},
      .center_active_available = available,
      .combat_action_page = page,
      .switch_weapon_combatant = CombatantId{2},
      .switch_weapon_available = available,
      .center_previous_combatant = CombatantId{2},
      .center_previous_available = available,
      .center_next_combatant = CombatantId{2},
      .center_next_available = available,
      .combat_items = OpenCombatItemsAction{2, 4},
      .combat_items_available = available,
      .auto_combatant = CombatantId{2},
      .auto_combatant_available = available,
      .show_combat_range_combatant = CombatantId{2},
      .show_combat_range_available = available,
      .bandage_combatant = CombatantId{2},
      .bandage_combatant_available = available,
      .undo_combatant = CombatantId{2},
      .undo_combatant_available = available,
      .open_combat_spellbook = CombatantId{2},
      .open_combat_spellbook_available = available,
      .open_combat_targeting = CombatantId{2},
      .open_combat_targeting_available = available,
      .escape_combat = CombatantId{2},
      .escape_combat_available = available,
      .open_combat_scroll_case = CombatantId{2},
      .open_combat_scroll_case_available = available,
      .center_combat_cursor = CenterCombatCursorAction{2, {42, 17}},
      .center_combat_cursor_available = available,
  };
}

void verify_combat_deck(
    const std::vector<ShellControlPlacement>& controls,
    LogicalRect panel,
    CombatActionPage active_page,
    size_t action_count) {
  constexpr std::array<const char*, 4> labels{
      "TURN", "GEAR", "TACTICS", "SPECIAL"};
  constexpr std::array<const char*, 4> accessibility_labels{
      "Turn combat commands tab",
      "Gear and view combat commands tab",
      "Tactical combat commands tab",
      "Special combat commands tab",
  };
  constexpr std::array<const char*, 4> focus_identifiers{
      "focus.action.combat.page.turn",
      "focus.action.combat.page.gear",
      "focus.action.combat.page.tactics",
      "focus.action.combat.page.special",
  };

  CHECK(controls.size() == 4U + action_count);
  std::set<uint32_t> regions;
  std::set<std::string> focus_ids;
  size_t selected_count = 0U;
  int32_t previous_tab_order = -1;
  for (size_t index = 0; index < controls.size(); ++index) {
    const auto& control = controls[index];
    CHECK(control.region.is_valid());
    CHECK(regions.emplace(control.region.value).second);
    CHECK(focus_ids.emplace(control.focus_identifier).second);
    CHECK(panel.contains(control.bounds));
    CHECK(control.bounds.width >= 44.0);
    CHECK(control.bounds.height >= 44.0);
    CHECK(control.bounds.width <=
        (control.kind == ShellControlKind::combat_action_page ? 112.0 : 160.0));
    CHECK(control.tab_order > previous_tab_order);
    previous_tab_order = control.tab_order;
    for (size_t prior = 0; prior < index; ++prior) {
      CHECK(!interiors_overlap(control.bounds, controls[prior].bounds));
    }
  }

  for (size_t index = 0; index < kCombatPages.size(); ++index) {
    const auto& tab = controls[index];
    CHECK(tab.region.value == 1200U + index);
    CHECK(tab.kind == ShellControlKind::combat_action_page);
    CHECK(tab.label == labels[index]);
    const std::string expected_accessibility_label =
        std::string(accessibility_labels[index]) +
        (active_page == kCombatPages[index] ? ", selected" : "");
    CHECK(tab.accessibility_label == expected_accessibility_label);
    CHECK(tab.focus_identifier == focus_identifiers[index]);
    CHECK(tab.tab_order == 1100 + static_cast<int32_t>(index));
    CHECK(tab.enabled);
    CHECK(tab.selected == (active_page == kCombatPages[index]));
    selected_count += tab.selected ? 1U : 0U;
    CHECK(std::holds_alternative<SetCombatActionPageAction>(tab.payload));
    CHECK(std::get<SetCombatActionPageAction>(tab.payload).page ==
        kCombatPages[index]);
    CHECK(is_valid_combat_action_page_transition(
        active_page,
        std::get<SetCombatActionPageAction>(tab.payload).page));
  }
  CHECK(selected_count == 1U);

  for (size_t index = kCombatPages.size(); index < controls.size(); ++index) {
    CHECK(!controls[index].selected);
  }
}

void verify_combat_command(
    const ShellControlPlacement& control,
    const CombatCommandExpectation& expected,
    bool enabled) {
  CHECK(control.region.value == expected.region);
  CHECK(control.kind == expected.kind);
  CHECK(control.label == expected.label);
  CHECK(control.accessibility_label == expected.accessibility_label);
  CHECK(control.focus_identifier == expected.focus_identifier);
  CHECK(control.tab_order == expected.tab_order);
  CHECK(control.enabled == enabled);
  CHECK(!control.selected);
  CHECK(control.payload == expected.payload);
  CHECK(control.bounds.width >= 44.0);
  CHECK(control.bounds.width <= 160.0);
  CHECK(control.bounds.height >= 44.0);
}

void test_combat_command_contracts_are_independent_and_fail_closed() {
  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};

  for (const auto& expected : kCombatCommands) {
    const auto enabled_request =
        single_combat_command_request(panel, expected, CombatantId{2}, true);
    const auto enabled = compute_shell_control_layout(enabled_request);
    verify_combat_deck(enabled, panel, expected.page, 1U);
    verify_combat_command(enabled.back(), expected, true);

    const auto disabled = compute_shell_control_layout(
        single_combat_command_request(
            panel, expected, CombatantId{2}, false));
    verify_combat_deck(disabled, panel, expected.page, 1U);
    verify_combat_command(disabled.back(), expected, false);

    // Every availability bit must be paired with its own typed payload. This
    // catches both missing descriptors and accidental cross-wiring to a peer's
    // availability flag.
    CHECK(compute_shell_control_layout(single_combat_command_request(
        panel, expected, std::nullopt, true)).empty());

    for (const CombatantId invalid : {CombatantId{-1}, CombatantId{256}}) {
      CHECK(compute_shell_control_layout(single_combat_command_request(
          panel, expected, invalid, true)).empty());
    }

    // Every actor-bearing payload must participate in the common-actor gate,
    // including commands on pages other than Turn.
    auto mismatched = enabled_request;
    if (expected.command == CombatCommand::guard) {
      populate_combat_command(
          mismatched, CombatCommand::finish, CombatantId{3}, true);
    } else {
      populate_combat_command(
          mismatched, CombatCommand::guard, CombatantId{3}, true);
    }
    CHECK(compute_shell_control_layout(mismatched).empty());
  }

  for (const auto cell : {
           CombatFieldCell{90, 0},
           CombatFieldCell{0, 90},
       }) {
    auto invalid_cursor = single_combat_command_request(
        panel, kCombatCommands.back());
    invalid_cursor.center_combat_cursor = CenterCombatCursorAction{2, cell};
    CHECK(compute_shell_control_layout(invalid_cursor).empty());
  }

  // Every tab is a live direct-selection destination. Exercise a request whose
  // target pages have unequal action counts at the exact panel size needed by
  // the largest page, then prove every advertised transition recomposes.
  const LogicalRect minimum_navigable_deck{0.0, 0.0, 272.0, 120.0};
  constexpr std::array<size_t, 4> action_counts{4U, 4U, 4U, 5U};
  for (size_t origin_index = 0;
       origin_index < kCombatPages.size(); ++origin_index) {
    const auto origin_request = populated_combat_request(
        minimum_navigable_deck, kCombatPages[origin_index]);
    const auto origin = compute_shell_control_layout(origin_request);
    verify_combat_deck(
        origin,
        minimum_navigable_deck,
        kCombatPages[origin_index],
        action_counts[origin_index]);
    for (size_t tab_index = 0; tab_index < kCombatPages.size(); ++tab_index) {
      const auto target_page = std::get<SetCombatActionPageAction>(
          origin[tab_index].payload).page;
      auto target_request = origin_request;
      target_request.combat_action_page = target_page;
      const auto target = compute_shell_control_layout(target_request);
      verify_combat_deck(
          target,
          minimum_navigable_deck,
          target_page,
          action_counts[tab_index]);
      for (size_t target_tab = 0;
           target_tab < kCombatPages.size(); ++target_tab) {
        CHECK(target[target_tab].bounds == origin[target_tab].bounds);
        CHECK(target[target_tab].enabled);
      }
    }
  }
  for (const auto page : kCombatPages) {
    CHECK(compute_shell_control_layout(populated_combat_request(
        {0.0, 0.0, 271.0, 120.0}, page)).empty());
    CHECK(compute_shell_control_layout(populated_combat_request(
        {0.0, 0.0, 272.0, 119.0}, page)).empty());
  }
}

void test_persistent_named_combat_command_deck() {
  const LogicalRect panel{16.0, 600.0, 900.0, 150.0};
  constexpr std::array<size_t, 4> action_counts{4U, 4U, 4U, 5U};
  std::array<std::vector<ShellControlPlacement>, 4> layouts;

  for (size_t page_index = 0; page_index < kCombatPages.size(); ++page_index) {
    layouts[page_index] = compute_shell_control_layout(
        populated_combat_request(panel, kCombatPages[page_index]));
    verify_combat_deck(
        layouts[page_index],
        panel,
        kCombatPages[page_index],
        action_counts[page_index]);

    for (size_t tab_index = 0; tab_index < kCombatPages.size(); ++tab_index) {
      const auto& reference = layouts[0][tab_index];
      const auto& tab = layouts[page_index][tab_index];
      CHECK(tab.region == reference.region);
      CHECK(tab.bounds == reference.bounds);
      CHECK(tab.label == reference.label);
      CHECK(tab.focus_identifier == reference.focus_identifier);
      CHECK(tab.tab_order == reference.tab_order);
      CHECK(tab.enabled == reference.enabled);
      CHECK(tab.payload == reference.payload);
    }
  }

  size_t expected_index = 0U;
  for (const auto& layout : layouts) {
    for (size_t control_index = kCombatPages.size();
         control_index < layout.size();
         ++control_index) {
      const auto& control = layout[control_index];
      CHECK(expected_index < kCombatCommands.size());
      verify_combat_command(
          control, kCombatCommands[expected_index], true);
      ++expected_index;
    }
  }
  CHECK(expected_index == kCombatCommands.size());

  for (const auto page : kCombatPages) {
    const auto disabled =
        compute_shell_control_layout(populated_combat_request(panel, page, false));
    const auto page_index = static_cast<size_t>(page);
    verify_combat_deck(disabled, panel, page, action_counts[page_index]);
    for (size_t index = kCombatPages.size(); index < disabled.size(); ++index) {
      CHECK(!disabled[index].enabled);
    }
  }

  for (const auto page : kCombatPages) {
    const auto sparse = compute_shell_control_layout({
        .screen = ScreenContext::combat,
        .action_panel = panel,
        .guard_combatant = CombatantId{2},
        .guard_available = true,
        .combat_action_page = page,
    });
    const size_t visible_actions =
        page == CombatActionPage::primary ? 1U : 0U;
    verify_combat_deck(sparse, panel, page, visible_actions);
    if (visible_actions == 1U) {
      CHECK(sparse[4].payload == UIActionPayload{GuardCombatantAction{2}});
    }
  }

  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1359.0, 900.0},
      LogicalSize{1360.0, 768.0},
      LogicalSize{1440.0, 900.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto size : sizes) {
    const auto canonical_panel = action_panel_for(size);
    for (size_t page_index = 0; page_index < kCombatPages.size(); ++page_index) {
      const auto controls = compute_shell_control_layout(
          populated_combat_request(canonical_panel, kCombatPages[page_index]));
      verify_combat_deck(
          controls,
          canonical_panel,
          kCombatPages[page_index],
          action_counts[page_index]);
    }
  }

  const LogicalRect exact_special_minimum{0.0, 0.0, 272.0, 120.0};
  const auto exact_special = compute_shell_control_layout(
      populated_combat_request(
          exact_special_minimum, CombatActionPage::special));
  verify_combat_deck(
      exact_special,
      exact_special_minimum,
      CombatActionPage::special,
      5U);
  for (size_t index = kCombatPages.size();
       index < exact_special.size();
       ++index) {
    CHECK(exact_special[index].bounds.width == 44.0);
    CHECK(exact_special[index].bounds.height == 44.0);
  }
  CHECK(compute_shell_control_layout(populated_combat_request(
      {0.0, 0.0, 271.0, 120.0},
      CombatActionPage::special)).empty());
  CHECK(compute_shell_control_layout(populated_combat_request(
      {0.0, 0.0, 272.0, 119.0},
      CombatActionPage::special)).empty());

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
      .guard_combatant = CombatantId{2},
      .finish_combatant = CombatantId{3},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .inventory_member = PartyMemberId{4},
      .guard_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::exploration,
      .world_presentation = WorldPresentation::outdoor,
      .action_panel = panel,
      .navigation_available = true,
      .guard_combatant = CombatantId{2},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .center_combat_cursor = CenterCombatCursorAction{2, {90, 0}},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .center_combat_cursor = CenterCombatCursorAction{2, {0, 90}},
  }).empty());
  CHECK(compute_shell_control_layout({
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .guard_combatant = CombatantId{2},
      .combat_action_page = static_cast<CombatActionPage>(255),
  }).empty());
}

} // namespace

int main() {
  try {
    test_canonical_sizes();
    test_payload_order_and_disabled_state();
    test_open_inventory_control();
    test_open_scroll_case_control_is_distinct_and_visible_when_disabled();
    test_open_character_sheet_control_is_the_fourth_party_action();
    test_world_action_controls_at_combined_minimum_layout();
    test_combat_command_contracts_are_independent_and_fail_closed();
    test_persistent_named_combat_command_deck();
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
