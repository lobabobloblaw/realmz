#pragma once

#include <cstddef>
#include <optional>

#include "Geometry.hpp"

namespace realmz::presentation {

enum class LayoutClass {
  compact,
  wide,
};

struct TileCoverage {
  size_t columns = 0;
  size_t rows = 0;

  bool operator==(const TileCoverage&) const = default;

  [[nodiscard]] bool is_valid() const noexcept {
    return (this->columns > 0) && (this->rows > 0);
  }
};

struct ResponsiveLayoutConfig {
  LogicalSize minimum_window{1024.0, 768.0};
  double wide_breakpoint = 1360.0;
  double outer_margin = 24.0;
  double gap = 16.0;
  double bottom_minimum = 120.0;
  double bottom_maximum = 180.0;
  double wide_rail_minimum = 300.0;
  double wide_rail_maximum = 360.0;
  double compact_rail_minimum = 240.0;
  double compact_rail_maximum = 300.0;
};

struct LayoutRequest {
  LogicalSize window_size;
  LogicalSize gameplay_content_size;
  TileCoverage visible_tiles;
  double backing_scale = 1.0;
};

struct PresentationLayout {
  LayoutClass layout_class = LayoutClass::compact;
  LogicalRect window;
  LogicalRect inner_bounds;
  LogicalRect gameplay_slot;
  LogicalRect gameplay_viewport;
  LogicalRect party_rail;
  LogicalRect action_bar;
  std::optional<LogicalRect> details_panel;
  std::optional<LogicalRect> event_log;
  std::optional<LogicalRect> drawer_tabs;
  LogicalSize gameplay_content_size;
  TileCoverage visible_tiles;
  double backing_scale = 1.0;

  [[nodiscard]] bool uses_collapsed_panels() const noexcept {
    return this->layout_class == LayoutClass::compact;
  }

  [[nodiscard]] UniformContentTransform gameplay_transform() const {
    return UniformContentTransform(
        this->gameplay_content_size,
        this->gameplay_viewport);
  }

  [[nodiscard]] BackingTransform backing_transform() const {
    return BackingTransform(this->backing_scale);
  }
};

// Computes a layout in logical window points. The returned tile coverage is
// exactly the requested coverage at every window size; only the uniform scale
// of gameplay_viewport changes.
[[nodiscard]] PresentationLayout compute_responsive_layout(
    const LayoutRequest& request,
    const ResponsiveLayoutConfig& config = {});

} // namespace realmz::presentation
