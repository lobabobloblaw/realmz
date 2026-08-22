#include "WorldContextLayout.hpp"

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

constexpr double kPanelRightInset = 14.0;
constexpr double kHeaderTopInset = 10.0;
constexpr double kHeaderHeight = 44.0;
constexpr double kTabGapScale = 0.008;
constexpr double kMinimumTabGap = 6.0;
constexpr double kMaximumTabGap = 10.0;
constexpr double kMaximumTabWidth = 112.0;
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

[[nodiscard]] std::string expected_clock_text(
    int32_t hour,
    int32_t minute) {
  const int32_t hour_12 = (hour % 12 == 0) ? 12 : hour % 12;
  return std::format(
      "{:02}:{:02} {}",
      hour_12,
      minute,
      hour < 12 ? "AM" : "PM");
}

[[nodiscard]] std::string_view marker_text(StateMarker marker) noexcept {
  switch (marker) {
    case StateMarker::none:
      return "";
    case StateMarker::selection:
      return "[>]";
    case StateMarker::information:
      return "[i]";
    case StateMarker::check:
      return "[+]";
    case StateMarker::alert:
      return "[!]";
    case StateMarker::stop:
      return "[X]";
    case StateMarker::unavailable:
      return "[-]";
    case StateMarker::condition:
      return "[*]";
  }
  return "";
}

void validate_condition(
    const WorldConditionModel& condition,
    bool search) {
  const bool active = condition.raw_value != 0;
  const std::string_view expected_identifier = search
      ? (active ? "world.search.active" : "world.search.inactive")
      : (active ? "world.torch.lit" : "world.torch.unlit");
  const std::string_view expected_label = search
      ? (active ? "Search on" : "Search off")
      : (active ? "Torch lit" : "Torch unlit");
  const StateEmphasis expected_emphasis = active
      ? (search ? StateEmphasis::information : StateEmphasis::positive)
      : StateEmphasis::inactive;
  const StateMarker expected_marker = active
      ? (search ? StateMarker::condition : StateMarker::check)
      : StateMarker::unavailable;
  if ((condition.state.identifier != expected_identifier) ||
      (condition.state.label != expected_label) ||
      (condition.state.emphasis != expected_emphasis) ||
      (condition.state.marker != expected_marker)) {
    throw std::invalid_argument(
        search
            ? "World Context Search token contradicts its exact raw state"
            : "World Context Torch token contradicts its exact raw state");
  }
}

void validate_model(const WorldContextLayoutRequest& request) {
  const bool valid_surface =
      ((request.screen == ScreenContext::exploration) &&
          (request.world_context.presentation ==
              WorldPresentation::outdoor)) ||
      ((request.screen == ScreenContext::dungeon) &&
          ((request.world_context.presentation ==
                  WorldPresentation::dungeon_map) ||
              (request.world_context.presentation ==
                  WorldPresentation::dungeon_first_person)));
  if (!valid_surface) {
    throw std::invalid_argument(
        "World Context requires a matching exploration or dungeon surface");
  }
  if (!request.action_panel.is_finite_and_nonnegative() ||
      !std::isfinite(request.action_panel.right()) ||
      !std::isfinite(request.action_panel.bottom()) ||
      (request.action_panel.width <= 0.0) ||
      (request.action_panel.height <= 0.0)) {
    throw std::invalid_argument(
        "World Context action panel must be finite and positive");
  }
  if (!std::isfinite(request.typography.scale) ||
      (request.typography.scale <= 0.0) ||
      !valid_text_style(request.typography.caption) ||
      !valid_text_style(request.typography.body) ||
      !valid_text_style(request.typography.heading)) {
    throw std::invalid_argument(
        "World Context typography must be finite and positive");
  }
  if ((request.world_context.hour < 0) ||
      (request.world_context.hour > 23) ||
      (request.world_context.minute < 0) ||
      (request.world_context.minute > 59) ||
      (request.world_context.clock_text != expected_clock_text(
          request.world_context.hour,
          request.world_context.minute))) {
    throw std::invalid_argument(
        "World Context clock must be an exact zero-padded 12-hour value");
  }
  validate_condition(request.world_context.search, true);
  validate_condition(request.world_context.torch, false);
}

