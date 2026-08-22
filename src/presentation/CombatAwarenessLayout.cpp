#include "CombatAwarenessLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace realmz::presentation {
namespace {

constexpr double kHorizontalInset = 14.0;
constexpr double kHeaderTopInset = 10.0;
constexpr double kHeaderHeight = 44.0;
constexpr double kControlsTopInset = 64.0;
constexpr double kBottomInset = 12.0;
constexpr double kTabGapScale = 0.008;
constexpr double kMinimumGap = 6.0;
constexpr double kMaximumGap = 10.0;
constexpr double kMaximumTabWidth = 112.0;
constexpr double kMaximumActionWidth = 160.0;
constexpr double kMinimumTargetExtent = 44.0;
constexpr double kMaximumActionHeight = 52.0;
constexpr double kCompactLineGap = 2.0;
constexpr double kAverageGlyphWidthInEms = 0.58;
constexpr double kMinimumPracticalPointSize = 8.0;
constexpr double kMinimumPracticalLineHeight = 10.0;

[[nodiscard]] bool nearly_equal(double first, double second) noexcept {
  constexpr double kTolerance = 1e-9;
  return std::abs(first - second) <=
      kTolerance * std::max({1.0, std::abs(first), std::abs(second)});
}

[[nodiscard]] bool interiors_overlap(
    LogicalRect first,
    LogicalRect second) noexcept {
  return std::max(first.x, second.x) <
          std::min(first.right(), second.right()) &&
      std::max(first.y, second.y) <
          std::min(first.bottom(), second.bottom());
}

[[nodiscard]] bool valid_text_style(const TextStyleModel& style) noexcept {
  return std::isfinite(style.point_size) &&
      std::isfinite(style.line_height) &&
      (style.point_size > 0.0) &&
      (style.line_height >= style.point_size);
}

void validate_request_values(const CombatAwarenessLayoutRequest& request) {
  if (request.screen != ScreenContext::combat) {
    throw std::invalid_argument(
        "Combat Awareness requires the combat screen");
  }
  if ((request.combat_awareness.round < -128) ||
      (request.combat_awareness.round > 127) ||
      (request.combat_awareness.enemies_remaining < -255) ||
      (request.combat_awareness.enemies_remaining > 255)) {
    throw std::invalid_argument(
        "Combat Awareness values exceed their Classic display domains");
  }
  if (!request.action_panel.is_finite_and_nonnegative() ||
      !std::isfinite(request.action_panel.right()) ||
      !std::isfinite(request.action_panel.bottom()) ||
      (request.action_panel.width <= 0.0) ||
      (request.action_panel.height <= 0.0)) {
    throw std::invalid_argument(
        "Combat Awareness action panel must be finite and positive");
  }
  if (!std::isfinite(request.typography.scale) ||
      (request.typography.scale <= 0.0) ||
      !valid_text_style(request.typography.caption) ||
      !valid_text_style(request.typography.body) ||
      !valid_text_style(request.typography.heading)) {
    throw std::invalid_argument(
        "Combat Awareness typography must be finite and positive");
  }
}

struct HeaderShape {
  LogicalRect bounds;
  bool owns_full_zero_tab_header = false;
};

struct ActionExpectation {
  CombatActionPage page = CombatActionPage::primary;
  size_t order = 0U;
  uint32_t region = 0U;
  std::string_view label;
  std::string_view accessibility_label;
  std::string_view focus_identifier;
  int32_t tab_order = 0;
  CombatantId combatant = 0;
};

[[nodiscard]] bool valid_combatant(CombatantId combatant) noexcept {
  return (combatant >= 0) && (combatant <= 0xFF);
}

[[nodiscard]] std::optional<ActionExpectation> action_expectation(
    const ShellControlPlacement& control) {
  switch (control.kind) {
    case ShellControlKind::guard_combatant: {
      const auto* payload = std::get_if<GuardCombatantAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::primary, 0U, 1104U,
          "GUARD", "Guard active combatant",
          "focus.action.combat.guard", 1104, payload->combatant};
    }
    case ShellControlKind::finish_combatant: {
      const auto* payload = std::get_if<FinishCombatantAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::primary, 1U, 1105U,
          "FINISH", "Finish active combatant's turn",
          "focus.action.combat.finish", 1105, payload->combatant};
    }
    case ShellControlKind::delay_combatant: {
      const auto* payload = std::get_if<DelayCombatantAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::primary, 2U, 1106U,
          "DELAY", "Delay active combatant's turn",
          "focus.action.combat.delay", 1106, payload->combatant};
    }
    case ShellControlKind::center_active_combatant: {
      const auto* payload = std::get_if<CenterActiveCombatantAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::primary, 3U, 1107U,
          "CENTER", "Center view on active combatant",
          "focus.action.combat.center", 1107, payload->combatant};
    }
    case ShellControlKind::switch_weapon_set: {
      const auto* payload = std::get_if<SwitchWeaponSetAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::secondary, 0U, 1109U,
          "WEAPON", "Switch active combatant's weapon set",
          "focus.action.combat.weapon", 1109, payload->combatant};
    }
    case ShellControlKind::cycle_combat_focus: {
      const auto* payload = std::get_if<CycleCombatFocusAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      if (payload->direction == CombatFocusDirection::previous) {
        return ActionExpectation{CombatActionPage::secondary, 1U, 1110U,
            "PREV", "Center view on previous combatant",
            "focus.action.combat.center.previous", 1110,
            payload->combatant};
      }
      if (payload->direction == CombatFocusDirection::next) {
        return ActionExpectation{CombatActionPage::secondary, 2U, 1111U,
            "NEXT", "Center view on next combatant",
            "focus.action.combat.center.next", 1111,
            payload->combatant};
      }
      return std::nullopt;
    }
    case ShellControlKind::open_combat_items: {
      const auto* payload = std::get_if<OpenCombatItemsAction>(
          &control.payload);
      if (!payload || (payload->member >= 6U)) return std::nullopt;
      return ActionExpectation{CombatActionPage::secondary, 3U, 1112U,
          "ITEMS", "Open combat items", "focus.action.combat.items",
          1112, payload->combatant};
    }
    case ShellControlKind::auto_combatant: {
      const auto* payload = std::get_if<AutoCombatantAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::utility, 0U, 1114U,
          "AUTO", "Auto-play active combatant's turn",
          "focus.action.combat.auto", 1114, payload->combatant};
    }
    case ShellControlKind::show_combat_range: {
      const auto* payload = std::get_if<ShowCombatRangeAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::utility, 1U, 1115U,
          "RANGE", "Show combat ranges; press any key to close",
          "focus.action.combat.range", 1115, payload->combatant};
    }
    case ShellControlKind::bandage_combatant: {
      const auto* payload = std::get_if<BandageCombatantAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::utility, 2U, 1116U,
          "BANDAGE", "Choose a party member to bandage",
          "focus.action.combat.bandage", 1116, payload->combatant};
    }
    case ShellControlKind::undo_combatant: {
      const auto* payload = std::get_if<UndoCombatantAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::utility, 3U, 1117U,
          "UNDO", "Undo active combatant's movement",
          "focus.action.combat.undo", 1117, payload->combatant};
    }
    case ShellControlKind::open_combat_spellbook: {
      const auto* payload = std::get_if<OpenCombatSpellbookAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::special, 0U, 1119U,
          "CAST", "Open combat spell chooser",
          "focus.action.combat.spellbook.open", 1119,
          payload->combatant};
    }
    case ShellControlKind::open_combat_targeting: {
      const auto* payload = std::get_if<OpenCombatTargetingAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::special, 1U, 1120U,
          "TARGET", "Begin combat targeting",
          "focus.action.combat.targeting.open", 1120,
          payload->combatant};
    }
    case ShellControlKind::escape_combat: {
      const auto* payload = std::get_if<EscapeCombatAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::special, 2U, 1121U,
          "ESCAPE", "Attempt to escape combat",
          "focus.action.combat.escape", 1121, payload->combatant};
    }
    case ShellControlKind::open_combat_scroll_case: {
      const auto* payload = std::get_if<OpenCombatScrollCaseAction>(
          &control.payload);
      if (!payload) return std::nullopt;
      return ActionExpectation{CombatActionPage::special, 3U, 1122U,
          "SCROLL", "Open combat scroll chooser",
          "focus.action.combat.scroll_case.open", 1122,
          payload->combatant};
    }
    case ShellControlKind::center_combat_cursor: {
      const auto* payload = std::get_if<CenterCombatCursorAction>(
          &control.payload);
      if (!payload || (payload->cell.x > 89U) ||
          (payload->cell.y > 89U)) {
        return std::nullopt;
      }
      return ActionExpectation{CombatActionPage::special, 4U, 1123U,
          "CURSOR", "Center combat view on cursor",
          "focus.action.combat.center.cursor", 1123,
          payload->combatant};
    }
    default:
      return std::nullopt;
  }
}

