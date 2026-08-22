#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "presentation/AdaptiveShell.hpp"
#include "presentation/ShellControlLayout.hpp"
#include "presentation/WorldContextLayout.hpp"

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

std::string clock_text(int32_t hour, int32_t minute) {
  const int32_t hour_12 = (hour % 12 == 0) ? 12 : hour % 12;
  const std::string hour_prefix = hour_12 < 10 ? "0" : "";
  const std::string minute_prefix = minute < 10 ? "0" : "";
  return hour_prefix + std::to_string(hour_12) + ":" + minute_prefix +
      std::to_string(minute) + (hour < 12 ? " AM" : " PM");
}

WorldConditionModel search_condition(int16_t raw_value) {
  const bool active = raw_value != 0;
  return {
      .raw_value = raw_value,
      .state = {
          .identifier = active
              ? "world.search.active"
              : "world.search.inactive",
          .label = active ? "Search on" : "Search off",
          .emphasis = active
              ? StateEmphasis::information
              : StateEmphasis::inactive,
          .marker = active
              ? StateMarker::condition
              : StateMarker::unavailable,
      },
  };
}

WorldConditionModel torch_condition(int16_t raw_value) {
  const bool active = raw_value != 0;
  return {
      .raw_value = raw_value,
      .state = {
          .identifier = active
              ? "world.torch.lit"
              : "world.torch.unlit",
          .label = active ? "Torch lit" : "Torch unlit",
          .emphasis = active
              ? StateEmphasis::positive
              : StateEmphasis::inactive,
          .marker = active
              ? StateMarker::check
              : StateMarker::unavailable,
      },
  };
}

WorldContextModel context_model(
    WorldPresentation presentation,
    std::optional<WorldPositionView> position,
    int16_t day,
    int32_t hour,
    int32_t minute,
    int16_t search_raw,
    int16_t torch_raw) {
  return {
      .presentation = presentation,
      .visible_position = position,
      .day = day,
      .hour = hour,
      .minute = minute,
      .clock_text = clock_text(hour, minute),
      .search = search_condition(search_raw),
      .torch = torch_condition(torch_raw),
  };
}

LogicalRect action_panel_for(LogicalSize size) {
  const auto plan = compute_adaptive_shell_plan({
      .mode = PresentationMode::remastered,
      .screen = ScreenContext::exploration,
      .window_size = size,
  });
  CHECK(plan.adaptive_layout.has_value());
  return plan.adaptive_layout->action_bar;
}

std::vector<ShellControlPlacement> world_controls(
    LogicalRect panel,
    ScreenContext screen,
    WorldPresentation presentation,
    WorldActionPage page) {
  return compute_shell_control_layout({
      .screen = screen,
      .world_presentation = presentation,
      .action_panel = panel,
      .world_action_page = page,
      .navigation_available = true,
  });
}

std::vector<ShellControlPlacement> tabs_only(
    const std::vector<ShellControlPlacement>& controls) {
  std::vector<ShellControlPlacement> result;
  for (const auto& control : controls) {
    if (control.kind == ShellControlKind::world_action_page) {
      result.emplace_back(control);
    }
  }
  return result;
}

std::string joined_visible_text(const WorldContextLayout& layout) {
  std::string result;
  for (const auto& line : layout.lines) {
    if (!result.empty()) {
      result += "\n";
    }
    result += line.text;
  }
  return result;
}

