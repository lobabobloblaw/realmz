#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "presentation/GameSnapshot.hpp"
#include "presentation/LegacyCommandBridge.hpp"
#include "presentation/PresentationHost.hpp"
#include "presentation/PresentationMode.hpp"
#include "presentation/ResponsiveLayout.hpp"
#include "presentation/UIAction.hpp"
#include "presentation/UIPrimitives.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  checks_run++;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

bool approximately_equal(double a, double b, double tolerance = 1e-8) {
  return std::abs(a - b) <= tolerance *
      std::max({1.0, std::abs(a), std::abs(b)});
}

void test_presentation_mode() {
  CHECK(to_string(PresentationMode::classic) == "classic");
  CHECK(to_string(PresentationMode::remastered) == "remastered");
  CHECK(presentation_mode_from_string("classic") == PresentationMode::classic);
  CHECK(presentation_mode_from_string("remastered") == PresentationMode::remastered);
  CHECK(!presentation_mode_from_string("modern"));
}

class RecordingClassicFrameTarget final : public ClassicFrameTarget {
public:
  int classic_submissions = 0;
  int remastered_submissions = 0;
  int untouched_engine_value = 73;

  void present_classic_frame() override {
    this->classic_submissions++;
  }

  void present_remastered_frame() override {
    this->remastered_submissions++;
  }
};

void test_presentation_host_routing() {
  RecordingClassicFrameTarget target;
  PresentationHost host;

  CHECK(host.mode() == PresentationMode::classic);
  CHECK(host.active_renderer().mode() == PresentationMode::classic);
  CHECK(host.present(target) == PresentationRenderPath::classic);
  CHECK(target.classic_submissions == 1);
  CHECK(target.remastered_submissions == 0);
  CHECK(target.untouched_engine_value == 73);

  host.set_mode(PresentationMode::remastered);
  CHECK(host.mode() == PresentationMode::remastered);
  CHECK(host.active_renderer().mode() == PresentationMode::remastered);
  CHECK(host.present(target) ==
      PresentationRenderPath::remastered_shell);
  CHECK(target.classic_submissions == 1);
  CHECK(target.remastered_submissions == 1);
  CHECK(target.untouched_engine_value == 73);

  host.set_mode(PresentationMode::classic);
  CHECK(host.present(target) == PresentationRenderPath::classic);
  CHECK(target.classic_submissions == 2);
  CHECK(target.remastered_submissions == 1);

  bool rejected_invalid_mode = false;
  try {
    host.set_mode(static_cast<PresentationMode>(255));
  } catch (const std::invalid_argument&) {
    rejected_invalid_mode = true;
  }
  CHECK(rejected_invalid_mode);
  CHECK(host.mode() == PresentationMode::classic);
  CHECK(target.classic_submissions == 2);
  CHECK(target.remastered_submissions == 1);
}

void test_snapshot_values() {
  GameSnapshot snapshot;
  snapshot.revision = 42;
  snapshot.screen = ScreenContext::exploration;
  snapshot.scenario_id = 1;
  snapshot.party.members.emplace_back(PartyMemberView{
      .id = 2,
      .name = "Myr",
      .level = 7,
      .stamina = {19, 24},
      .spell_points = {8, 12},
      .selected = true,
  });
  snapshot.party.selected_member = 2;
  CHECK(snapshot.party.member(2) != nullptr);
  CHECK(snapshot.party.member(2)->stamina.is_valid());
  CHECK((MeterView{-3, 24}.is_valid()));
  CHECK(snapshot.party.member(3) == nullptr);

  snapshot.world.visible_columns = 2;
  snapshot.world.visible_rows = 2;
  snapshot.world.visible_tiles = {
      WorldTileView{.terrain_id = 10},
      WorldTileView{.terrain_id = 11},
      WorldTileView{.terrain_id = 12},
      WorldTileView{.terrain_id = 13},
  };
  CHECK(snapshot.world.has_complete_tile_grid());
  CHECK(snapshot.world.tile_at(1, 1)->terrain_id == 13);
  CHECK(snapshot.world.tile_at(2, 1) == nullptr);

  const GameSnapshot retained = snapshot;
  snapshot.party.members[0].name = "Changed in a later capture";
  snapshot.world.visible_tiles.pop_back();
  CHECK(retained.party.members[0].name == "Myr");
  CHECK(retained.world.has_complete_tile_grid());
  CHECK(!snapshot.world.has_complete_tile_grid());
}