void validate_action_row(
    const CombatAwarenessLayoutRequest& request,
    CombatActionPage selected_page) {
  const size_t action_count = request.action_controls.size() - 4U;
  if (action_count == 0U) {
    return;
  }
  const double available_width =
      request.action_panel.width - 2.0 * kHorizontalInset;
  const double gap = std::clamp(
      available_width * kTabGapScale, kMinimumGap, kMaximumGap);
  const double button_width = std::min(
      kMaximumActionWidth,
      (available_width - gap * static_cast<double>(action_count - 1U)) /
          static_cast<double>(action_count));
  const double button_height = std::min(
      kMaximumActionHeight,
      request.action_panel.height - kControlsTopInset - kBottomInset);
  if (!std::isfinite(button_width) || !std::isfinite(button_height) ||
      (button_width < kMinimumTargetExtent) ||
      (button_height < kMinimumTargetExtent)) {
    throw std::invalid_argument(
        "Combat Awareness requires a canonical contained action row");
  }

  std::optional<size_t> prior_order;
  std::optional<CombatantId> common_combatant;
  for (size_t index = 0; index < action_count; ++index) {
    const auto& control = request.action_controls[index + 4U];
    const auto expectation = action_expectation(control);
    if (!expectation || (expectation->page != selected_page) ||
        (prior_order && (expectation->order <= *prior_order)) ||
        !valid_combatant(expectation->combatant) ||
        (common_combatant &&
            (*common_combatant != expectation->combatant)) ||
        (control.region.value != expectation->region) ||
        (control.label != expectation->label) ||
        (control.accessibility_label != expectation->accessibility_label) ||
        (control.focus_identifier != expectation->focus_identifier) ||
        (control.tab_order != expectation->tab_order) || control.selected) {
      throw std::invalid_argument(
          "Combat Awareness rejects forged or mixed combat actions");
    }
    prior_order = expectation->order;
    common_combatant = expectation->combatant;

    const LogicalRect expected_bounds{
        request.action_panel.x + kHorizontalInset +
            static_cast<double>(index) * (button_width + gap),
        request.action_panel.y + kControlsTopInset,
        button_width,
        button_height,
    };
    if (!nearly_equal(control.bounds.x, expected_bounds.x) ||
        !nearly_equal(control.bounds.y, expected_bounds.y) ||
        !nearly_equal(control.bounds.width, expected_bounds.width) ||
        !nearly_equal(control.bounds.height, expected_bounds.height)) {
      throw std::invalid_argument(
          "Combat Awareness requires canonical combat-action geometry");
    }
  }
}

