#include "ResponsiveLayout.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace realmz::presentation {

namespace {

void validate_config(const ResponsiveLayoutConfig& config) {
  if (!config.minimum_window.is_finite_and_positive()) {
    throw std::invalid_argument("minimum window size must be finite and positive");
  }
  if (!std::isfinite(config.wide_breakpoint) ||
      (config.wide_breakpoint < config.minimum_window.width)) {
    throw std::invalid_argument(
        "wide breakpoint must be finite and at least the minimum width");
  }
  if (!std::isfinite(config.outer_margin) ||
      !std::isfinite(config.gap) ||
      (config.outer_margin < 0.0) || (config.gap < 0.0)) {
    throw std::invalid_argument("layout spacing must be finite and nonnegative");
  }
  if (!std::isfinite(config.bottom_minimum) ||
      !std::isfinite(config.bottom_maximum) ||
      (config.bottom_minimum <= 0.0) ||
      (config.bottom_maximum < config.bottom_minimum)) {
    throw std::invalid_argument("bottom panel bounds are invalid");
  }
  if (!std::isfinite(config.wide_rail_minimum) ||
      !std::isfinite(config.wide_rail_maximum) ||
      !std::isfinite(config.compact_rail_minimum) ||
      !std::isfinite(config.compact_rail_maximum) ||
      (config.wide_rail_minimum <= 0.0) ||
      (config.compact_rail_minimum <= 0.0) ||
      (config.wide_rail_maximum < config.wide_rail_minimum) ||
      (config.compact_rail_maximum < config.compact_rail_minimum)) {
    throw std::invalid_argument("party rail bounds are invalid");
  }
}

} // namespace

PresentationLayout compute_responsive_layout(
    const LayoutRequest& request,
    const ResponsiveLayoutConfig& config) {
  validate_config(config);
  if (!request.window_size.is_finite_and_positive()) {
    throw std::invalid_argument("window size must be finite and positive");
  }
  if ((request.window_size.width < config.minimum_window.width) ||
      (request.window_size.height < config.minimum_window.height)) {
    throw std::invalid_argument("window size is below the supported minimum");
  }
  if (!request.gameplay_content_size.is_finite_and_positive()) {
    throw std::invalid_argument("gameplay content size must be finite and positive");
  }
  if (!request.visible_tiles.is_valid()) {
    throw std::invalid_argument("visible tile coverage must be nonzero");
  }
  // Validate here even though the returned transform also validates it. This
  // keeps all request failures at the layout boundary.
  BackingTransform backing_transform(request.backing_scale);
  (void)backing_transform;

  const bool wide = request.window_size.width >= config.wide_breakpoint;
  const LayoutClass layout_class = wide ? LayoutClass::wide : LayoutClass::compact;
  const LogicalRect window{
      0.0,
      0.0,
      request.window_size.width,
      request.window_size.height,
  };
  const LogicalRect inner{
      config.outer_margin,
      config.outer_margin,
      request.window_size.width - 2.0 * config.outer_margin,
      request.window_size.height - 2.0 * config.outer_margin,
  };
  if ((inner.width <= 0.0) || (inner.height <= 0.0)) {
    throw std::invalid_argument("outer margin consumes the entire window");
  }

  const double bottom_fraction = wide ? 0.20 : 0.18;
  const double bottom_height = std::clamp(
      request.window_size.height * bottom_fraction,
      config.bottom_minimum,
      config.bottom_maximum);
  const double main_height = inner.height - config.gap - bottom_height;
  if (main_height <= 0.0) {
    throw std::invalid_argument("bottom panel and spacing consume the layout");
  }

  const double rail_width = wide
      ? std::clamp(
            request.window_size.width * 0.235,
            config.wide_rail_minimum,
            config.wide_rail_maximum)
      : std::clamp(
            request.window_size.width * 0.255,
            config.compact_rail_minimum,
            config.compact_rail_maximum);
  const double gameplay_slot_width = inner.width - config.gap - rail_width;
  if (gameplay_slot_width <= 0.0) {
    throw std::invalid_argument("party rail and spacing consume the layout");
  }

  const LogicalRect gameplay_slot{
      inner.x,
      inner.y,
      gameplay_slot_width,
      main_height,
  };
  const LogicalRect gameplay_viewport = aspect_fit(
      request.gameplay_content_size,
      gameplay_slot);
  const LogicalRect rail_area{
      gameplay_slot.right() + config.gap,
      inner.y,
      rail_width,
      main_height,
  };
  const double bottom_y = inner.y + main_height + config.gap;

  PresentationLayout result;
  result.layout_class = layout_class;
  result.window = window;
  result.inner_bounds = inner;
  result.gameplay_slot = gameplay_slot;
  result.gameplay_viewport = gameplay_viewport;
  result.gameplay_content_size = request.gameplay_content_size;
  result.visible_tiles = request.visible_tiles;
  result.backing_scale = request.backing_scale;

  if (wide) {
    constexpr double kMinimumPartyHeight = 240.0;
    constexpr double kMinimumDetailsHeight = 160.0;
    if (main_height <
        kMinimumPartyHeight + config.gap + kMinimumDetailsHeight) {
      throw std::invalid_argument(
          "wide layout is too short for party and details panels");
    }
    const double details_height = std::clamp(
        main_height * 0.36,
        kMinimumDetailsHeight,
        main_height - config.gap - kMinimumPartyHeight);
    result.party_rail = {
        rail_area.x,
        rail_area.y,
        rail_area.width,
        rail_area.height - config.gap - details_height,
    };
    result.details_panel = LogicalRect{
        rail_area.x,
        result.party_rail.bottom() + config.gap,
        rail_area.width,
        details_height,
    };
    result.action_bar = {
        gameplay_slot.x,
        bottom_y,
        gameplay_slot.width,
        bottom_height,
    };
    result.event_log = LogicalRect{
        rail_area.x,
        bottom_y,
        rail_area.width,
        bottom_height,
    };
  } else {
    result.party_rail = rail_area;
    const double drawer_width = std::clamp(inner.width * 0.26, 200.0, 260.0);
    const double action_width = inner.width - config.gap - drawer_width;
    if (action_width <= 0.0) {
      throw std::invalid_argument("drawer tabs consume the bottom panel");
    }
    result.action_bar = {
        inner.x,
        bottom_y,
        action_width,
        bottom_height,
    };
    result.drawer_tabs = LogicalRect{
        result.action_bar.right() + config.gap,
        bottom_y,
        drawer_width,
        bottom_height,
    };
  }

  return result;
}

} // namespace realmz::presentation