void test_actions_and_events() {
  UIAction movement{
      .sequence = 8,
      .payload = MovePartyAction{MovementCommand::turn_left},
  };
  CHECK(action_name(movement.payload) == "move_party");
  CHECK(std::get<MovePartyAction>(movement.payload).command ==
      MovementCommand::turn_left);

  UIAction open_inventory{
      .sequence = 9,
      .payload = OpenInventoryAction{2},
  };
  CHECK(action_name(open_inventory.payload) == "open_inventory");
  CHECK(std::get<OpenInventoryAction>(open_inventory.payload).member == 2);

  UIAction open_spellbook{
      .sequence = 10,
      .payload = OpenSpellbookAction{2},
  };
  CHECK(action_name(open_spellbook.payload) == "open_spellbook");
  CHECK(std::get<OpenSpellbookAction>(open_spellbook.payload).member == 2);

  UIAction open_save_game{
      .sequence = 11,
      .payload = OpenSaveGameAction{},
  };
  CHECK(action_name(open_save_game.payload) == "open_save_game");
  CHECK(std::holds_alternative<OpenSaveGameAction>(
      open_save_game.payload));

  UIAction open_load_game{
      .sequence = 12,
      .payload = OpenLoadGameAction{},
  };
  CHECK(action_name(open_load_game.payload) == "open_load_game");
  CHECK(std::holds_alternative<OpenLoadGameAction>(
      open_load_game.payload));

  UIAction guard{
      .sequence = 13,
      .payload = GuardCombatantAction{2},
  };
  CHECK(action_name(guard.payload) == "guard_combatant");
  CHECK(std::get<GuardCombatantAction>(guard.payload).combatant == 2);

  UIAction casting{
      .sequence = 14,
      .payload = CastSpellAction{
          .caster = 1,
          .spell_id = 72,
          .target = ActionTarget::map_cell(4, 5),
      },
  };
  CHECK(action_name(casting.payload) == "cast_spell");
  const auto& cast = std::get<CastSpellAction>(casting.payload);
  CHECK(cast.target.kind == TargetKind::map_cell);
  CHECK(cast.target.primary == 4);
  CHECK(cast.target.secondary == 5);

  UIAction drawer{
      .sequence = 13,
      .payload = SetDrawerPanelAction{DrawerPanel::event_log},
  };
  CHECK(action_name(drawer.payload) == "set_drawer_panel");
  CHECK(std::get<SetDrawerPanelAction>(drawer.payload).panel ==
      DrawerPanel::event_log);

  GameEvent event{
      .sequence = 3,
      .payload = MessageEvent{MessageSeverity::success, "Saved"},
  };
  CHECK(std::get<MessageEvent>(event.payload).text == "Saved");
}

