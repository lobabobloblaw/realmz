#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "presentation/AdaptiveShell.hpp"
#include "presentation/RemasteredInputMapper.hpp"

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

bool approximately_equal(LogicalPoint a, LogicalPoint b) {
  return approximately_equal(a.x, b.x) && approximately_equal(a.y, b.y);
}

bool is_positive(LogicalRect rect) {
  return rect.is_finite_and_nonnegative() &&
      rect.width > 0.0 && rect.height > 0.0;
}

bool interiors_overlap(LogicalRect a, LogicalRect b) {
  return std::max(a.x, b.x) < std::min(a.right(), b.right()) &&
      std::max(a.y, b.y) < std::min(a.bottom(), b.bottom());
}

const BlitClassicFrameCommand& classic_blit(const AdaptiveShellPlan& plan) {
  for (const auto& command : plan.commands) {
    if (const auto* blit = std::get_if<BlitClassicFrameCommand>(&command)) {
      return *blit;
    }
  }
  throw std::runtime_error("plan does not contain a Classic-frame blit");
}

bool has_panel(const AdaptiveShellPlan& plan, ShellPanelKind kind) {
  return std::ranges::any_of(plan.commands, [kind](const auto& command) {
    const auto* panel = std::get_if<DrawShellPanelCommand>(&command);
    return (panel != nullptr) && (panel->panel == kind);
  });
}

size_t panel_count(const AdaptiveShellPlan& plan) {
  return static_cast<size_t>(std::ranges::count_if(
      plan.commands,
      [](const auto& command) {
        return std::holds_alternative<DrawShellPanelCommand>(command);
      }));
}

const DrawShellPanelCommand& panel_command(
    const AdaptiveShellPlan& plan,
    ShellPanelKind kind) {
  const DrawShellPanelCommand* result = nullptr;
  for (const auto& command : plan.commands) {
    if (const auto* panel = std::get_if<DrawShellPanelCommand>(&command);
        panel && panel->panel == kind) {
      CHECK(result == nullptr);
      result = panel;
    }
  }
  if (!result) {
    throw std::runtime_error("plan does not contain the requested panel");
  }
  return *result;
}