WorldContextLayout verify_complete_layout(
    const WorldContextModel& model,
    ScreenContext screen,
    LogicalRect panel,
    const std::vector<ShellControlPlacement>& controls,
    double text_scale,
    double backing_scale) {
  const auto model_before = model;
  const auto controls_before = controls;
  const auto type = typography(text_scale);
  const WorldContextLayoutRequest request{
      .world_context = model,
      .screen = screen,
      .action_panel = panel,
      .typography = type,
      .action_controls = controls,
  };
  const auto layout = compute_world_context_layout(request);
  const auto repeat = compute_world_context_layout(request);
  CHECK(layout == repeat);
  CHECK(model == model_before);
  CHECK(controls == controls_before);
  CHECK(panel.contains(layout.bounds));
  CHECK(layout.bounds.width > 0.0);
  check_close(layout.bounds.height, 44.0);
  check_close(layout.bounds.y, panel.y + 10.0);
  check_close(layout.bounds.right(), panel.right() - 14.0);

  CHECK(layout.condition_tokens[0].raw_value == model.search.raw_value);
  CHECK(layout.condition_tokens[0].state == model.search.state);
  CHECK(layout.condition_tokens[1].raw_value == model.torch.raw_value);
  CHECK(layout.condition_tokens[1].state == model.torch.state);
  CHECK(layout.condition_tokens[0].render_text.find("RAW ") !=
      std::string::npos);
  CHECK(layout.condition_tokens[1].render_text.find("RAW ") !=
      std::string::npos);

  const bool compact =
      layout.density == WorldContextLayoutDensity::compact;
  CHECK(layout.lines.size() == (compact ? 2U : 1U));
  for (size_t index = 0; index < layout.lines.size(); ++index) {
    const auto& line = layout.lines[index];
    CHECK(layout.bounds.contains(line.bounds));
    CHECK(line.bounds.width > 0.0);
    CHECK(line.bounds.height > 0.0);
    CHECK(!line.text.empty());
    CHECK(std::isfinite(line.text_style.point_size));
    CHECK(std::isfinite(line.text_style.line_height));
    CHECK(line.text_style.point_size >= 8.0);
    CHECK(line.text_style.line_height >= 10.0);
    for (size_t prior = 0; prior < index; ++prior) {
      CHECK(!interiors_overlap(line.bounds, layout.lines[prior].bounds));
    }
  }
  if (compact) {
    check_close(layout.lines[1].bounds.y -
        layout.lines[0].bounds.bottom(), 2.0);
  }
  const std::string complete_one_line = compact
      ? layout.lines[0].text + " · " + layout.lines[1].text
      : layout.lines[0].text;
  constexpr double kAverageGlyphWidthInEms = 0.58;
  constexpr double kMinimumPointSize = 8.0;
  constexpr double kMinimumLineHeight = 10.0;
  const double line_to_point =
      type.caption.line_height / type.caption.point_size;
  const double fitted_point_size = std::min({
      type.caption.point_size,
      layout.bounds.height / line_to_point,
      layout.bounds.width /
          (kAverageGlyphWidthInEms * complete_one_line.size()),
  });
  const double fitted_line_height = std::min(
      layout.bounds.height,
      fitted_point_size * line_to_point);
  const bool complete_one_line_fits =
      std::isfinite(fitted_point_size) &&
      std::isfinite(fitted_line_height) &&
      (fitted_point_size >= kMinimumPointSize) &&
      (fitted_line_height >= kMinimumLineHeight);
  CHECK((layout.density == WorldContextLayoutDensity::wide) ==
      complete_one_line_fits);

  for (const auto& control : controls) {
    CHECK(control.bounds.width >= 44.0);
    CHECK(control.bounds.height >= 44.0);
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

  const std::string visible = joined_visible_text(layout);
  CHECK(visible.find("DAY " + std::to_string(model.day)) !=
      std::string::npos);
  CHECK(visible.find(model.clock_text) != std::string::npos);
  CHECK(visible.find("RAW " + std::to_string(model.search.raw_value)) !=
      std::string::npos);
  CHECK(visible.find("RAW " + std::to_string(model.torch.raw_value)) !=
      std::string::npos);
  CHECK(layout.accessibility_text.find(
      "day " + std::to_string(model.day)) != std::string::npos);
  CHECK(layout.accessibility_text.find("time " + model.clock_text) !=
      std::string::npos);
  CHECK(layout.accessibility_text.find(model.search.state.label) !=
      std::string::npos);
  CHECK(layout.accessibility_text.find(model.torch.state.label) !=
      std::string::npos);
  if (model.visible_position) {
    CHECK(visible.find(
        "X " + std::to_string(model.visible_position->x)) !=
        std::string::npos);
    CHECK(visible.find(
        "Y " + std::to_string(model.visible_position->y)) !=
        std::string::npos);
    CHECK(layout.accessibility_text.find(
        "coordinates X " + std::to_string(model.visible_position->x) +
        ", Y " + std::to_string(model.visible_position->y)) !=
        std::string::npos);
    CHECK(visible.find("COORDINATES HIDDEN") == std::string::npos);
  } else {
    CHECK(visible.find("X ?  Y ?") != std::string::npos);
    CHECK(visible.find("COORDINATES HIDDEN") != std::string::npos);
    CHECK(layout.accessibility_text.find("coordinates hidden") !=
        std::string::npos);
    CHECK(visible.find("-2147483648") == std::string::npos);
    CHECK(visible.find("2147483647") == std::string::npos);
    CHECK(layout.accessibility_text.find("-2147483648") ==
        std::string::npos);
    CHECK(layout.accessibility_text.find("2147483647") ==
        std::string::npos);
  }
  return layout;
}

void test_complete_responsive_matrix() {
  struct PanelCase {
    LogicalSize window;
    LogicalRect expected;
    WorldContextLayoutDensity density;
  };
  const std::array panels{
      PanelCase{
          .window = {1024.0, 768.0},
          .expected = {24.0, 605.76, 706.24, 138.24},
          .density = WorldContextLayoutDensity::compact,
      },
      PanelCase{
          .window = {1360.0, 768.0},
          .expected = {24.0, 590.4, 976.4, 153.6},
          .density = WorldContextLayoutDensity::wide,
      },
  };
  struct SurfaceCase {
    ScreenContext screen;
    WorldPresentation presentation;
  };
  constexpr std::array surfaces{
      SurfaceCase{ScreenContext::exploration, WorldPresentation::outdoor},
      SurfaceCase{ScreenContext::dungeon, WorldPresentation::dungeon_map},
      SurfaceCase{
          ScreenContext::dungeon,
          WorldPresentation::dungeon_first_person},
  };
  constexpr std::array pages{
      WorldActionPage::travel,
      WorldActionPage::party,
      WorldActionPage::game,
  };
  constexpr std::array text_scales{0.75, 1.0, 2.0};
  constexpr std::array backing_scales{0.75, 1.0, 2.0};
  const std::array<std::optional<WorldPositionView>, 2> positions{
      WorldPositionView{
          std::numeric_limits<int32_t>::min(),
          std::numeric_limits<int32_t>::max()},
      std::nullopt,
  };
  struct ConditionCase {
    int16_t search;
    int16_t torch;
  };
  constexpr std::array conditions{
      ConditionCase{0, 0},
      ConditionCase{std::numeric_limits<int16_t>::min(), 0},
      ConditionCase{0, std::numeric_limits<int16_t>::max()},
      ConditionCase{-1, 119},
  };

  for (const auto& panel_case : panels) {
    const auto panel = action_panel_for(panel_case.window);
    check_close(panel.x, panel_case.expected.x);
    check_close(panel.y, panel_case.expected.y);
    check_close(panel.width, panel_case.expected.width);
    check_close(panel.height, panel_case.expected.height);
    for (const auto surface : surfaces) {
      for (const auto page : pages) {
        const auto controls = world_controls(
            panel, surface.screen, surface.presentation, page);
        CHECK(controls.size() >= 3U);
        CHECK(std::count_if(
            controls.begin(),
            controls.end(),
            [](const auto& control) {
              return control.kind ==
                  ShellControlKind::world_action_page;
            }) == 3);
        for (const auto& position : positions) {
          for (const auto condition : conditions) {
            const int16_t day = position
                ? std::numeric_limits<int16_t>::min()
                : std::numeric_limits<int16_t>::max();
            const auto model = context_model(
                surface.presentation,
                position,
                day,
                23,
                59,
                condition.search,
                condition.torch);
            for (const double text_scale : text_scales) {
              for (const double backing_scale : backing_scales) {
                verify_complete_layout(
                    model,
                    surface.screen,
                    panel,
                    controls,
                    text_scale,
                    backing_scale);
                const auto layout = compute_world_context_layout({
                    .world_context = model,
                    .screen = surface.screen,
                    .action_panel = panel,
                    .typography = typography(text_scale),
                    .action_controls = controls,
                });
                CHECK(layout.density == panel_case.density);
              }
            }
          }
        }
      }
    }
  }
}

void test_clock_and_condition_text_boundaries() {
  const auto panel = action_panel_for({1024.0, 768.0});
  const auto controls = world_controls(
      panel,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      WorldActionPage::travel);
  struct ClockCase {
    int32_t hour;
    int32_t minute;
    const char* text;
  };
  constexpr std::array clocks{
      ClockCase{0, 0, "12:00 AM"},
      ClockCase{0, 5, "12:05 AM"},
      ClockCase{12, 0, "12:00 PM"},
      ClockCase{23, 59, "11:59 PM"},
  };
  for (const auto clock : clocks) {
    const auto model = context_model(
        WorldPresentation::outdoor,
        WorldPositionView{-91, 73},
        -12,
        clock.hour,
        clock.minute,
        -1,
        119);
    CHECK(model.clock_text == clock.text);
    const auto layout = compute_world_context_layout({
        .world_context = model,
        .screen = ScreenContext::exploration,
        .action_panel = panel,
        .typography = typography(1.0),
        .action_controls = controls,
    });
    CHECK(layout.lines.size() == 2U);
    CHECK(layout.lines[0].text ==
        "X -91  Y 73 · DAY -12 · " + std::string(clock.text));
    CHECK(layout.lines[1].text ==
        "[*] SEARCH ON · RAW -1 · [+] TORCH LIT · RAW 119");
    CHECK(layout.condition_tokens[0].marker_text == "[*]");
    CHECK(layout.condition_tokens[1].marker_text == "[+]");
  }

  const auto inactive = context_model(
      WorldPresentation::outdoor,
      std::nullopt,
      3,
      12,
      0,
      0,
      0);
  const auto layout = compute_world_context_layout({
      .world_context = inactive,
      .screen = ScreenContext::exploration,
      .action_panel = panel,
      .typography = typography(1.0),
      .action_controls = controls,
  });
  CHECK(layout.lines[0].text ==
      "X ?  Y ? · COORDINATES HIDDEN · DAY 3 · 12:00 PM");
  CHECK(layout.lines[1].text ==
      "[-] SEARCH OFF · RAW 0 · [-] TORCH UNLIT · RAW 0");
  CHECK(layout.lines[1].emphasis == StateEmphasis::inactive);
}

void test_every_supported_width_selects_content_fitting_density() {
  struct SurfaceCase {
    ScreenContext screen;
    WorldPresentation presentation;
  };
  constexpr std::array surfaces{
      SurfaceCase{ScreenContext::exploration, WorldPresentation::outdoor},
      SurfaceCase{ScreenContext::dungeon, WorldPresentation::dungeon_map},
      SurfaceCase{
          ScreenContext::dungeon,
          WorldPresentation::dungeon_first_person},
  };
  constexpr std::array pages{
      WorldActionPage::travel,
      WorldActionPage::party,
      WorldActionPage::game,
  };
  constexpr std::array<int, 4> boundary_widths{1208, 1209, 1238, 1239};
  std::array<std::optional<WorldContextLayoutDensity>, 4>
      visible_boundary_densities{};
  std::array<std::optional<WorldContextLayoutDensity>, 4>
      hidden_boundary_densities{};
  bool saw_visible_compact = false;
  bool saw_visible_wide = false;
  bool saw_hidden_compact = false;
  bool saw_hidden_wide = false;

  for (int width = 1024; width <= 1600; ++width) {
    const auto panel = action_panel_for({
        static_cast<double>(width),
        768.0,
    });
    for (size_t surface_index = 0;
         surface_index < surfaces.size();
         ++surface_index) {
      const auto surface = surfaces[surface_index];
      for (size_t page_index = 0; page_index < pages.size(); ++page_index) {
        const auto controls = world_controls(
            panel,
            surface.screen,
            surface.presentation,
            pages[page_index]);
        const std::array models{
            context_model(
                surface.presentation,
                WorldPositionView{
                    std::numeric_limits<int32_t>::min(),
                    std::numeric_limits<int32_t>::max()},
                std::numeric_limits<int16_t>::min(),
                23,
                59,
                std::numeric_limits<int16_t>::min(),
                std::numeric_limits<int16_t>::max()),
            context_model(
                surface.presentation,
                std::nullopt,
                std::numeric_limits<int16_t>::max(),
                23,
                59,
                std::numeric_limits<int16_t>::min(),
                std::numeric_limits<int16_t>::max()),
        };
        for (size_t model_index = 0; model_index < models.size();
             ++model_index) {
          const auto layout = verify_complete_layout(
              models[model_index],
              surface.screen,
              panel,
              controls,
              2.0,
              0.75);
          const bool compact =
              layout.density == WorldContextLayoutDensity::compact;
          if (model_index == 0U) {
            saw_visible_compact = saw_visible_compact || compact;
            saw_visible_wide = saw_visible_wide || !compact;
          } else {
            saw_hidden_compact = saw_hidden_compact || compact;
            saw_hidden_wide = saw_hidden_wide || !compact;
          }
          if ((surface_index == 0U) && (page_index == 0U)) {
            for (size_t boundary_index = 0;
                 boundary_index < boundary_widths.size();
                 ++boundary_index) {
              if (width == boundary_widths[boundary_index]) {
                auto& destination = model_index == 0U
                    ? visible_boundary_densities[boundary_index]
                    : hidden_boundary_densities[boundary_index];
                destination = layout.density;
              }
            }
          }
        }
      }
    }
  }

  CHECK(saw_visible_compact);
  CHECK(saw_visible_wide);
  CHECK(saw_hidden_compact);
  CHECK(saw_hidden_wide);
  constexpr std::array visible_expected{
      WorldContextLayoutDensity::compact,
      WorldContextLayoutDensity::compact,
      WorldContextLayoutDensity::compact,
      WorldContextLayoutDensity::wide,
  };
  constexpr std::array hidden_expected{
      WorldContextLayoutDensity::compact,
      WorldContextLayoutDensity::compact,
      WorldContextLayoutDensity::compact,
      WorldContextLayoutDensity::compact,
  };
  for (size_t index = 0; index < boundary_widths.size(); ++index) {
    CHECK(visible_boundary_densities[index].has_value());
    CHECK(hidden_boundary_densities[index].has_value());
    CHECK(*visible_boundary_densities[index] == visible_expected[index]);
    CHECK(*hidden_boundary_densities[index] == hidden_expected[index]);
  }
}

void expect_invalid(
    const WorldContextModel& model,
    ScreenContext screen,
    LogicalRect panel,
    const TypographyModel& type,
    const std::vector<ShellControlPlacement>& controls) {
  bool threw = false;
  try {
    (void)compute_world_context_layout({
        .world_context = model,
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

void test_invalid_models_geometry_typography_and_tabs_fail_closed() {
  const auto panel = action_panel_for({1024.0, 768.0});
  const auto full_controls = world_controls(
      panel,
      ScreenContext::exploration,
      WorldPresentation::outdoor,
      WorldActionPage::travel);
  const auto valid_tabs = tabs_only(full_controls);
  const auto valid = context_model(
      WorldPresentation::outdoor,
      WorldPositionView{12, -34},
      5,
      0,
      5,
      -1,
      119);
  const auto type = typography(1.0);

  expect_invalid(
      valid, ScreenContext::combat, panel, type, valid_tabs);
  expect_invalid(
      valid, ScreenContext::dungeon, panel, type, valid_tabs);
  auto bad_surface = valid;
  bad_surface.presentation = WorldPresentation::dungeon_map;
  expect_invalid(
      bad_surface,
      ScreenContext::exploration,
      panel,
      type,
      valid_tabs);
  bad_surface.presentation = WorldPresentation::none;
  expect_invalid(
      bad_surface,
      ScreenContext::exploration,
      panel,
      type,
      valid_tabs);

  auto bad_clock = valid;
  bad_clock.hour = -1;
  expect_invalid(bad_clock, ScreenContext::exploration, panel, type, valid_tabs);
  bad_clock = valid;
  bad_clock.hour = 24;
  expect_invalid(bad_clock, ScreenContext::exploration, panel, type, valid_tabs);
  bad_clock = valid;
  bad_clock.minute = -1;
  expect_invalid(bad_clock, ScreenContext::exploration, panel, type, valid_tabs);
  bad_clock = valid;
  bad_clock.minute = 60;
  expect_invalid(bad_clock, ScreenContext::exploration, panel, type, valid_tabs);
  bad_clock = valid;
  bad_clock.clock_text = "0:05 AM";
  expect_invalid(bad_clock, ScreenContext::exploration, panel, type, valid_tabs);
  bad_clock.clock_text.clear();
  expect_invalid(bad_clock, ScreenContext::exploration, panel, type, valid_tabs);

  auto bad_token = valid;
  bad_token.search.state = search_condition(0).state;
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.search.state.identifier.clear();
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.search.state.label = "Searching";
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.search.state.emphasis = StateEmphasis::positive;
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.search.state.marker = StateMarker::check;
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.torch.state = torch_condition(0).state;
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.torch.state.identifier.clear();
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.torch.state.label = "Torch on";
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.torch.state.emphasis = StateEmphasis::information;
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);
  bad_token = valid;
  bad_token.torch.state.marker = StateMarker::condition;
  expect_invalid(bad_token, ScreenContext::exploration, panel, type, valid_tabs);

  auto bad_panel = panel;
  bad_panel.x = std::numeric_limits<double>::quiet_NaN();
  expect_invalid(valid, ScreenContext::exploration, bad_panel, type, valid_tabs);
  bad_panel = panel;
  bad_panel.width = 0.0;
  expect_invalid(valid, ScreenContext::exploration, bad_panel, type, valid_tabs);
  bad_panel = panel;
  bad_panel.height = std::numeric_limits<double>::infinity();
  expect_invalid(valid, ScreenContext::exploration, bad_panel, type, valid_tabs);

  auto bad_type = type;
  bad_type.scale = std::numeric_limits<double>::quiet_NaN();
  expect_invalid(valid, ScreenContext::exploration, panel, bad_type, valid_tabs);
  bad_type = type;
  bad_type.caption.point_size = 0.0;
  expect_invalid(valid, ScreenContext::exploration, panel, bad_type, valid_tabs);
  bad_type = type;
  bad_type.body.line_height = -1.0;
  expect_invalid(valid, ScreenContext::exploration, panel, bad_type, valid_tabs);
  bad_type = type;
  bad_type.heading.line_height = 1.0;
  expect_invalid(valid, ScreenContext::exploration, panel, bad_type, valid_tabs);

  auto bad_tabs = valid_tabs;
  bad_tabs.pop_back();
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs.emplace_back(valid_tabs.front());
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  std::swap(bad_tabs[0], bad_tabs[1]);
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[1].payload = MovePartyAction{MovementCommand::north};
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[1].kind = ShellControlKind::movement;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  std::get<SetWorldActionPageAction>(bad_tabs[1].payload).page =
      WorldActionPage::travel;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[1].bounds.x = bad_tabs[0].bounds.right();
  bad_tabs[2].bounds.x = bad_tabs[1].bounds.right();
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[2].bounds.x += 1.0;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[1].bounds.x = bad_tabs[0].bounds.x + 10.0;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[2].bounds.x = panel.right();
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[0].bounds.height = 43.0;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  for (auto& tab : bad_tabs) {
    tab.selected = false;
  }
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[0].selected = true;
  bad_tabs[1].selected = true;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[2].enabled = false;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[2].tab_order = bad_tabs[1].tab_order;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[0].region = ShellRegionId{7000};
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[0].label = "WORLD";
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[1].accessibility_label = "Forged party tab";
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[2].focus_identifier = "focus.action.world.page.forged";
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[1].bounds.width += 1.0;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  for (auto& tab : bad_tabs) {
    tab.bounds.x += 1.0;
  }
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  for (size_t index = 0; index < bad_tabs.size(); ++index) {
    bad_tabs[index].bounds.width -= 1.0;
    bad_tabs[index].bounds.x -= static_cast<double>(index);
  }
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[1].bounds.x += 1.0;
  bad_tabs[2].bounds.x += 2.0;
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
  bad_tabs = valid_tabs;
  bad_tabs[0].bounds.x = std::numeric_limits<double>::infinity();
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);

  const auto baseline = compute_world_context_layout({
      .world_context = valid,
      .screen = ScreenContext::exploration,
      .action_panel = panel,
      .typography = type,
      .action_controls = valid_tabs,
  });
  bad_tabs = valid_tabs;
  bad_tabs.emplace_back(ShellControlPlacement{
      .region = ShellRegionId{9999},
      .kind = ShellControlKind::movement,
      .bounds = baseline.bounds,
      .label = "FORGED",
      .accessibility_label = "Forged overlapping action",
      .focus_identifier = "focus.forged",
      .tab_order = 9999,
      .enabled = true,
      .payload = MovePartyAction{MovementCommand::north},
  });
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);

  // A large uniform shift is also rejected at the canonical geometry
  // boundary, before any text-fitting decision can observe it.
  bad_tabs = valid_tabs;
  for (auto& tab : bad_tabs) {
    tab.bounds.x += 250.0;
  }
  expect_invalid(valid, ScreenContext::exploration, panel, type, bad_tabs);
}

} // namespace

int main() {
  try {
    test_complete_responsive_matrix();
    test_clock_and_condition_text_boundaries();
    test_every_supported_width_selects_content_fitting_density();
    test_invalid_models_geometry_typography_and_tabs_fail_closed();
    std::cout << "WorldContextLayoutTest: " << checks_run
              << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "WorldContextLayoutTest failed after " << checks_run
              << " checks: " << e.what() << "\n";
    return 1;
  }
}
