#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "presentation/AdaptiveShell.hpp"
#include "presentation/CombatAwarenessLayout.hpp"
#include "presentation/ShellControlLayout.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " +
        expression);
  }
}

#define CHECK(expression) \
  check(static_cast<bool>(expression), #expression, __LINE__)

void check_close(double first, double second, double tolerance = 1e-9) {
  CHECK(std::abs(first - second) <= tolerance);
}

bool interiors_overlap(LogicalRect first, LogicalRect second) {
  return std::max(first.x, second.x) <
          std::min(first.right(), second.right()) &&
      std::max(first.y, second.y) <
          std::min(first.bottom(), second.bottom());
}

bool interiors_overlap(PhysicalRect first, PhysicalRect second) {
  return std::max(first.x, second.x) <
          std::min(first.right(), second.right()) &&
      std::max(first.y, second.y) <
          std::min(first.bottom(), second.bottom());
}

bool contains(PhysicalRect outer, PhysicalRect inner) {
  return (inner.x >= outer.x) && (inner.y >= outer.y) &&
      (inner.right() <= outer.right()) &&
      (inner.bottom() <= outer.bottom());
}

TypographyModel typography(double scale) {
  return {
      .scale = scale,
      .caption = {13.0 * scale, 17.0 * scale},
      .body = {16.0 * scale, 22.0 * scale},
      .heading = {20.0 * scale, 26.0 * scale},
  };
}

LogicalRect action_panel_for(double window_width) {
  const auto plan = compute_adaptive_shell_plan({
      .mode = PresentationMode::remastered,
      .screen = ScreenContext::combat,
      .window_size = {window_width, 768.0},
  });
  CHECK(plan.adaptive_layout.has_value());
  return plan.adaptive_layout->action_bar;
}

std::vector<ShellControlPlacement> party_turn_controls(
    LogicalRect panel,
    CombatActionPage page) {
  ShellControlLayoutRequest request;
  request.screen = ScreenContext::combat;
  request.action_panel = panel;
  request.guard_combatant = CombatantId{2};
  request.guard_available = true;
  request.finish_combatant = CombatantId{2};
  request.finish_available = true;
  request.delay_combatant = CombatantId{2};
  request.delay_available = true;
  request.center_active_combatant = CombatantId{2};
  request.center_active_available = true;
  request.combat_action_page = page;
  request.switch_weapon_combatant = CombatantId{2};
  request.switch_weapon_available = true;
  request.center_previous_combatant = CombatantId{2};
  request.center_previous_available = true;
  request.center_next_combatant = CombatantId{2};
  request.center_next_available = true;
  request.combat_items = OpenCombatItemsAction{
      CombatantId{2}, PartyMemberId{1}};
  request.combat_items_available = true;
  request.auto_combatant = CombatantId{2};
  request.auto_combatant_available = true;
  request.show_combat_range_combatant = CombatantId{2};
  request.show_combat_range_available = true;
  request.bandage_combatant = CombatantId{2};
  request.bandage_combatant_available = true;
  request.undo_combatant = CombatantId{2};
  request.undo_combatant_available = true;
  request.open_combat_spellbook = CombatantId{2};
  request.open_combat_spellbook_available = true;
  request.open_combat_targeting = CombatantId{2};
  request.open_combat_targeting_available = true;
  request.escape_combat = CombatantId{2};
  request.escape_combat_available = true;
  request.open_combat_scroll_case = CombatantId{2};
  request.open_combat_scroll_case_available = true;
  request.center_combat_cursor = CenterCombatCursorAction{2, {42, 17}};
  request.center_combat_cursor_available = true;
  return compute_shell_control_layout(request);
}

std::vector<ShellControlPlacement> tabs_only(
    const std::vector<ShellControlPlacement>& controls) {
  CHECK(controls.size() >= 4U);
  return {controls.begin(), controls.begin() + 4};
}

std::string complete_text(const CombatAwarenessModel& model) {
  return "ROUND " + std::to_string(model.round) +
      " · ENEMIES LEFT " + std::to_string(model.enemies_remaining);
}

bool complete_text_fits(
    const TypographyModel& type,
    LogicalRect bounds,
    size_t character_count) {
  constexpr double kAverageGlyphWidthInEms = 0.58;
  constexpr double kMinimumPointSize = 8.0;
  constexpr double kMinimumLineHeight = 10.0;
  const double line_to_point =
      type.caption.line_height / type.caption.point_size;
  const double point_size = std::min({
      type.caption.point_size,
      bounds.height / line_to_point,
      bounds.width /
          (kAverageGlyphWidthInEms *
              static_cast<double>(character_count)),
  });
  const double line_height = std::min(
      bounds.height, point_size * line_to_point);
  return std::isfinite(point_size) && std::isfinite(line_height) &&
      (point_size >= kMinimumPointSize) &&
      (line_height >= kMinimumLineHeight);
}

CombatAwarenessLayout verify_complete_layout(
    const CombatAwarenessModel& model,
    LogicalRect panel,
    const std::vector<ShellControlPlacement>& controls,
    double text_scale,
    double backing_scale) {
  const auto model_before = model;
  const auto controls_before = controls;
  const auto type = typography(text_scale);
  const CombatAwarenessLayoutRequest request{
      .combat_awareness = model,
      .screen = ScreenContext::combat,
      .action_panel = panel,
      .typography = type,
      .action_controls = controls,
  };
  const auto layout = compute_combat_awareness_layout(request);
  const auto repeat = compute_combat_awareness_layout(request);
  CHECK(layout == repeat);
  CHECK(model == model_before);
  CHECK(controls == controls_before);
  CHECK(panel.contains(layout.bounds));
  CHECK(layout.bounds.width > 0.0);
  check_close(layout.bounds.y, panel.y + 10.0);
  check_close(layout.bounds.height, 44.0);
  check_close(layout.bounds.right(), panel.right() - 14.0);
  CHECK(layout.owns_full_zero_tab_header == controls.empty());
  if (controls.empty()) {
    check_close(layout.bounds.x, panel.x + 14.0);
  } else {
    CHECK(controls.size() >= 4U);
    constexpr double kMinimumGap = 6.0;
    constexpr double kMaximumGap = 10.0;
    const double available_width = panel.width - 28.0;
    const double gap = std::clamp(
        available_width * 0.008, kMinimumGap, kMaximumGap);
    check_close(layout.bounds.x, controls[3].bounds.right() + gap);
  }

  const std::string full_text = complete_text(model);
  const bool expected_wide = complete_text_fits(
      type, layout.bounds, full_text.size());
  CHECK((layout.density == CombatAwarenessLayoutDensity::wide) ==
      expected_wide);
  CHECK(layout.lines.size() == (expected_wide ? 1U : 2U));
  if (expected_wide) {
    CHECK(layout.lines[0].text == full_text);
    CHECK(layout.lines[0].bounds == layout.bounds);
  } else {
    CHECK(layout.lines[0].text ==
        "ROUND " + std::to_string(model.round));
    CHECK(layout.lines[1].text ==
        "ENEMIES LEFT " + std::to_string(model.enemies_remaining));
    check_close(
        layout.lines[1].bounds.y - layout.lines[0].bounds.bottom(),
        2.0);
  }
  CHECK(layout.accessibility_text ==
      "Combat awareness; round " + std::to_string(model.round) +
      "; enemies remaining " +
      std::to_string(model.enemies_remaining) + ".");

  for (size_t index = 0; index < layout.lines.size(); ++index) {
    const auto& line = layout.lines[index];
    CHECK(layout.bounds.contains(line.bounds));
    CHECK(!line.text.empty());
    CHECK(std::isfinite(line.text_style.point_size));
    CHECK(std::isfinite(line.text_style.line_height));
    CHECK(line.text_style.point_size >= 8.0);
    CHECK(line.text_style.line_height >= 10.0);
    CHECK(line.text_style.line_height >= line.text_style.point_size);
    CHECK(line.emphasis == StateEmphasis::information);
    for (size_t prior = 0; prior < index; ++prior) {
      CHECK(!interiors_overlap(line.bounds, layout.lines[prior].bounds));
    }
  }
  for (const auto& control : controls) {
    CHECK(panel.contains(control.bounds));
    CHECK(!interiors_overlap(layout.bounds, control.bounds));
    for (const auto& line : layout.lines) {
      CHECK(!interiors_overlap(line.bounds, control.bounds));
    }
  }

  const BackingTransform transform(backing_scale);
  const auto physical_panel = transform.to_physical(panel);
  const auto physical_layout = transform.to_physical(layout.bounds);
  CHECK(contains(physical_panel, physical_layout));
  CHECK(physical_layout.width > 0);
  CHECK(physical_layout.height > 0);
  for (size_t index = 0; index < controls.size(); ++index) {
    const auto physical_control = transform.to_physical(
        controls[index].bounds);
    CHECK(contains(physical_panel, physical_control));
    CHECK(!interiors_overlap(physical_layout, physical_control));
    for (size_t prior = 0; prior < index; ++prior) {
      CHECK(!interiors_overlap(
          physical_control,
          transform.to_physical(controls[prior].bounds)));
    }
  }
  for (size_t index = 0; index < layout.lines.size(); ++index) {
    const auto physical_line = transform.to_physical(
        layout.lines[index].bounds);
    CHECK(contains(physical_layout, physical_line));
    CHECK(physical_line.width > 0);
    CHECK(physical_line.height > 0);
    for (size_t prior = 0; prior < index; ++prior) {
      CHECK(!interiors_overlap(
          physical_line,
          transform.to_physical(layout.lines[prior].bounds)));
    }
    for (const auto& control : controls) {
      CHECK(!interiors_overlap(
          physical_line,
          transform.to_physical(control.bounds)));
    }
  }
  return layout;
}

void test_exact_wording_and_two_header_shapes() {
  const LogicalRect wide_panel{20.0, 500.0, 900.0, 150.0};
  const auto controls = party_turn_controls(
      wide_panel, CombatActionPage::primary);
  CHECK(controls.size() == 8U);
  const CombatAwarenessModel model{-12, 27};
  const auto tabbed = verify_complete_layout(
      model, wide_panel, controls, 1.0, 1.0);
  CHECK(tabbed.density == CombatAwarenessLayoutDensity::wide);
  CHECK(tabbed.lines[0].text == "ROUND -12 · ENEMIES LEFT 27");
  CHECK(!tabbed.owns_full_zero_tab_header);

  const std::vector<ShellControlPlacement> empty_controls;
  const auto zero_tab = verify_complete_layout(
      model, wide_panel, empty_controls, 1.0, 1.0);
  CHECK(zero_tab.density == CombatAwarenessLayoutDensity::wide);
  CHECK(zero_tab.lines[0].text == "ROUND -12 · ENEMIES LEFT 27");
  CHECK(zero_tab.owns_full_zero_tab_header);
  CHECK(zero_tab.bounds.width > tabbed.bounds.width);
}

void test_content_driven_compact_and_wide_densities() {
  constexpr std::array pages{
      CombatActionPage::primary,
      CombatActionPage::secondary,
      CombatActionPage::utility,
      CombatActionPage::special,
  };
  const CombatAwarenessModel extremes{-128, -255};
  for (const auto page : pages) {
    const LogicalRect compact_panel{0.0, 0.0, 600.0, 120.0};
    const auto compact_controls = party_turn_controls(compact_panel, page);
    const auto compact = verify_complete_layout(
        extremes, compact_panel, compact_controls, 2.0, 0.75);
    CHECK(compact.density == CombatAwarenessLayoutDensity::compact);
    CHECK(compact.lines.size() == 2U);

    const LogicalRect wide_panel{0.0, 0.0, 900.0, 120.0};
    const auto wide_controls = party_turn_controls(wide_panel, page);
    const auto wide = verify_complete_layout(
        extremes, wide_panel, wide_controls, 2.0, 2.0);
    CHECK(wide.density == CombatAwarenessLayoutDensity::wide);
    CHECK(wide.lines.size() == 1U);
  }

  const std::vector<ShellControlPlacement> no_controls;
  const LogicalRect compact_zero_panel{0.0, 0.0, 150.0, 64.0};
  const auto compact_zero = verify_complete_layout(
      extremes, compact_zero_panel, no_controls, 2.0, 1.0);
  CHECK(compact_zero.density == CombatAwarenessLayoutDensity::compact);
  CHECK(compact_zero.owns_full_zero_tab_header);

  const LogicalRect wide_zero_panel{0.0, 0.0, 300.0, 64.0};
  const auto wide_zero = verify_complete_layout(
      extremes, wide_zero_panel, no_controls, 2.0, 1.0);
  CHECK(wide_zero.density == CombatAwarenessLayoutDensity::wide);
  CHECK(wide_zero.owns_full_zero_tab_header);
}

void test_every_supported_width_page_turn_shape_scale_and_extreme() {
  constexpr std::array pages{
      CombatActionPage::primary,
      CombatActionPage::secondary,
      CombatActionPage::utility,
      CombatActionPage::special,
  };
  constexpr std::array models{
      CombatAwarenessModel{-128, -255},
      CombatAwarenessModel{127, 255},
  };
  constexpr std::array text_scales{0.75, 1.0, 2.0};
  constexpr std::array backing_scales{0.75, 1.0, 2.0};
  const std::vector<ShellControlPlacement> zero_tab_controls;
  bool saw_four_tab_shape = false;
  bool saw_zero_tab_shape = false;

  for (int width = 1024; width <= 1600; ++width) {
    const auto panel = action_panel_for(static_cast<double>(width));
    for (const auto page : pages) {
      const auto controls = party_turn_controls(panel, page);
      CHECK(controls.size() >= 5U);
      CHECK(std::count_if(
          controls.begin(),
          controls.end(),
          [](const auto& control) {
            return control.kind == ShellControlKind::combat_action_page;
          }) == 4);
      for (const auto model : models) {
        for (const double text_scale : text_scales) {
          for (const double backing_scale : backing_scales) {
            const auto tabbed = verify_complete_layout(
                model,
                panel,
                controls,
                text_scale,
                backing_scale);
            const auto zero_tab = verify_complete_layout(
                model,
                panel,
                zero_tab_controls,
                text_scale,
                backing_scale);
            saw_four_tab_shape = saw_four_tab_shape ||
                !tabbed.owns_full_zero_tab_header;
            saw_zero_tab_shape = saw_zero_tab_shape ||
                zero_tab.owns_full_zero_tab_header;
          }
        }
      }
    }
  }
  CHECK(saw_four_tab_shape);
  CHECK(saw_zero_tab_shape);
}

void test_tab_only_rows_remain_page_persistent() {
  constexpr std::array pages{
      CombatActionPage::primary,
      CombatActionPage::secondary,
      CombatActionPage::utility,
      CombatActionPage::special,
  };
  const auto panel = action_panel_for(1024.0);
  for (const auto page : pages) {
    const auto controls = party_turn_controls(panel, page);
    const auto tabs = tabs_only(controls);
    const auto layout = verify_complete_layout(
        CombatAwarenessModel{1, 4}, panel, tabs, 1.0, 1.0);
    CHECK(!layout.owns_full_zero_tab_header);
    CHECK(std::count_if(
        tabs.begin(),
        tabs.end(),
        [](const auto& tab) { return tab.selected; }) == 1);
  }
}

void expect_invalid(
    const CombatAwarenessModel& model,
    ScreenContext screen,
    LogicalRect panel,
    const TypographyModel& type,
    const std::vector<ShellControlPlacement>& controls) {
  bool threw = false;
  try {
    (void)compute_combat_awareness_layout({
        .combat_awareness = model,
        .screen = screen,
        .action_panel = panel,
        .typography = type,
        .action_controls = controls,
    });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void test_invalid_values_surfaces_panels_and_typography_fail_closed() {
  const auto panel = action_panel_for(1024.0);
  const auto controls = party_turn_controls(
      panel, CombatActionPage::primary);
  const auto type = typography(1.0);
  const CombatAwarenessModel valid{1, 4};

  expect_invalid({-129, 0}, ScreenContext::combat, panel, type, controls);
  expect_invalid({128, 0}, ScreenContext::combat, panel, type, controls);
  expect_invalid({0, -256}, ScreenContext::combat, panel, type, controls);
  expect_invalid({0, 256}, ScreenContext::combat, panel, type, controls);
  expect_invalid(valid, ScreenContext::title, panel, type, controls);
  expect_invalid(valid, ScreenContext::exploration, panel, type, controls);
  expect_invalid(valid, ScreenContext::dungeon, panel, type, controls);

  auto bad_panel = panel;
  bad_panel.x = std::numeric_limits<double>::quiet_NaN();
  expect_invalid(valid, ScreenContext::combat, bad_panel, type, controls);
  bad_panel = panel;
  bad_panel.width = 0.0;
  expect_invalid(valid, ScreenContext::combat, bad_panel, type, controls);
  bad_panel = panel;
  bad_panel.height = std::numeric_limits<double>::infinity();
  expect_invalid(valid, ScreenContext::combat, bad_panel, type, controls);
  bad_panel = {0.0, 0.0, 20.0, 20.0};
  const std::vector<ShellControlPlacement> no_controls;
  expect_invalid(valid, ScreenContext::combat, bad_panel, type, no_controls);

  auto bad_type = type;
  bad_type.scale = 0.0;
  expect_invalid(valid, ScreenContext::combat, panel, bad_type, controls);
  bad_type = type;
  bad_type.scale = std::numeric_limits<double>::quiet_NaN();
  expect_invalid(valid, ScreenContext::combat, panel, bad_type, controls);
  bad_type = type;
  bad_type.caption.point_size = 0.0;
  expect_invalid(valid, ScreenContext::combat, panel, bad_type, controls);
  bad_type = type;
  bad_type.caption.line_height = 1.0;
  expect_invalid(valid, ScreenContext::combat, panel, bad_type, controls);
  bad_type = type;
  bad_type.body.line_height = std::numeric_limits<double>::infinity();
  expect_invalid(valid, ScreenContext::combat, panel, bad_type, controls);
  bad_type = type;
  bad_type.heading.point_size = -1.0;
  expect_invalid(valid, ScreenContext::combat, panel, bad_type, controls);

  // Positive but impractically small typography must not silently violate
  // the 8-point/10-line-height floor.
  bad_type = typography(1.0);
  bad_type.caption = {7.99, 10.0};
  expect_invalid(valid, ScreenContext::combat, panel, bad_type, controls);
}

void test_partial_forged_mixed_and_overlapping_controls_fail_closed() {
  const auto panel = action_panel_for(1024.0);
  const auto valid = party_turn_controls(
      panel, CombatActionPage::primary);
  CHECK(valid.size() == 8U);
  const CombatAwarenessModel model{1, 4};
  const auto type = typography(1.0);

  for (size_t count = 1U; count < 4U; ++count) {
    const std::vector<ShellControlPlacement> partial(
        valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(count));
    expect_invalid(model, ScreenContext::combat, panel, type, partial);
  }
  std::vector<ShellControlPlacement> bad{valid[4]};
  expect_invalid(model, ScreenContext::combat, panel, type, bad);

  bad = valid;
  std::swap(bad[0], bad[1]);
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[0].kind = ShellControlKind::world_action_page;
  bad[0].payload = SetWorldActionPageAction{WorldActionPage::travel};
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[0].payload = SetCombatActionPageAction{CombatActionPage::special};
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[0].region = ShellRegionId{9999U};
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[1].label = "ITEMS";
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[2].accessibility_label = "Forged tab";
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[3].focus_identifier = "focus.forged";
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[1].tab_order = 9999;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[2].enabled = false;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[0].selected = false;
  bad[0].accessibility_label = "Turn combat commands tab";
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[1].selected = true;
  bad[1].accessibility_label += ", selected";
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[0].bounds.x += 1.0;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[1].bounds.width += 1.0;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  for (size_t index = 0; index < 4U; ++index) {
    bad[index].bounds.x += 1.0;
  }
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  const double canonical_tab_gap =
      valid[1].bounds.x - valid[0].bounds.right();
  const double shrunken_tab_width = valid[0].bounds.width - 1.0;
  for (size_t index = 0; index < 4U; ++index) {
    bad[index].bounds.x = valid[0].bounds.x +
        static_cast<double>(index) *
            (shrunken_tab_width + canonical_tab_gap);
    bad[index].bounds.width = shrunken_tab_width;
  }
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  const double forged_tab_gap = canonical_tab_gap + 1.0;
  for (size_t index = 0; index < 4U; ++index) {
    bad[index].bounds.x = valid[0].bounds.x +
        static_cast<double>(index) *
            (valid[0].bounds.width + forged_tab_gap);
  }
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad.emplace_back(valid[0]);
  expect_invalid(model, ScreenContext::combat, panel, type, bad);

  bad = valid;
  bad[4].kind = ShellControlKind::movement;
  bad[4].payload = MovePartyAction{MovementCommand::north};
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[4].payload = FinishCombatantAction{2};
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[4].label = "WAIT";
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[4].bounds.x += 1.0;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[4].bounds = bad[5].bounds;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  bad[4].bounds.y = panel.y + 10.0;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  std::get<FinishCombatantAction>(bad[5].payload).combatant = 3;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = valid;
  std::get<GuardCombatantAction>(bad[4].payload).combatant = -1;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);

  const auto secondary = party_turn_controls(
      panel, CombatActionPage::secondary);
  CHECK(secondary.size() == 8U);
  bad = secondary;
  std::get<OpenCombatItemsAction>(bad[7].payload).member = 6;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
  bad = secondary;
  std::get<CycleCombatFocusAction>(bad[5].payload).direction =
      static_cast<CombatFocusDirection>(99);
  expect_invalid(model, ScreenContext::combat, panel, type, bad);

  const auto special = party_turn_controls(
      panel, CombatActionPage::special);
  CHECK(special.size() == 9U);
  bad = special;
  std::get<CenterCombatCursorAction>(bad[8].payload).cell.x = 90U;
  expect_invalid(model, ScreenContext::combat, panel, type, bad);
}

} // namespace

int main() {
  try {
    test_exact_wording_and_two_header_shapes();
    test_content_driven_compact_and_wide_densities();
    test_every_supported_width_page_turn_shape_scale_and_extreme();
    test_tab_only_rows_remain_page_persistent();
    test_invalid_values_surfaces_panels_and_typography_fail_closed();
    test_partial_forged_mixed_and_overlapping_controls_fail_closed();
    std::cout << "CombatAwarenessLayoutTest: " << checks_run
              << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "CombatAwarenessLayoutTest failed after " << checks_run
              << " checks: " << e.what() << "\n";
    return 1;
  }
}