void verify_command_geometry(const AdaptiveShellPlan& plan) {
  CHECK(is_positive(plan.window));
  const BackingTransform backing(plan.backing_scale);
  size_t clear_count = 0;
  size_t blit_count = 0;
  std::vector<LogicalRect> rendered_surfaces;

  for (const auto& command : plan.commands) {
    std::visit([&](const auto& concrete) {
      CHECK(is_positive(concrete.destination));
      CHECK(plan.window.contains(concrete.destination));
      CHECK(concrete.backing_destination ==
          backing.to_physical(concrete.destination));
      using Command = std::decay_t<decltype(concrete)>;
      if constexpr (std::is_same_v<Command, ClearShellCommand>) {
        ++clear_count;
        CHECK(concrete.destination == plan.window);
      } else {
        rendered_surfaces.emplace_back(concrete.destination);
        if constexpr (std::is_same_v<Command, BlitClassicFrameCommand>) {
          ++blit_count;
          CHECK(is_positive(concrete.source));
          CHECK(kClassicFrameRect.contains(concrete.source));
        }
      }
    }, command);
  }

  CHECK(clear_count == 1);
  CHECK(blit_count == 1);
  CHECK(std::holds_alternative<ClearShellCommand>(plan.commands.front()));
  CHECK(plan.classic_interaction_regions.size() == 1);
  CHECK(plan.classic_interaction_regions.front().classic_rect ==
      classic_blit(plan).source);
  CHECK(plan.classic_interaction_regions.front().window_rect ==
      classic_blit(plan).destination);

  // Blits and panels may share an edge, but no two visible surfaces may claim
  // interior pixels. This also guards input routing from ambiguous chrome.
  for (size_t first = 0; first < rendered_surfaces.size(); ++first) {
    for (size_t second = first + 1;
         second < rendered_surfaces.size(); ++second) {
      CHECK(!interiors_overlap(
          rendered_surfaces[first], rendered_surfaces[second]));
    }
  }

  if (!plan.adaptive_layout) {
    CHECK(panel_count(plan) == 0);
    return;
  }

  const auto& layout = *plan.adaptive_layout;
  CHECK(layout.window == plan.window);
  CHECK(plan.window.contains(layout.inner_bounds));
  CHECK(layout.inner_bounds.contains(layout.gameplay_slot));
  CHECK(layout.gameplay_slot.contains(layout.gameplay_viewport));
  CHECK(layout.gameplay_slot.contains(classic_blit(plan).destination));
  CHECK(layout.inner_bounds.contains(layout.party_rail));
  CHECK(layout.inner_bounds.contains(layout.action_bar));
  CHECK(panel_command(plan, ShellPanelKind::party_rail).destination ==
      layout.party_rail);
  CHECK(panel_command(plan, ShellPanelKind::action_bar).destination ==
      layout.action_bar);

  if (layout.layout_class == LayoutClass::wide) {
    CHECK(layout.details_panel.has_value());
    CHECK(layout.event_log.has_value());
    CHECK(!layout.drawer_tabs.has_value());
    CHECK(layout.inner_bounds.contains(*layout.details_panel));
    CHECK(layout.inner_bounds.contains(*layout.event_log));
    CHECK(panel_command(plan, ShellPanelKind::details).destination ==
        *layout.details_panel);
    CHECK(panel_command(plan, ShellPanelKind::event_log).destination ==
        *layout.event_log);
  } else {
    CHECK(!layout.details_panel.has_value());
    CHECK(!layout.event_log.has_value());
    CHECK(layout.drawer_tabs.has_value());
    CHECK(layout.inner_bounds.contains(*layout.drawer_tabs));
    CHECK(panel_command(plan, ShellPanelKind::drawer_tabs).destination ==
        *layout.drawer_tabs);
  }
}

LogicalPoint center(LogicalRect rect) {
  return {rect.x + rect.width / 2.0, rect.y + rect.height / 2.0};
}

void test_classic_exact_frame_mapping() {
  const auto exact = compute_adaptive_shell_plan({
      .mode = PresentationMode::classic,
      .screen = ScreenContext::title,
      .window_size = {800.0, 600.0},
      .backing_scale = 1.0,
  });

  CHECK(exact.route == AdaptiveShellRoute::classic_full_frame);
  CHECK(!exact.adaptive_layout.has_value());
  CHECK(!exact.gameplay_tile_coverage.is_valid());
  CHECK(exact.commands.size() == 2);
  CHECK(std::holds_alternative<ClearShellCommand>(exact.commands[0]));
  CHECK(std::holds_alternative<BlitClassicFrameCommand>(exact.commands[1]));
  CHECK(exact.classic_interaction_regions.size() == 1);
  const auto& blit = classic_blit(exact);
  CHECK(blit.source == kClassicFrameRect);
  CHECK(blit.destination == kClassicFrameRect);
  CHECK(blit.backing_destination == (PhysicalRect{0, 0, 800, 600}));
  CHECK(blit.sampling == ClassicSampling::nearest);
  CHECK(exact.window_to_classic({123.25, 456.5}) ==
      (LogicalPoint{123.25, 456.5}));
  CHECK(exact.classic_to_window({123.25, 456.5}) ==
      (LogicalPoint{123.25, 456.5}));
  CHECK(!exact.window_to_classic({800.0, 300.0}));
  CHECK(!exact.classic_to_window({400.0, 600.0}));
  verify_command_geometry(exact);

  const auto letterboxed = compute_adaptive_shell_plan({
      .mode = PresentationMode::classic,
      .screen = ScreenContext::combat,
      .window_size = {1440.0, 900.0},
      .backing_scale = 2.0,
  });
  const auto& letterbox_blit = classic_blit(letterboxed);
  CHECK(letterbox_blit.source == kClassicFrameRect);
  CHECK(letterbox_blit.destination ==
      (LogicalRect{120.0, 0.0, 1200.0, 900.0}));
  CHECK(letterbox_blit.backing_destination ==
      (PhysicalRect{240, 0, 2400, 1800}));
  CHECK(!letterboxed.window_to_classic({119.999, 450.0}));
  CHECK(!letterboxed.window_to_classic({1320.0, 450.0}));
  CHECK(approximately_equal(
      *letterboxed.window_to_classic({720.0, 450.0}),
      {400.0, 300.0}));
  verify_command_geometry(letterboxed);

  // Classic keeps its complete source mapping even when displayed below the
  // Remastered shell's minimum logical window size.
  const auto scaled_down = compute_adaptive_shell_plan({
      .mode = PresentationMode::classic,
      .window_size = {640.0, 480.0},
  });
  CHECK(classic_blit(scaled_down).source == kClassicFrameRect);
  CHECK(classic_blit(scaled_down).destination ==
      (LogicalRect{0.0, 0.0, 640.0, 480.0}));
  verify_command_geometry(scaled_down);
}

