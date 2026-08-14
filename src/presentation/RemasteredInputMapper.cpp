#include "RemasteredInputMapper.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace realmz::presentation {

namespace {

[[nodiscard]] bool is_positive_rect(const LogicalRect& rect) noexcept {
  return rect.is_finite_and_nonnegative() &&
      (rect.width > 0.0) && (rect.height > 0.0);
}

[[nodiscard]] bool approximately_equal(double lhs, double rhs) noexcept {
  constexpr double kTolerance = 1e-9;
  return std::abs(lhs - rhs) <=
      kTolerance * std::max({1.0, std::abs(lhs), std::abs(rhs)});
}

[[nodiscard]] bool is_finite(LogicalPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

} // namespace

RemasteredInputMapper::RemasteredInputMapper(
    LogicalRect window_bounds,
    ClassicFramePlacement classic_frame,
    std::span<const ShellHitRegion> shell_regions,
    ShellRegionId fallback_shell_region,
    double backing_scale)
    : window_bounds_(window_bounds),
      classic_frame_(classic_frame),
      shell_regions_(shell_regions.begin(), shell_regions.end()),
      fallback_shell_region_(fallback_shell_region),
      backing_transform_(backing_scale) {
  if (!is_positive_rect(this->window_bounds_)) {
    throw std::invalid_argument("window bounds must be finite and positive");
  }
  if (!is_positive_rect(this->classic_frame_.destination) ||
      !this->window_bounds_.contains(this->classic_frame_.destination)) {
    throw std::invalid_argument(
        "Classic frame destination must be positive and inside the window");
  }
  if (!is_positive_rect(this->classic_frame_.source_crop) ||
      !LogicalRect{0.0, 0.0, kClassicCanvasSize.width,
          kClassicCanvasSize.height}.contains(this->classic_frame_.source_crop)) {
    throw std::invalid_argument(
        "Classic source crop must be positive and inside 800x600");
  }

  const double scale_x = this->classic_frame_.destination.width /
      this->classic_frame_.source_crop.width;
  const double scale_y = this->classic_frame_.destination.height /
      this->classic_frame_.source_crop.height;
  if (!approximately_equal(scale_x, scale_y)) {
    throw std::invalid_argument(
        "Classic frame destination must uniformly scale its source crop");
  }
  this->classic_scale_ = (scale_x + scale_y) / 2.0;

  if (!this->fallback_shell_region_.is_valid()) {
    throw std::invalid_argument("fallback shell region must be valid");
  }
  for (const auto& region : this->shell_regions_) {
    if (!region.id.is_valid()) {
      throw std::invalid_argument("shell region identifier must be valid");
    }
    if (!is_positive_rect(region.bounds) ||
        !this->window_bounds_.contains(region.bounds)) {
      throw std::invalid_argument(
          "shell hit regions must be positive and inside the window");
    }
  }
}

RemasteredPointerTarget RemasteredInputMapper::map_window_point(
    LogicalPoint window_point) const noexcept {
  if (!this->window_bounds_.contains(window_point)) {
    return OutsideWindowTarget{window_point};
  }

  const auto& destination = this->classic_frame_.destination;
  if (destination.contains(window_point)) {
    const auto& crop = this->classic_frame_.source_crop;
    LogicalPoint classic_point{
        crop.x + (window_point.x - destination.x) / this->classic_scale_,
        crop.y + (window_point.y - destination.y) / this->classic_scale_,
    };

    // destination is half-open, so the mathematical result is also half-open.
    // Clamp only against a possible floating-point round-up at the far edge.
    classic_point.x = std::clamp(
        classic_point.x, crop.x, std::nextafter(crop.right(), crop.x));
    classic_point.y = std::clamp(
        classic_point.y, crop.y, std::nextafter(crop.bottom(), crop.y));
    return LegacyPointerTarget{classic_point};
  }

  for (const auto& region : this->shell_regions_) {
    if (region.bounds.contains(window_point)) {
      return ShellPointerTarget{
          .region = region.id,
          .window_point = window_point,
          .local_point = {
              window_point.x - region.bounds.x,
              window_point.y - region.bounds.y,
          },
      };
    }
  }

  return ShellPointerTarget{
      .region = this->fallback_shell_region_,
      .window_point = window_point,
      .local_point = {
          window_point.x - this->window_bounds_.x,
          window_point.y - this->window_bounds_.y,
      },
  };
}

std::optional<RemasteredPointerCapture>
RemasteredInputMapper::begin_pointer_capture(
    LogicalPoint window_point) const noexcept {
  const auto target = this->map_window_point(window_point);
  if (std::holds_alternative<OutsideWindowTarget>(target)) {
    return std::nullopt;
  }
  if (std::holds_alternative<LegacyPointerTarget>(target)) {
    return LegacyPointerCapture{.captured_frame = this->classic_frame_};
  }

  const auto& shell_target = std::get<ShellPointerTarget>(target);
  for (const auto& region : this->shell_regions_) {
    if ((region.id == shell_target.region) &&
        region.bounds.contains(window_point)) {
      return ShellPointerCapture{
          .region = region.id,
          .captured_bounds = region.bounds,
      };
    }
  }
  return ShellPointerCapture{
      .region = this->fallback_shell_region_,
      .captured_bounds = this->window_bounds_,
  };
}

RemasteredPointerTarget RemasteredInputMapper::map_captured_window_point(
    LogicalPoint window_point,
    const RemasteredPointerCapture& capture) const noexcept {
  if (!is_finite(window_point)) {
    return OutsideWindowTarget{window_point};
  }

  if (std::holds_alternative<LegacyPointerCapture>(capture)) {
    const auto& captured = std::get<LegacyPointerCapture>(capture);
    const auto& destination = captured.captured_frame.destination;
    const auto& crop = captured.captured_frame.source_crop;
    if (!is_positive_rect(destination) || !is_positive_rect(crop)) {
      return OutsideWindowTarget{window_point};
    }
    const double captured_scale = destination.width / crop.width;
    if (!std::isfinite(captured_scale) || (captured_scale <= 0.0) ||
        !approximately_equal(
            captured_scale, destination.height / crop.height)) {
      return OutsideWindowTarget{window_point};
    }
    return LegacyPointerTarget{{
        crop.x + (window_point.x - destination.x) / captured_scale,
        crop.y + (window_point.y - destination.y) / captured_scale,
    }};
  }

  const auto& shell_capture = std::get<ShellPointerCapture>(capture);
  if (!shell_capture.region.is_valid() ||
      !is_positive_rect(shell_capture.captured_bounds)) {
    return OutsideWindowTarget{window_point};
  }
  return ShellPointerTarget{
      .region = shell_capture.region,
      .window_point = window_point,
      .local_point = {
          window_point.x - shell_capture.captured_bounds.x,
          window_point.y - shell_capture.captured_bounds.y,
      },
  };
}

RemasteredPointerTarget RemasteredInputMapper::map_backing_pixel(
    PhysicalPoint backing_pixel) const noexcept {
  return this->map_window_point(
      this->backing_transform_.to_logical(backing_pixel));
}

std::optional<LogicalPoint> RemasteredInputMapper::classic_to_window_point(
    LogicalPoint classic_point) const noexcept {
  const auto& crop = this->classic_frame_.source_crop;
  if (!crop.contains(classic_point)) {
    return std::nullopt;
  }
  const auto& destination = this->classic_frame_.destination;
  return LogicalPoint{
      destination.x + (classic_point.x - crop.x) * this->classic_scale_,
      destination.y + (classic_point.y - crop.y) * this->classic_scale_,
  };
}

std::optional<LogicalRect> RemasteredInputMapper::classic_to_window_rect(
    LogicalRect classic_rect) const noexcept {
  if (!classic_rect.is_finite_and_nonnegative() ||
      !this->classic_frame_.source_crop.contains(classic_rect)) {
    return std::nullopt;
  }

  const auto& crop = this->classic_frame_.source_crop;
  const auto& destination = this->classic_frame_.destination;
  return LogicalRect{
      destination.x + (classic_rect.x - crop.x) * this->classic_scale_,
      destination.y + (classic_rect.y - crop.y) * this->classic_scale_,
      classic_rect.width * this->classic_scale_,
      classic_rect.height * this->classic_scale_,
  };
}

PhysicalPoint RemasteredInputMapper::window_point_to_backing_pixel(
    LogicalPoint window_point) const noexcept {
  return this->backing_transform_.to_physical(window_point);
}

PhysicalRect RemasteredInputMapper::window_rect_to_backing_pixels(
    LogicalRect window_rect) const noexcept {
  return this->backing_transform_.to_physical(window_rect);
}

std::optional<PhysicalRect>
RemasteredInputMapper::classic_rect_to_backing_pixels(
    LogicalRect classic_rect) const noexcept {
  const auto window_rect = this->classic_to_window_rect(classic_rect);
  if (!window_rect) {
    return std::nullopt;
  }
  return this->window_rect_to_backing_pixels(*window_rect);
}

} // namespace realmz::presentation