[[nodiscard]] HeaderShape validate_header_shape(
    const CombatAwarenessLayoutRequest& request) {
  if (request.action_controls.empty()) {
    const LogicalRect full_header{
        request.action_panel.x + kHorizontalInset,
        request.action_panel.y + kHeaderTopInset,
        request.action_panel.width - 2.0 * kHorizontalInset,
        kHeaderHeight,
    };
    if (!full_header.is_finite_and_nonnegative() ||
        !std::isfinite(full_header.right()) ||
        !std::isfinite(full_header.bottom()) ||
        (full_header.width <= 0.0) ||
        !request.action_panel.contains(full_header)) {
      throw std::invalid_argument(
          "Combat Awareness empty deck cannot contain its full header");
    }
    return {
        .bounds = full_header,
        .owns_full_zero_tab_header = true,
    };
  }

  if (request.action_controls.size() < 4U) {
    throw std::invalid_argument(
        "Combat Awareness rejects a partial combat tab row");
  }

  constexpr std::array kExpectedPages{
      CombatActionPage::primary,
      CombatActionPage::secondary,
      CombatActionPage::utility,
      CombatActionPage::special,
  };
  constexpr std::array<std::string_view, 4> kExpectedLabels{
      "TURN", "GEAR", "TACTICS", "SPECIAL"};
  constexpr std::array<std::string_view, 4> kExpectedAccessibilityLabels{
      "Turn combat commands tab",
      "Gear and view combat commands tab",
      "Tactical combat commands tab",
      "Special combat commands tab",
  };
  constexpr std::array<std::string_view, 4> kExpectedFocusIdentifiers{
      "focus.action.combat.page.turn",
      "focus.action.combat.page.gear",
      "focus.action.combat.page.tactics",
      "focus.action.combat.page.special",
  };

  const double available_width =
      request.action_panel.width - 2.0 * kHorizontalInset;
  const double gap = std::clamp(
      available_width * kTabGapScale, kMinimumGap, kMaximumGap);
  const double tab_width = std::min(
      kMaximumTabWidth,
      (available_width - gap * 3.0) / 4.0);
  if (!std::isfinite(gap) || !std::isfinite(tab_width) ||
      (tab_width < kMinimumTargetExtent)) {
    throw std::invalid_argument(
        "Combat Awareness panel cannot contain canonical combat tabs");
  }

  size_t selected_count = 0U;
  CombatActionPage selected_page = CombatActionPage::primary;
  for (size_t index = 0; index < kExpectedPages.size(); ++index) {
    const auto& tab = request.action_controls[index];
    const auto* payload = std::get_if<SetCombatActionPageAction>(
        &tab.payload);
    const std::string expected_accessibility =
        std::string(kExpectedAccessibilityLabels[index]) +
        (tab.selected ? ", selected" : "");
    const LogicalRect expected_bounds{
        request.action_panel.x + kHorizontalInset +
            static_cast<double>(index) * (tab_width + gap),
        request.action_panel.y + kHeaderTopInset,
        tab_width,
        kHeaderHeight,
    };
    if ((tab.kind != ShellControlKind::combat_action_page) || !payload ||
        (payload->page != kExpectedPages[index]) ||
        (tab.region.value != 1200U + index) ||
        (tab.label != kExpectedLabels[index]) ||
        (tab.accessibility_label != expected_accessibility) ||
        (tab.focus_identifier != kExpectedFocusIdentifiers[index]) ||
        (tab.tab_order != 1100 + static_cast<int32_t>(index)) ||
        !tab.enabled ||
        !nearly_equal(tab.bounds.x, expected_bounds.x) ||
        !nearly_equal(tab.bounds.y, expected_bounds.y) ||
        !nearly_equal(tab.bounds.width, expected_bounds.width) ||
        !nearly_equal(tab.bounds.height, expected_bounds.height) ||
        !request.action_panel.contains(tab.bounds)) {
      throw std::invalid_argument(
          "Combat Awareness requires canonical typed combat tabs");
    }
    if (tab.selected) {
      ++selected_count;
      selected_page = payload->page;
    }
  }
  if (selected_count != 1U) {
    throw std::invalid_argument(
        "Combat Awareness requires exactly one selected combat tab");
  }

  // A tab payload outside the first four positions, or any non-combat action,
  // is never one of ShellControlLayout's two authoritative combat shapes.
  for (size_t index = 4U; index < request.action_controls.size(); ++index) {
    const auto& control = request.action_controls[index];
    if ((control.kind == ShellControlKind::combat_action_page) ||
        std::holds_alternative<SetCombatActionPageAction>(control.payload) ||
        !action_expectation(control).has_value()) {
      throw std::invalid_argument(
          "Combat Awareness rejects extra, forged, or mixed controls");
    }
  }
  validate_action_row(request, selected_page);

  const double strip_left = request.action_controls[3].bounds.right() + gap;
  const double strip_right = request.action_panel.right() - kHorizontalInset;
  const LogicalRect strip{
      strip_left,
      request.action_panel.y + kHeaderTopInset,
      strip_right - strip_left,
      kHeaderHeight,
  };
  if (!strip.is_finite_and_nonnegative() ||
      !std::isfinite(strip.right()) || !std::isfinite(strip.bottom()) ||
      (strip.width <= 0.0) || !request.action_panel.contains(strip)) {
    throw std::invalid_argument(
        "Combat Awareness tabs leave no contained header strip");
  }

  for (size_t index = 0; index < request.action_controls.size(); ++index) {
    const auto& control = request.action_controls[index];
    if (!control.bounds.is_finite_and_nonnegative() ||
        !std::isfinite(control.bounds.right()) ||
        !std::isfinite(control.bounds.bottom()) ||
        (control.bounds.width <= 0.0) ||
        (control.bounds.height <= 0.0) ||
        !request.action_panel.contains(control.bounds) ||
        interiors_overlap(control.bounds, strip)) {
      throw std::invalid_argument(
          "Combat Awareness header and controls must be contained and separate");
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (interiors_overlap(
              control.bounds, request.action_controls[prior].bounds)) {
        throw std::invalid_argument(
            "Combat Awareness rejects overlapping controls");
      }
    }
  }
  return {.bounds = strip, .owns_full_zero_tab_header = false};
}