void verify_adaptive_geometry(
    LogicalSize window,
    double backing_scale,
    LayoutClass expected_class) {
  const auto plan = compute_adaptive_shell_plan({
      .mode = PresentationMode::remastered,
      .screen = ScreenContext::exploration,
      .window_size = window,
      .backing_scale = backing_scale,
      .semantic_controls_ready = true,
  });
  CHECK(plan.route == AdaptiveShellRoute::remastered_adaptive);
  CHECK(plan.adaptive_layout.has_value());
  CHECK(plan.adaptive_layout->layout_class == expected_class);
  CHECK(plan.adaptive_layout->backing_scale == backing_scale);
  CHECK(plan.gameplay_tile_coverage == kClassicGameplayTileCoverage);
  CHECK(plan.adaptive_layout->visible_tiles == kClassicGameplayTileCoverage);
  CHECK(plan.adaptive_layout->gameplay_content_size ==
      (LogicalSize{kClassicGameplayCrop.width, kClassicGameplayCrop.height}));
  CHECK(plan.classic_interaction_regions.size() == 1);

  const auto& blit = classic_blit(plan);
  CHECK(blit.source == kClassicGameplayCrop);
  CHECK(blit.destination == plan.adaptive_layout->gameplay_viewport);
  CHECK(blit.backing_destination ==
      BackingTransform(backing_scale).to_physical(blit.destination));
  CHECK(approximately_equal(
      blit.destination.width / blit.destination.height,
      kClassicGameplayCrop.width / kClassicGameplayCrop.height));
  CHECK(approximately_equal(
      kClassicGameplayCrop.width / kClassicTileExtent,
      static_cast<double>(plan.gameplay_tile_coverage.columns)));
  CHECK(approximately_equal(
      kClassicGameplayCrop.height / kClassicTileExtent,
      static_cast<double>(plan.gameplay_tile_coverage.rows)));

  CHECK(has_panel(plan, ShellPanelKind::party_rail));
  CHECK(has_panel(plan, ShellPanelKind::action_bar));
  if (expected_class == LayoutClass::wide) {
    CHECK(panel_count(plan) == 4);
    CHECK(has_panel(plan, ShellPanelKind::details));
    CHECK(has_panel(plan, ShellPanelKind::event_log));
    CHECK(!has_panel(plan, ShellPanelKind::drawer_tabs));
  } else {
    CHECK(panel_count(plan) == 3);
    CHECK(!has_panel(plan, ShellPanelKind::details));
    CHECK(!has_panel(plan, ShellPanelKind::event_log));
    CHECK(has_panel(plan, ShellPanelKind::drawer_tabs));
  }

  const std::array classic_samples{
      LogicalPoint{0.0, 20.0},
      LogicalPoint{32.0, 52.0},
      LogicalPoint{240.0, 228.0},
      LogicalPoint{479.999, 435.999},
  };
  for (const auto sample : classic_samples) {
    const auto window_point = plan.classic_to_window(sample);
    CHECK(window_point.has_value());
    const auto round_trip = plan.window_to_classic(*window_point);
    CHECK(round_trip.has_value());
    CHECK(approximately_equal(*round_trip, sample));
  }

  // Classic input is accepted only over the actual crop. Shell chrome is not
  // allowed to dispatch an accidental coordinate into the legacy event loop.
  CHECK(!plan.classic_to_window({480.0, 100.0}));
  CHECK(!plan.classic_to_window({100.0, 19.999}));
  CHECK(!plan.classic_to_window({100.0, 436.0}));
  CHECK(!plan.classic_to_window({700.0, 500.0}));
  CHECK(!plan.window_to_classic(center(plan.adaptive_layout->party_rail)));
  CHECK(!plan.window_to_classic(center(plan.adaptive_layout->action_bar)));
  verify_command_geometry(plan);
}