struct ValidatedHeader {
  LogicalRect strip_bounds;
};

[[nodiscard]] ValidatedHeader validate_header(
    const WorldContextLayoutRequest& request) {
  std::array<const ShellControlPlacement*, 3> tabs{};
  size_t tab_count = 0U;
  for (const auto& control : request.action_controls) {
    const auto* page = std::get_if<SetWorldActionPageAction>(
        &control.payload);
    if (control.kind == ShellControlKind::world_action_page) {
      if (!page || (tab_count >= tabs.size())) {
        throw std::invalid_argument(
            "World Context requires exactly three typed world tabs");
      }
      tabs[tab_count++] = &control;
    } else if (page) {
      throw std::invalid_argument(
          "World Context rejects a world-page payload on a forged control");
    }
  }
  if (tab_count != tabs.size()) {
    throw std::invalid_argument(
        "World Context requires exactly three typed world tabs");
  }

  constexpr std::array kExpectedPages{
      WorldActionPage::travel,
      WorldActionPage::party,
      WorldActionPage::game,
  };
  constexpr std::array<uint32_t, 3> kExpectedRegions{1210U, 1211U, 1212U};
  constexpr std::array<std::string_view, 3> kExpectedLabels{
      "TRAVEL", "PARTY", "GAME"};
  constexpr std::array<std::string_view, 3> kExpectedAccessibilityLabels{
      "Travel commands tab",
      "Party commands tab",
      "Game commands tab",
  };
  constexpr std::array<std::string_view, 3> kExpectedFocusIdentifiers{
      "focus.action.world.page.travel",
      "focus.action.world.page.party",
      "focus.action.world.page.game",
  };
  size_t selected_count = 0U;
  for (size_t index = 0; index < tabs.size(); ++index) {
    const auto& tab = *tabs[index];
    const auto* page = std::get_if<SetWorldActionPageAction>(&tab.payload);
    const std::string expected_accessibility_label =
        std::string(kExpectedAccessibilityLabels[index]) +
        (tab.selected ? ", selected" : "");
    if (!page || (page->page != kExpectedPages[index]) ||
        (tab.region.value != kExpectedRegions[index]) ||
        (tab.label != kExpectedLabels[index]) ||
        (tab.accessibility_label != expected_accessibility_label) ||
        (tab.focus_identifier != kExpectedFocusIdentifiers[index]) ||
        (tab.tab_order != 900 + static_cast<int32_t>(index)) ||
        !tab.enabled ||
        !tab.bounds.is_finite_and_nonnegative() ||
        !std::isfinite(tab.bounds.right()) ||
        !std::isfinite(tab.bounds.bottom()) ||
        (tab.bounds.width < 44.0) ||
        (tab.bounds.width > 112.0) ||
        !nearly_equal(tab.bounds.height, kHeaderHeight) ||
        !request.action_panel.contains(tab.bounds) ||
        !nearly_equal(tab.bounds.y,
            request.action_panel.y + kHeaderTopInset) ||
        ((index > 0U) &&
            ((tab.bounds.x <= tabs[index - 1U]->bounds.x) ||
                !nearly_equal(
                    tab.bounds.width,
                    tabs[index - 1U]->bounds.width)))) {
      throw std::invalid_argument(
          "World Context world tabs must be ordered, live, and contained");
    }
    selected_count += tab.selected ? 1U : 0U;
    for (size_t prior = 0; prior < index; ++prior) {
      if (interiors_overlap(tab.bounds, tabs[prior]->bounds)) {
        throw std::invalid_argument(
            "World Context world tabs must not overlap");
      }
    }
  }
  if (selected_count != 1U) {
    throw std::invalid_argument(
        "World Context requires exactly one selected world tab");
  }

  // ShellControlLayout is the sole geometry authority for this persistent
  // tab row. Recompute its responsive values so a typed-but-forged row cannot
  // move or resize the information strip while still looking internally
  // consistent.
  const double available_width =
      request.action_panel.width - 2.0 * kPanelRightInset;
  const double expected_gap = std::clamp(
      available_width * kTabGapScale,
      kMinimumTabGap,
      kMaximumTabGap);
  const double expected_width = std::min(
      kMaximumTabWidth,
      (available_width - expected_gap * (tabs.size() - 1U)) /
          tabs.size());
  if (!std::isfinite(expected_gap) || !std::isfinite(expected_width) ||
      (expected_width < kHeaderHeight)) {
    throw std::invalid_argument(
        "World Context panel cannot contain the canonical world tabs");
  }
  for (size_t index = 0; index < tabs.size(); ++index) {
    const double expected_x = request.action_panel.x + kPanelRightInset +
        static_cast<double>(index) * (expected_width + expected_gap);
    if (!nearly_equal(tabs[index]->bounds.x, expected_x) ||
        !nearly_equal(tabs[index]->bounds.width, expected_width)) {
      throw std::invalid_argument(
          "World Context requires canonical world-tab geometry");
    }
  }

  const double first_gap = tabs[1]->bounds.x - tabs[0]->bounds.right();
  const double second_gap = tabs[2]->bounds.x - tabs[1]->bounds.right();
  if (!std::isfinite(first_gap) || !std::isfinite(second_gap) ||
      (first_gap <= 0.0) || (second_gap <= 0.0) ||
      !nearly_equal(first_gap, second_gap)) {
    throw std::invalid_argument(
        "World Context world tabs require one consistent positive gap");
  }

  const double strip_left = tabs[2]->bounds.right() + second_gap;
  const double strip_right = request.action_panel.right() -
      kPanelRightInset;
  const LogicalRect strip{
      strip_left,
      tabs[0]->bounds.y,
      strip_right - strip_left,
      kHeaderHeight,
  };
  if (!strip.is_finite_and_nonnegative() ||
      !std::isfinite(strip.right()) || !std::isfinite(strip.bottom()) ||
      (strip.width <= 0.0) || !request.action_panel.contains(strip)) {
    throw std::invalid_argument(
        "World Context world tabs leave no contained header strip");
  }

  for (const auto& control : request.action_controls) {
    if (!control.bounds.is_finite_and_nonnegative() ||
        !std::isfinite(control.bounds.right()) ||
        !std::isfinite(control.bounds.bottom())) {
      throw std::invalid_argument(
          "World Context action-control geometry must be finite");
    }
    if ((control.kind != ShellControlKind::world_action_page) &&
        interiors_overlap(control.bounds, strip)) {
      throw std::invalid_argument(
          "World Context header must not overlap an action control");
    }
  }
  return {.strip_bounds = strip};
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
      bounds.height,
      point_size * line_to_point);
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