void test_command_bridge() {
  MovementCommand received = MovementCommand::step_backward;
  LegacyActionHandlers handlers;
  handlers.move_party = [&received](const MovePartyAction& action) {
    received = action.command;
    return DispatchResult::handled({GameEvent{
        .sequence = 1,
        .payload = AudioCueEvent{"footstep", AudioBus::effect, false},
    }});
  };
  handlers.cast_spell = [](const CastSpellAction&) -> DispatchResult {
    throw std::runtime_error("legacy failure");
  };

  InjectedLegacyCommandBridge* bridge_address = nullptr;
  handlers.confirm = [&bridge_address](const ConfirmAction&) {
    CHECK(bridge_address != nullptr);
    const auto nested = bridge_address->dispatch(UIAction{
        .sequence = 99,
        .payload = CancelAction{},
    });
    CHECK(nested.status == DispatchStatus::rejected);
    return DispatchResult::handled();
  };

  InjectedLegacyCommandBridge bridge(std::move(handlers));
  bridge_address = &bridge;

  const auto move_result = bridge.dispatch(UIAction{
      .sequence = 1,
      .payload = MovePartyAction{MovementCommand::north},
  });
  CHECK(move_result.was_handled());
  CHECK(received == MovementCommand::north);
  CHECK(move_result.events.size() == 1);

  const auto unsupported = bridge.dispatch(UIAction{
      .sequence = 2,
      .payload = SaveGameAction{"A"},
  });
  CHECK(unsupported.status == DispatchStatus::unsupported);
  CHECK(unsupported.detail.find("save_game") != std::string::npos);

  const auto local_only = bridge.dispatch(UIAction{
      .sequence = 5,
      .payload = SetDrawerPanelAction{DrawerPanel::details},
  });
  CHECK(local_only.status == DispatchStatus::unsupported);
  CHECK(local_only.detail.find("set_drawer_panel") != std::string::npos);

  const auto inventory_unsupported = bridge.dispatch(UIAction{
      .sequence = 6,
      .payload = OpenInventoryAction{1},
  });
  CHECK(inventory_unsupported.status == DispatchStatus::unsupported);
  CHECK(inventory_unsupported.detail.find("open_inventory") !=
      std::string::npos);

  const auto spellbook_unsupported = bridge.dispatch(UIAction{
      .sequence = 7,
      .payload = OpenSpellbookAction{1},
  });
  CHECK(spellbook_unsupported.status == DispatchStatus::unsupported);
  CHECK(spellbook_unsupported.detail.find("open_spellbook") !=
      std::string::npos);

  const auto save_chooser_unsupported = bridge.dispatch(UIAction{
      .sequence = 8,
      .payload = OpenSaveGameAction{},
  });
  CHECK(save_chooser_unsupported.status == DispatchStatus::unsupported);
  CHECK(save_chooser_unsupported.detail.find("open_save_game") !=
      std::string::npos);

  const auto load_chooser_unsupported = bridge.dispatch(UIAction{
      .sequence = 9,
      .payload = OpenLoadGameAction{},
  });
  CHECK(load_chooser_unsupported.status == DispatchStatus::unsupported);
  CHECK(load_chooser_unsupported.detail.find("open_load_game") !=
      std::string::npos);

  const auto guard_unsupported = bridge.dispatch(UIAction{
      .sequence = 10,
      .payload = GuardCombatantAction{1},
  });
  CHECK(guard_unsupported.status == DispatchStatus::unsupported);
  CHECK(guard_unsupported.detail.find("guard_combatant") !=
      std::string::npos);

  const auto failed = bridge.dispatch(UIAction{
      .sequence = 3,
      .payload = CastSpellAction{},
  });
  CHECK(failed.status == DispatchStatus::failed);
  CHECK(failed.detail.find("legacy failure") != std::string::npos);

  const auto confirmed = bridge.dispatch(UIAction{
      .sequence = 4,
      .payload = ConfirmAction{},
  });
  CHECK(confirmed.was_handled());
}

void verify_layout(
    LogicalSize window_size,
    LayoutClass expected_class,
    double backing_scale) {
  constexpr LogicalSize gameplay_content{480.0, 416.0};
  constexpr TileCoverage tile_coverage{15, 13};
  const auto layout = compute_responsive_layout({
      .window_size = window_size,
      .gameplay_content_size = gameplay_content,
      .visible_tiles = tile_coverage,
      .backing_scale = backing_scale,
  });

  CHECK(layout.layout_class == expected_class);
  CHECK(layout.visible_tiles == tile_coverage);
  CHECK(layout.inner_bounds.contains(layout.gameplay_slot));
  CHECK(layout.gameplay_slot.contains(layout.gameplay_viewport));
  CHECK(layout.inner_bounds.contains(layout.party_rail));
  CHECK(layout.inner_bounds.contains(layout.action_bar));
  CHECK(approximately_equal(
      layout.gameplay_viewport.width / layout.gameplay_viewport.height,
      gameplay_content.width / gameplay_content.height));

  if (expected_class == LayoutClass::wide) {
    CHECK(layout.details_panel.has_value());
    CHECK(layout.event_log.has_value());
    CHECK(!layout.drawer_tabs.has_value());
    CHECK(layout.inner_bounds.contains(*layout.details_panel));
    CHECK(layout.inner_bounds.contains(*layout.event_log));
  } else {
    CHECK(!layout.details_panel.has_value());
    CHECK(!layout.event_log.has_value());
    CHECK(layout.drawer_tabs.has_value());
    CHECK(layout.inner_bounds.contains(*layout.drawer_tabs));
  }

  const auto content_transform = layout.gameplay_transform();
  CHECK(approximately_equal(content_transform.scale(),
      layout.gameplay_viewport.width / gameplay_content.width));
  const auto content_origin = content_transform.content_to_window(
      LogicalPoint{0.0, 0.0});
  CHECK(approximately_equal(content_origin.x, layout.gameplay_viewport.x));
  CHECK(approximately_equal(content_origin.y, layout.gameplay_viewport.y));
  const auto content_end = content_transform.content_to_window(LogicalPoint{
      gameplay_content.width,
      gameplay_content.height,
  });
  CHECK(approximately_equal(content_end.x, layout.gameplay_viewport.right()));
  CHECK(approximately_equal(content_end.y, layout.gameplay_viewport.bottom()));

  const LogicalPoint content_sample{120.0, 104.0};
  const auto window_sample = content_transform.content_to_window(content_sample);
  const auto round_trip = content_transform.window_to_content(window_sample);
  CHECK(round_trip.has_value());
  CHECK(approximately_equal(round_trip->x, content_sample.x));
  CHECK(approximately_equal(round_trip->y, content_sample.y));

  const auto physical = layout.backing_transform().to_physical(
      layout.gameplay_viewport);
  CHECK(approximately_equal(
      physical.width,
      layout.gameplay_viewport.width * backing_scale,
      1e-2));
  CHECK(approximately_equal(
      physical.height,
      layout.gameplay_viewport.height * backing_scale,
      1e-2));
}