void test_adaptive_sizes_and_breakpoint() {
  verify_adaptive_geometry({1024.0, 768.0}, 1.0, LayoutClass::compact);
  verify_adaptive_geometry({1359.0, 900.0}, 2.0, LayoutClass::compact);
  verify_adaptive_geometry({1360.0, 768.0}, 1.0, LayoutClass::wide);
  verify_adaptive_geometry({1440.0, 900.0}, 2.0, LayoutClass::wide);
  verify_adaptive_geometry({1920.0, 1080.0}, 1.0, LayoutClass::wide);
  verify_adaptive_geometry({3440.0, 1440.0}, 2.0, LayoutClass::wide);
}

void test_screen_fallback_policy() {
  struct ExpectedPolicy {
    ScreenContext screen;
    bool fallback;
  };
  constexpr std::array policies{
      ExpectedPolicy{ScreenContext::title, true},
      ExpectedPolicy{ScreenContext::party_selection, true},
      ExpectedPolicy{ScreenContext::party_creation, true},
      ExpectedPolicy{ScreenContext::exploration, false},
      ExpectedPolicy{ScreenContext::dungeon, false},
      ExpectedPolicy{ScreenContext::combat, false},
      ExpectedPolicy{ScreenContext::inventory, true},
      ExpectedPolicy{ScreenContext::shop, true},
      ExpectedPolicy{ScreenContext::encounter, true},
      ExpectedPolicy{ScreenContext::ending, true},
  };

  for (const auto policy : policies) {
    CHECK(legacy_screen_requires_full_frame_fallback(policy.screen) ==
        policy.fallback);
    const auto plan = compute_adaptive_shell_plan({
        .mode = PresentationMode::remastered,
        .screen = policy.screen,
        .window_size = {1440.0, 900.0},
        .backing_scale = 2.0,
        .semantic_controls_ready = true,
    });
    if (policy.fallback) {
      CHECK(plan.route ==
          AdaptiveShellRoute::remastered_legacy_fallback);
      CHECK(!plan.adaptive_layout.has_value());
      CHECK(!plan.gameplay_tile_coverage.is_valid());
      CHECK(panel_count(plan) == 0);
      CHECK(plan.commands.size() == 2);
      CHECK(classic_blit(plan).source == kClassicFrameRect);
      CHECK(classic_blit(plan).destination ==
          (LogicalRect{120.0, 0.0, 1200.0, 900.0}));
      CHECK(approximately_equal(
          *plan.window_to_classic({720.0, 450.0}),
          {400.0, 300.0}));
    } else {
      CHECK(plan.route == AdaptiveShellRoute::remastered_adaptive);
      CHECK(plan.adaptive_layout.has_value());
      CHECK(classic_blit(plan).source == kClassicGameplayCrop);
      CHECK(panel_count(plan) == 4);
    }
    verify_command_geometry(plan);
  }

  CHECK(legacy_screen_requires_full_frame_fallback(
      static_cast<ScreenContext>(255)));
}