[[nodiscard]] std::optional<TextStyleModel> fitted_text_style(
    const TextStyleModel& requested,
    LogicalRect bounds,
    size_t character_count) noexcept {
  const double line_to_point = requested.line_height / requested.point_size;
  const double height_limited_points = bounds.height / line_to_point;
  const double width_limited_points = bounds.width /
      (kAverageGlyphWidthInEms *
          static_cast<double>(std::max<size_t>(character_count, 1U)));
  const double point_size = std::min({
      requested.point_size,
      height_limited_points,
      width_limited_points,
  });
  const double line_height = std::min(
      bounds.height, point_size * line_to_point);
  if (!std::isfinite(point_size) || !std::isfinite(line_height) ||
      (point_size < kMinimumPracticalPointSize) ||
      (line_height < kMinimumPracticalLineHeight)) {
    return std::nullopt;
  }
  return TextStyleModel{
      .point_size = point_size,
      .line_height = line_height,
  };
}

[[nodiscard]] TextStyleModel require_fitted_text_style(
    const TextStyleModel& requested,
    LogicalRect bounds,
    size_t character_count) {
  const auto fitted = fitted_text_style(requested, bounds, character_count);
  if (!fitted) {
    throw std::invalid_argument(
        "Combat Awareness text cannot honor the practical text floor");
  }
  return *fitted;
}

} // namespace