void test_layout_and_transforms() {
  verify_layout({1024.0, 768.0}, LayoutClass::compact, 1.0);
  verify_layout({1359.0, 900.0}, LayoutClass::compact, 2.0);
  verify_layout({1360.0, 768.0}, LayoutClass::wide, 1.0);
  verify_layout({1440.0, 900.0}, LayoutClass::wide, 2.0);
  verify_layout({1920.0, 1080.0}, LayoutClass::wide, 1.0);
  verify_layout({3440.0, 1440.0}, LayoutClass::wide, 2.0);

  const BackingTransform retina(2.0);
  CHECK(retina.to_physical(LogicalPoint{10.5, 20.25}) ==
      (PhysicalPoint{21, 41}));
  CHECK(retina.to_physical(LogicalRect{1.25, 2.25, 10.5, 20.5}) ==
      (PhysicalRect{3, 5, 21, 41}));
  const auto logical = retina.to_logical({21, 41});
  CHECK(approximately_equal(logical.x, 10.5));
  CHECK(approximately_equal(logical.y, 20.5));

  bool rejected_small_window = false;
  try {
    (void)compute_responsive_layout({
        .window_size = {1000.0, 768.0},
        .gameplay_content_size = {480.0, 416.0},
        .visible_tiles = {15, 13},
    });
  } catch (const std::invalid_argument&) {
    rejected_small_window = true;
  }
  CHECK(rejected_small_window);

  bool rejected_nonuniform_transform = false;
  try {
    (void)UniformContentTransform(
        {100.0, 100.0},
        {0.0, 0.0, 200.0, 150.0});
  } catch (const std::invalid_argument&) {
    rejected_nonuniform_transform = true;
  }
  CHECK(rejected_nonuniform_transform);
}

void test_hit_testing_and_focus() {
  const std::array regions{
      HitRegion{1, {0.0, 0.0, 100.0, 100.0}, 0, true, true},
      HitRegion{2, {20.0, 20.0, 40.0, 40.0}, 2, true, true},
      HitRegion{3, {20.0, 20.0, 40.0, 40.0}, 2, true, true},
      HitRegion{4, {20.0, 20.0, 40.0, 40.0}, 4, false, true},
  };
  CHECK(hit_test(regions, {10.0, 10.0}) == 1);
  CHECK(hit_test(regions, {30.0, 30.0}) == 3);
  CHECK(!hit_test(regions, {100.0, 100.0}));

  FocusNavigator focus;
  focus.set_entries({
      FocusEntry{1, 20, true, true},
      FocusEntry{2, 10, true, true},
      FocusEntry{3, 0, false, true},
      FocusEntry{4, 20, true, true},
  });
  CHECK(focus.move(FocusMove::next) == 2);
  CHECK(focus.move(FocusMove::next) == 1);
  CHECK(focus.move(FocusMove::next) == 4);
  CHECK(focus.move(FocusMove::next) == 2);
  CHECK(focus.move(FocusMove::previous) == 4);
  CHECK(!focus.set_focus(3));
  CHECK(focus.focused() == 4);

  focus.set_entries({
      FocusEntry{1, 20, true, true},
      FocusEntry{2, 10, true, true},
      FocusEntry{4, 20, false, true},
  });
  CHECK(!focus.focused());
  CHECK(focus.move(FocusMove::previous) == 1);

  bool rejected_duplicate = false;
  try {
    focus.set_entries({FocusEntry{7}, FocusEntry{7}});
  } catch (const std::invalid_argument&) {
    rejected_duplicate = true;
  }
  CHECK(rejected_duplicate);
}

} // namespace

int main() {
  try {
    test_presentation_mode();
    test_presentation_host_routing();
    test_snapshot_values();
    test_actions_and_events();
    test_command_bridge();
    test_layout_and_transforms();
    test_hit_testing_and_focus();
    std::cout << "PresentationCoreTest passed (" << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "PresentationCoreTest failed after " << checks_run
              << " checks: " << e.what() << '\n';
    return 1;
  }
}