void test_validation_and_nonfinite_input() {
  bool rejected_small_remastered_window = false;
  try {
    (void)compute_adaptive_shell_plan({
        .mode = PresentationMode::remastered,
        .screen = ScreenContext::combat,
        .window_size = {1023.0, 768.0},
    });
  } catch (const std::invalid_argument&) {
    rejected_small_remastered_window = true;
  }
  CHECK(rejected_small_remastered_window);

  bool rejected_invalid_scale = false;
  try {
    (void)compute_adaptive_shell_plan({
        .mode = PresentationMode::classic,
        .window_size = {800.0, 600.0},
        .backing_scale = 0.0,
    });
  } catch (const std::invalid_argument&) {
    rejected_invalid_scale = true;
  }
  CHECK(rejected_invalid_scale);

  bool rejected_invalid_mode = false;
  try {
    (void)compute_adaptive_shell_plan({
        .mode = static_cast<PresentationMode>(255),
        .window_size = {800.0, 600.0},
    });
  } catch (const std::invalid_argument&) {
    rejected_invalid_mode = true;
  }
  CHECK(rejected_invalid_mode);

  const auto plan = compute_adaptive_shell_plan({
      .mode = PresentationMode::remastered,
      .screen = ScreenContext::combat,
      .window_size = {1440.0, 900.0},
      .semantic_controls_ready = true,
  });
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(!plan.window_to_classic({nan, 0.0}));
  CHECK(!plan.classic_to_window({0.0, nan}));
}

void test_compatibility_shell_preserves_legacy_controls() {
  constexpr std::array sizes{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1359.0, 900.0},
      LogicalSize{1360.0, 768.0},
      LogicalSize{1440.0, 900.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto screen : {
           ScreenContext::exploration,
           ScreenContext::dungeon,
           ScreenContext::combat,
       }) {
    for (size_t index = 0; index < sizes.size(); ++index) {
      const double backing_scale = (index % 2 == 0) ? 1.0 : 2.0;
      const auto plan = compute_adaptive_shell_plan({
          .mode = PresentationMode::remastered,
          .screen = screen,
          .window_size = sizes[index],
          .backing_scale = backing_scale,
      });
      CHECK(plan.route ==
          AdaptiveShellRoute::remastered_compatibility_shell);
      CHECK(plan.adaptive_layout.has_value());
      CHECK(classic_blit(plan).source == kClassicFrameRect);
      CHECK(classic_blit(plan).destination ==
          aspect_fit(kClassicFrameSize, plan.adaptive_layout->gameplay_slot));
      CHECK(panel_count(plan) ==
          (plan.adaptive_layout->layout_class == LayoutClass::wide ? 4 : 3));
      CHECK(approximately_equal(
          *plan.window_to_classic(center(classic_blit(plan).destination)),
          {400.0, 300.0}));
      CHECK(plan.classic_to_window({700.0, 500.0}).has_value());
      CHECK(!plan.window_to_classic(center(plan.adaptive_layout->party_rail)));
      verify_command_geometry(plan);
    }
  }
}

void test_runtime_modal_override() {
  const auto plan = compute_adaptive_shell_plan({
      .mode = PresentationMode::remastered,
      .screen = ScreenContext::combat,
      .window_size = {1440.0, 900.0},
      .backing_scale = 2.0,
      .legacy_full_frame_required = true,
      .semantic_controls_ready = true,
  });
  CHECK(plan.route == AdaptiveShellRoute::remastered_legacy_fallback);
  CHECK(!plan.adaptive_layout.has_value());
  CHECK(classic_blit(plan).source == kClassicFrameRect);
  CHECK(panel_count(plan) == 0);
  verify_command_geometry(plan);
}