[[nodiscard]] TextStyleModel fit_text_style(
    const TextStyleModel& requested,
    LogicalRect bounds,
    size_t character_count) {
  const auto fitted = fitted_text_style(
      requested, bounds, character_count);
  if (!fitted) {
    throw std::invalid_argument(
        "World Context text cannot honor the practical text floor");
  }
  return *fitted;
}

[[nodiscard]] int emphasis_rank(StateEmphasis emphasis) noexcept {
  switch (emphasis) {
    case StateEmphasis::inactive:
      return 0;
    case StateEmphasis::neutral:
      return 1;
    case StateEmphasis::information:
      return 2;
    case StateEmphasis::positive:
      return 3;
    case StateEmphasis::caution:
      return 4;
    case StateEmphasis::critical:
      return 5;
  }
  return 1;
}

[[nodiscard]] StateEmphasis strongest_emphasis(
    std::span<const StateEmphasis> values) noexcept {
  if (values.empty()) {
    return StateEmphasis::neutral;
  }
  StateEmphasis result = values.front();
  for (const auto value : values.subspan(1U)) {
    if (emphasis_rank(value) > emphasis_rank(result)) {
      result = value;
    }
  }
  return result;
}

[[nodiscard]] WorldContextConditionLayout layout_condition(
    const WorldConditionModel& condition,
    bool search) {
  const bool active = condition.raw_value != 0;
  const std::string marker(marker_text(condition.state.marker));
  const std::string state_text = search
      ? (active ? "SEARCH ON" : "SEARCH OFF")
      : (active ? "TORCH LIT" : "TORCH UNLIT");
  return {
      .raw_value = condition.raw_value,
      .state = condition.state,
      .marker_text = marker,
      .render_text = std::format(
          "{} {} · RAW {}", marker, state_text, condition.raw_value),
  };
}

} // namespace