CombatAwarenessLayout compute_combat_awareness_layout(
    const CombatAwarenessLayoutRequest& request) {
  validate_request_values(request);
  const HeaderShape header = validate_header_shape(request);

  const std::string round_text = std::format(
      "ROUND {}", request.combat_awareness.round);
  const std::string enemies_text = std::format(
      "ENEMIES LEFT {}", request.combat_awareness.enemies_remaining);
  const std::string full_text = round_text + " · " + enemies_text;
  const bool full_line_fits = fitted_text_style(
      request.typography.caption,
      header.bounds,
      full_text.size()).has_value();

  CombatAwarenessLayout result{
      .bounds = header.bounds,
      .density = full_line_fits
          ? CombatAwarenessLayoutDensity::wide
          : CombatAwarenessLayoutDensity::compact,
      .lines = {},
      .accessibility_text = {},
      .owns_full_zero_tab_header =
          header.owns_full_zero_tab_header,
  };
  if (full_line_fits) {
    result.lines.emplace_back(CombatAwarenessLineLayout{
        .bounds = result.bounds,
        .text = full_text,
        .text_style = require_fitted_text_style(
            request.typography.caption,
            result.bounds,
            full_text.size()),
        .emphasis = StateEmphasis::information,
    });
  } else {
    const double line_height =
        (result.bounds.height - kCompactLineGap) / 2.0;
    const LogicalRect round_bounds{
        result.bounds.x,
        result.bounds.y,
        result.bounds.width,
        line_height,
    };
    const LogicalRect enemies_bounds{
        result.bounds.x,
        round_bounds.bottom() + kCompactLineGap,
        result.bounds.width,
        line_height,
    };
    result.lines.emplace_back(CombatAwarenessLineLayout{
        .bounds = round_bounds,
        .text = round_text,
        .text_style = require_fitted_text_style(
            request.typography.caption,
            round_bounds,
            round_text.size()),
        .emphasis = StateEmphasis::information,
    });
    result.lines.emplace_back(CombatAwarenessLineLayout{
        .bounds = enemies_bounds,
        .text = enemies_text,
        .text_style = require_fitted_text_style(
            request.typography.caption,
            enemies_bounds,
            enemies_text.size()),
        .emphasis = StateEmphasis::information,
    });
  }

  result.accessibility_text = std::format(
      "Combat awareness; round {}; enemies remaining {}.",
      request.combat_awareness.round,
      request.combat_awareness.enemies_remaining);
  return result;
}

} // namespace realmz::presentation
