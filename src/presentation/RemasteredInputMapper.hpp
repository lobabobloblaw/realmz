#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "Geometry.hpp"

namespace realmz::presentation {

// The legacy engine's coordinate extent is an invariant, even when only a
// crop of its framebuffer is embedded in the adaptive shell.
inline constexpr LogicalSize kClassicCanvasSize{800.0, 600.0};

// The adaptive shell owns the meaning of these values. Keeping the identifier
// opaque here prevents pointer mapping from depending on a particular layout
// model or screen router.
struct ShellRegionId {
  std::uint32_t value = 0;

  bool operator==(const ShellRegionId&) const = default;

  [[nodiscard]] bool is_valid() const noexcept {
    return this->value != 0;
  }
};

struct ShellHitRegion {
  ShellRegionId id;
  LogicalRect bounds;

  bool operator==(const ShellHitRegion&) const = default;
};

struct ClassicFramePlacement {
  // Location in logical SDL window points. This is deliberately independent
  // of the backing-pixel scale.
  LogicalRect destination;

  // Portion of the fixed 800x600 Classic framebuffer displayed at destination.
  LogicalRect source_crop{0.0, 0.0, 800.0, 600.0};

  bool operator==(const ClassicFramePlacement&) const = default;
};

struct LegacyPointerTarget {
  // Continuous Classic-canvas coordinates. The legacy event bridge owns the
  // final integer conversion, matching its existing positive-coordinate rule.
  LogicalPoint classic_point;

  bool operator==(const LegacyPointerTarget&) const = default;
};

struct ShellPointerTarget {
  ShellRegionId region;
  LogicalPoint window_point;
  LogicalPoint local_point;

  bool operator==(const ShellPointerTarget&) const = default;
};

struct OutsideWindowTarget {
  LogicalPoint window_point;

  bool operator==(const OutsideWindowTarget&) const = default;
};

using RemasteredPointerTarget = std::variant<
    LegacyPointerTarget,
    ShellPointerTarget,
    OutsideWindowTarget>;

// Pointer presses can start a drag in one presentation path and finish after
// the pointer has left that path's visible bounds. The SDL host must retain one
// of these tokens per pressed pointer/button and use map_captured_window_point
// until the matching release. Without capture, a legacy mouse-up outside the
// embedded Classic frame would be silently rerouted to shell chrome.
struct LegacyPointerCapture {
  // Capture the exact placement chosen at pointer-down. A modal/window-stack
  // recomposite can replace the live mapper before mouse-up; using the new
  // placement would deliver the release to a different Classic coordinate.
  ClassicFramePlacement captured_frame;

  bool operator==(const LegacyPointerCapture&) const = default;
};

struct ShellPointerCapture {
  ShellRegionId region;
  LogicalRect captured_bounds;

  bool operator==(const ShellPointerCapture&) const = default;
};

using RemasteredPointerCapture = std::variant<
    LegacyPointerCapture,
    ShellPointerCapture>;

// Pure coordinate boundary for Remastered-mode pointer and native-UI anchors.
//
// Classic mode intentionally does not pass through this class: its existing
// SDL logical-presentation conversion remains the pixel-identical 800x600
// path. The presentation-mode owner chooses this mapper only for Remastered
// shell events.
//
// Shell hit regions are ordered front-to-back. A designated Classic frame has
// priority over every shell region, so a legacy mouse event can only originate
// inside that explicit rectangle. Every other in-window point resolves to the
// first matching shell region or to fallback_shell_region.
class RemasteredInputMapper {
public:
  RemasteredInputMapper(
      LogicalRect window_bounds,
      ClassicFramePlacement classic_frame,
      std::span<const ShellHitRegion> shell_regions,
      ShellRegionId fallback_shell_region,
      // Pass SDL_GetWindowPixelDensity(), not the display/content scale.
      double backing_scale = 1.0);

  [[nodiscard]] const LogicalRect& window_bounds() const noexcept {
    return this->window_bounds_;
  }

  [[nodiscard]] const ClassicFramePlacement& classic_frame() const noexcept {
    return this->classic_frame_;
  }

  [[nodiscard]] double backing_scale() const noexcept {
    return this->backing_transform_.scale();
  }

  [[nodiscard]] RemasteredPointerTarget map_window_point(
      LogicalPoint window_point) const noexcept;

  // Returns no token for a press outside the window. Capture is deliberately
  // explicit instead of hidden mutable mapper state so hosts can track
  // multiple mouse buttons or pointer identifiers independently.
  [[nodiscard]] std::optional<RemasteredPointerCapture> begin_pointer_capture(
      LogicalPoint window_point) const noexcept;

  // Preserves the route selected at pointer-down even when the point moves
  // outside that route (or the window). Captured Classic coordinates are
  // intentionally allowed outside source_crop: legacy drag/release handlers
  // need the real out-of-bounds point to cancel a click correctly.
  [[nodiscard]] RemasteredPointerTarget map_captured_window_point(
      LogicalPoint window_point,
      const RemasteredPointerCapture& capture) const noexcept;

  // SDL mouse events are already expressed in window coordinates and should
  // use map_window_point(). This entry point is only for a source explicitly
  // expressed in high-density backing pixels.
  [[nodiscard]] RemasteredPointerTarget map_backing_pixel(
      PhysicalPoint backing_pixel) const noexcept;

  // Reverse transforms for native popup and text-input placement. Native APIs
  // consume window points; backing-pixel conversion is separate and explicit.
  [[nodiscard]] std::optional<LogicalPoint> classic_to_window_point(
      LogicalPoint classic_point) const noexcept;

  [[nodiscard]] std::optional<LogicalRect> classic_to_window_rect(
      LogicalRect classic_rect) const noexcept;

  [[nodiscard]] PhysicalPoint window_point_to_backing_pixel(
      LogicalPoint window_point) const noexcept;

  [[nodiscard]] PhysicalRect window_rect_to_backing_pixels(
      LogicalRect window_rect) const noexcept;

  [[nodiscard]] std::optional<PhysicalRect> classic_rect_to_backing_pixels(
      LogicalRect classic_rect) const noexcept;

private:
  LogicalRect window_bounds_;
  ClassicFramePlacement classic_frame_;
  std::vector<ShellHitRegion> shell_regions_;
  ShellRegionId fallback_shell_region_;
  BackingTransform backing_transform_;
  double classic_scale_ = 1.0;
};

} // namespace realmz::presentation