void test_edge_stable_full_frame_fit() {
  const LogicalRect persisted_window{0.0, 0.0, 1600.0, 868.0};
  const auto persisted_fit = aspect_fit(kClassicFrameSize, persisted_window);
  CHECK(persisted_window.contains(persisted_fit));
  CHECK(persisted_fit.y == 0.0);
  CHECK(persisted_fit.height == 868.0);
  CHECK(approximately_equal(
      persisted_fit.width / kClassicFrameSize.width,
      persisted_fit.height / kClassicFrameSize.height));

  // This is the complete persisted-Remastered startup route: a title/full
  // frame plan is immediately handed to the strict input mapper. It must not
  // throw before the first frame is presented.
  const auto startup_plan = compute_adaptive_shell_plan({
      .mode = PresentationMode::remastered,
      .screen = ScreenContext::title,
      .window_size = {persisted_window.width, persisted_window.height},
      .backing_scale = 2.0,
  });
  CHECK(startup_plan.route ==
      AdaptiveShellRoute::remastered_legacy_fallback);
  CHECK(startup_plan.classic_interaction_regions.size() == 1);
  const auto& startup_interaction =
      startup_plan.classic_interaction_regions.front();
  CHECK(startup_plan.window.contains(startup_interaction.window_rect));
  const RemasteredInputMapper startup_mapper(
      startup_plan.window,
      {
          .destination = startup_interaction.window_rect,
          .source_crop = startup_interaction.classic_rect,
      },
      {},
      ShellRegionId{100U},
      startup_plan.backing_scale);
  CHECK(std::holds_alternative<LegacyPointerTarget>(
      startup_mapper.map_window_point(center(startup_interaction.window_rect))));

  // Exercise integer window sizes that previously exposed limiting-axis
  // roundoff, along with common reference, Retina, and ultrawide extents.
  constexpr std::array windows{
      LogicalSize{1024.0, 768.0},
      LogicalSize{1027.0, 770.0},
      LogicalSize{1280.0, 800.0},
      LogicalSize{1366.0, 768.0},
      LogicalSize{1440.0, 900.0},
      LogicalSize{1600.0, 868.0},
      LogicalSize{1680.0, 1050.0},
      LogicalSize{1920.0, 1080.0},
      LogicalSize{2560.0, 1440.0},
      LogicalSize{3440.0, 1440.0},
  };
  for (const auto window_size : windows) {
    const LogicalRect slot{0.0, 0.0, window_size.width, window_size.height};
    const auto fit = aspect_fit(kClassicFrameSize, slot);
    CHECK(slot.contains(fit));
    CHECK(approximately_equal(
        fit.width / kClassicFrameSize.width,
        fit.height / kClassicFrameSize.height));

    const auto plan = compute_adaptive_shell_plan({
        .mode = PresentationMode::remastered,
        .screen = ScreenContext::title,
        .window_size = window_size,
    });
    const auto& interaction = plan.classic_interaction_regions.front();
    CHECK(plan.window.contains(interaction.window_rect));
    const RemasteredInputMapper mapper(
        plan.window,
        {
            .destination = interaction.window_rect,
            .source_crop = interaction.classic_rect,
        },
        {},
        ShellRegionId{100U});
    CHECK(std::holds_alternative<LegacyPointerTarget>(
        mapper.map_window_point(center(interaction.window_rect))));
  }
}

} // namespace

int main() {
  try {
    test_classic_exact_frame_mapping();
    test_adaptive_sizes_and_breakpoint();
    test_screen_fallback_policy();
    test_validation_and_nonfinite_input();
    test_compatibility_shell_preserves_legacy_controls();
    test_runtime_modal_override();
    test_edge_stable_full_frame_fit();
    std::cout << "AdaptiveShellTest passed (" << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "AdaptiveShellTest failed after " << checks_run
              << " checks: " << e.what() << '\n';
    return 1;
  }
}
