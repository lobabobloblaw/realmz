#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "presentation/DrawerControlLayout.hpp"
#include "presentation/ResponsiveLayout.hpp"
#include "presentation/ShellKeyboardInteraction.hpp"

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

[[nodiscard]] DrawerModel drawers(
    std::optional<DrawerPanel> active = std::nullopt) {
  return {
      .collapsed = true,
      .active_panel = active,
      .tabs = {
          DrawerTabModel{
              .panel = DrawerPanel::details,
              .label = "Details",
              .command = "drawer.details.toggle",
              .focus_identifier = "focus.drawer.details",
              .tab_order = 2000,
              .active = active == DrawerPanel::details,
          },
          DrawerTabModel{
              .panel = DrawerPanel::event_log,
              .label = "Event log",
              .command = "drawer.event_log.toggle",
              .focus_identifier = "focus.drawer.event_log",
              .tab_order = 2001,
              .badge_count = 3,
              .active = active == DrawerPanel::event_log,
          },
      },
  };
}

[[nodiscard]] LogicalRect compact_panel(LogicalSize window_size) {
  const auto layout = compute_responsive_layout({
      .window_size = window_size,
      .gameplay_content_size = {800.0, 600.0},
      .visible_tiles = {15, 13},
      .backing_scale = 1.0,
  });
  CHECK(layout.layout_class == LayoutClass::compact);
  CHECK(layout.drawer_tabs.has_value());
  return *layout.drawer_tabs;
}

void verify_controls(
    LogicalRect panel,
    std::optional<DrawerPanel> active,
    bool available) {
  const auto model = drawers(active);
  const auto controls = compute_drawer_control_layout(
      model, panel, available);
  CHECK(controls.size() == 2U);
  for (size_t index = 0; index < controls.size(); ++index) {
    const auto& control = controls[index];
    const auto& tab = model.tabs[index];
    CHECK(control.kind == ShellControlKind::drawer_tab);
    CHECK(control.region == drawer_panel_region_id(tab.panel));
    CHECK(control.region.value == 3000U + index);
    CHECK(panel.contains(control.bounds));
    CHECK(control.bounds.width >= 44.0);
    CHECK(control.bounds.height >= 44.0);
    CHECK(control.label == tab.label);
    CHECK(control.focus_identifier == tab.focus_identifier);
    CHECK(control.tab_order == tab.tab_order);
    CHECK(control.enabled == available);
    CHECK(std::holds_alternative<SetDrawerPanelAction>(control.payload));
    const auto& action = std::get<SetDrawerPanelAction>(control.payload);
    if (tab.active) {
      CHECK(!action.panel.has_value());
      CHECK(control.accessibility_label == "Close " + tab.label + " drawer");
    } else {
      CHECK(action.panel == tab.panel);
      CHECK(control.accessibility_label == "Open " + tab.label + " drawer");
    }
  }
  CHECK(controls[0].bounds.right() < controls[1].bounds.x);
  CHECK(controls == compute_drawer_control_layout(model, panel, available));
}

void test_canonical_compact_sizes_and_explicit_actions() {
  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1200.0, 800.0},
      LogicalSize{1359.0, 900.0},
  };
  for (const auto size : sizes) {
    const auto panel = compact_panel(size);
    verify_controls(panel, std::nullopt, true);
    verify_controls(panel, DrawerPanel::details, true);
    verify_controls(panel, DrawerPanel::event_log, true);
    verify_controls(panel, DrawerPanel::details, false);
  }
}

template <typename Mutation>
void verify_malformed_model_fails_closed(Mutation mutation) {
  auto model = drawers();
  mutation(model);
  CHECK(compute_drawer_control_layout(
      model, compact_panel({1024.0, 768.0}), true)
          .empty());
}