WorldContextLayout compute_world_context_layout(
    const WorldContextLayoutRequest& request) {
  validate_model(request);
  const auto header = validate_header(request);

  std::string position_text;
  std::string accessible_position;
  if (request.world_context.visible_position) {
    position_text = std::format(
        "X {}  Y {}",
        request.world_context.visible_position->x,
        request.world_context.visible_position->y);
    accessible_position = std::format(
        "coordinates X {}, Y {}",
        request.world_context.visible_position->x,
        request.world_context.visible_position->y);
  } else {
    position_text = "X ?  Y ? · COORDINATES HIDDEN";
    accessible_position = "coordinates hidden";
  }

  const std::array condition_tokens{
      layout_condition(request.world_context.search, true),
      layout_condition(request.world_context.torch, false),
  };
  const std::string context_text = std::format(
      "{} · DAY {} · {}",
      position_text,
      request.world_context.day,
      request.world_context.clock_text);
  const std::string conditions_text = std::format(
      "{} · {}",
      condition_tokens[0].render_text,
      condition_tokens[1].render_text);
  const std::string full_text = context_text + " · " + conditions_text;
  const bool full_line_fits = fitted_text_style(
      request.typography.caption,
      header.strip_bounds,
      full_text.size()).has_value();
  WorldContextLayout result{
      .bounds = header.strip_bounds,
      .density = full_line_fits
          ? WorldContextLayoutDensity::wide
          : WorldContextLayoutDensity::compact,
      .condition_tokens = condition_tokens,
  };
  const std::array condition_emphases{
      result.condition_tokens[0].state.emphasis,
      result.condition_tokens[1].state.emphasis,
  };
  const auto condition_emphasis = strongest_emphasis(condition_emphases);

  if (result.density == WorldContextLayoutDensity::compact) {
    const double line_height =
        (result.bounds.height - kCompactLineGap) / 2.0;
    const LogicalRect first_line{
        result.bounds.x,
        result.bounds.y,
        result.bounds.width,
        line_height,
    };
    const LogicalRect second_line{
        result.bounds.x,
        first_line.bottom() + kCompactLineGap,
        result.bounds.width,
        line_height,
    };
    result.lines.emplace_back(WorldContextLineLayout{
        .bounds = first_line,
        .text = context_text,
        .text_style = fit_text_style(
            request.typography.caption,
            first_line,
            context_text.size()),
        .emphasis = StateEmphasis::information,
    });
    result.lines.emplace_back(WorldContextLineLayout{
        .bounds = second_line,
        .text = conditions_text,
        .text_style = fit_text_style(
            request.typography.caption,
            second_line,
            conditions_text.size()),
        .emphasis = condition_emphasis,
    });
  } else {
    const std::array wide_emphases{
        StateEmphasis::information,
        condition_emphasis,
    };
    result.lines.emplace_back(WorldContextLineLayout{
        .bounds = result.bounds,
        .text = full_text,
        .text_style = fit_text_style(
            request.typography.caption,
            result.bounds,
            full_text.size()),
        .emphasis = strongest_emphasis(wide_emphases),
    });
  }

  result.accessibility_text = std::format(
      "World context; {}; day {}; time {}; {}, raw {}; {}, raw {}.",
      accessible_position,
      request.world_context.day,
      request.world_context.clock_text,
      request.world_context.search.state.label,
      request.world_context.search.raw_value,
      request.world_context.torch.state.label,
      request.world_context.torch.raw_value);
  return result;
}

} // namespace realmz::presentation
