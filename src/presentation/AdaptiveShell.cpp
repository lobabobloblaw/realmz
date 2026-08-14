#include "AdaptiveShell.hpp"

#include <cmath>
#include <stdexcept>

namespace realmz::presentation {

namespace {

[[nodiscard]] LogicalRect full_window(LogicalSize size) noexcept {
  return {0.0, 0.0, size.width, size.height};
}

void append_panel(
    AdaptiveShellPlan& plan,
    BackingTransform backing_transform,
    ShellPanelKind kind,
    LogicalRect destination) {
  plan.commands.emplace_back(DrawShellPanelCommand{
      .panel = kind,
      .destination = destination,
      .backing_destination = backing_transform.to_physical(destination),
  });
}

void append_classic_blit(
    AdaptiveShellPlan& plan,
    BackingTransform backing_transform,
    LogicalRect source,
    LogicalRect destination) {
  plan.commands.emplace_back(BlitClassicFrameCommand{
      .source = source,
      .destination = destination,
      .backing_destination = backing_transform.to_physical(destination),
      .sampling = ClassicSampling::nearest,
  });
  plan.classic_interaction_regions.emplace_back(ClassicInteractionRegion{
      .classic_rect = source,
      .window_rect = destination,
  });
}

[[nodiscard]] std::optional<LogicalPoint> map_point(
    LogicalPoint point,
    LogicalRect from,
    LogicalRect to) noexcept {
  if (!from.contains(point) || (from.width <= 0.0) || (from.height <= 0.0)) {
    return std::nullopt;
  }
  return LogicalPoint{
      to.x + ((point.x - from.x) / from.width) * to.width,
      to.y + ((point.y - from.y) / from.height) * to.height,
  };
}

void validate_mode(PresentationMode mode) {
  switch (mode) {
    case PresentationMode::classic:
    case PresentationMode::remastered:
      return;
  }
  throw std::invalid_argument("invalid presentation mode");
}

} // namespace

bool legacy_screen_requires_full_frame_fallback(ScreenContext screen) noexcept {
  switch (screen) {
    case ScreenContext::exploration:
    case ScreenContext::dungeon:
    case ScreenContext::combat:
      return false;
    case ScreenContext::title:
    case ScreenContext::party_selection:
    case ScreenContext::party_creation:
    case ScreenContext::inventory:
    case ScreenContext::shop:
    case ScreenContext::encounter:
    case ScreenContext::ending:
      return true;
  }
  // An unknown engine screen is safer as an intact legacy composition than as
  // a partially interactive adaptive view.
  return true;
}

std::optional<LogicalPoint> AdaptiveShellPlan::window_to_classic(
    LogicalPoint point) const noexcept {
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return std::nullopt;
  }
  for (const auto& region : this->classic_interaction_regions) {
    if (const auto mapped = map_point(
            point,
            region.window_rect,
            region.classic_rect)) {
      return mapped;
    }
  }
  return std::nullopt;
}

std::optional<LogicalPoint> AdaptiveShellPlan::classic_to_window(
    LogicalPoint point) const noexcept {
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return std::nullopt;
  }
  for (const auto& region : this->classic_interaction_regions) {
    if (const auto mapped = map_point(
            point,
            region.classic_rect,
            region.window_rect)) {
      return mapped;
    }
  }
  return std::nullopt;
}

AdaptiveShellPlan compute_adaptive_shell_plan(
    const AdaptiveShellRequest& request,
    const ResponsiveLayoutConfig& config) {
  validate_mode(request.mode);
  if (!request.window_size.is_finite_and_positive()) {
    throw std::invalid_argument("window size must be finite and positive");
  }
  const BackingTransform backing_transform(request.backing_scale);
  const LogicalRect window = full_window(request.window_size);

  AdaptiveShellPlan plan;
  plan.mode = request.mode;
  plan.screen = request.screen;
  plan.window = window;
  plan.backing_scale = request.backing_scale;
  plan.commands.emplace_back(ClearShellCommand{
      .destination = window,
      .backing_destination = backing_transform.to_physical(window),
  });

  if (request.mode == PresentationMode::classic) {
    plan.route = AdaptiveShellRoute::classic_full_frame;
    append_classic_blit(
        plan,
        backing_transform,
        kClassicFrameRect,
        aspect_fit(kClassicFrameSize, window));
    return plan;
  }

  // Besides producing the adaptive geometry, this centralizes validation of
  // the Remastered minimum window, layout config, backing scale, and invariant
  // tile coverage in ResponsiveLayout.
  const PresentationLayout layout = compute_responsive_layout({
      .window_size = request.window_size,
      .gameplay_content_size = {
          kClassicGameplayCrop.width,
          kClassicGameplayCrop.height,
      },
      .visible_tiles = kClassicGameplayTileCoverage,
      .backing_scale = request.backing_scale,
  }, config);

  if (request.legacy_full_frame_required ||
      legacy_screen_requires_full_frame_fallback(request.screen)) {
    plan.route = AdaptiveShellRoute::remastered_legacy_fallback;
    append_classic_blit(
        plan,
        backing_transform,
        kClassicFrameRect,
        aspect_fit(kClassicFrameSize, window));
    return plan;
  }

  plan.route = request.semantic_controls_ready
      ? AdaptiveShellRoute::remastered_adaptive
      : AdaptiveShellRoute::remastered_compatibility_shell;
  plan.adaptive_layout = layout;
  plan.gameplay_tile_coverage = kClassicGameplayTileCoverage;
  append_classic_blit(
      plan,
      backing_transform,
      request.semantic_controls_ready ? kClassicGameplayCrop : kClassicFrameRect,
      request.semantic_controls_ready
          ? layout.gameplay_viewport
          : aspect_fit(kClassicFrameSize, layout.gameplay_slot));
  append_panel(
      plan,
      backing_transform,
      ShellPanelKind::party_rail,
      layout.party_rail);
  append_panel(
      plan,
      backing_transform,
      ShellPanelKind::action_bar,
      layout.action_bar);

  if (layout.layout_class == LayoutClass::wide) {
    append_panel(
        plan,
        backing_transform,
        ShellPanelKind::details,
        *layout.details_panel);
    append_panel(
        plan,
        backing_transform,
        ShellPanelKind::event_log,
        *layout.event_log);
  } else {
    append_panel(
        plan,
        backing_transform,
        ShellPanelKind::drawer_tabs,
        *layout.drawer_tabs);
  }
  return plan;
}

} // namespace realmz::presentation