void test_malformed_models_fail_closed() {
  verify_malformed_model_fails_closed(
      [](auto& model) { model.collapsed = false; });
  verify_malformed_model_fails_closed(
      [](auto& model) { model.tabs.pop_back(); });
  verify_malformed_model_fails_closed(
      [](auto& model) { model.tabs[1].panel = model.tabs[0].panel; });
  verify_malformed_model_fails_closed(
      [](auto& model) {
        model.tabs[1].focus_identifier = model.tabs[0].focus_identifier;
      });
  verify_malformed_model_fails_closed(
      [](auto& model) { model.tabs[1].command = model.tabs[0].command; });
  verify_malformed_model_fails_closed(
      [](auto& model) { model.tabs[1].tab_order = model.tabs[0].tab_order; });
  verify_malformed_model_fails_closed(
      [](auto& model) { model.tabs[0].label.clear(); });
  verify_malformed_model_fails_closed(
      [](auto& model) { model.active_panel = DrawerPanel::details; });
  verify_malformed_model_fails_closed(
      [](auto& model) {
        model.active_panel = DrawerPanel::event_log;
        model.tabs[0].active = true;
      });
  verify_malformed_model_fails_closed(
      [](auto& model) {
        model.tabs[0].active = true;
        model.tabs[1].active = true;
        model.active_panel = DrawerPanel::details;
      });
}

void test_invalid_geometry_fails_closed() {
  const auto model = drawers();
  CHECK(compute_drawer_control_layout(
      model, {0.0, 0.0, 110.0, 60.0}, true)
          .empty());
  CHECK(compute_drawer_control_layout(
      model, {0.0, 0.0, 200.0, 59.0}, true)
          .empty());
  CHECK(compute_drawer_control_layout(
      model,
      {0.0, 0.0, std::numeric_limits<double>::infinity(), 120.0},
      true)
          .empty());
  CHECK(compute_drawer_control_layout(
      model, {0.0, 0.0, -200.0, 120.0}, true)
          .empty());
}

[[nodiscard]] ShellKeyboardEvent key_event(
    ShellKeyboardKey key,
    ShellKeyboardPhase phase,
    uint32_t scancode) {
  return {
      .token = {7, scancode},
      .key = key,
      .phase = phase,
  };
}

void test_keyboard_open_and_close_actions() {
  const auto panel = compact_panel({1024.0, 768.0});
  const auto closed = compute_drawer_control_layout(
      drawers(), panel, true);
  ShellKeyboardInteraction keyboard;

  auto result = keyboard.handle(
      key_event(ShellKeyboardKey::tab, ShellKeyboardPhase::down, 43),
      closed,
      true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() ==
      std::optional<std::string>{"focus.drawer.details"});
  CHECK(keyboard.handle(
                    key_event(ShellKeyboardKey::tab, ShellKeyboardPhase::up, 43),
                    closed,
                    true)
          .consumed);

  CHECK(keyboard.handle(
                    key_event(ShellKeyboardKey::enter, ShellKeyboardPhase::down, 40),
                    closed,
                    true)
          .consumed);
  result = keyboard.handle(
      key_event(ShellKeyboardKey::enter, ShellKeyboardPhase::up, 40),
      closed,
      true);
  CHECK(result.consumed);
  CHECK(result.invoked_control.has_value());
  CHECK(result.invoked_control->kind == ShellControlKind::drawer_tab);
  CHECK(std::get<SetDrawerPanelAction>(result.invoked_control->payload).panel ==
      DrawerPanel::details);

  const auto event_log_open = compute_drawer_control_layout(
      drawers(DrawerPanel::event_log), panel, true);
  keyboard.reset();
  CHECK(keyboard.focus_control(
      "focus.drawer.event_log", event_log_open, true));
  CHECK(keyboard.handle(
                    key_event(ShellKeyboardKey::space, ShellKeyboardPhase::down, 44),
                    event_log_open,
                    true)
          .consumed);
  result = keyboard.handle(
      key_event(ShellKeyboardKey::space, ShellKeyboardPhase::up, 44),
      event_log_open,
      true);
  CHECK(result.consumed);
  CHECK(result.invoked_control.has_value());
  CHECK(!std::get<SetDrawerPanelAction>(result.invoked_control->payload)
          .panel.has_value());
}

} // namespace

int main() {
  try {
    test_canonical_compact_sizes_and_explicit_actions();
    test_malformed_models_fail_closed();
    test_invalid_geometry_fails_closed();
    test_keyboard_open_and_close_actions();
    std::cout << "DrawerControlLayoutTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "DrawerControlLayoutTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
