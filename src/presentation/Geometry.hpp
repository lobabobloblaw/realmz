#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace realmz::presentation {

struct LogicalPoint {
  double x = 0.0;
  double y = 0.0;

  bool operator==(const LogicalPoint&) const = default;
};

struct LogicalSize {
  double width = 0.0;
  double height = 0.0;

  bool operator==(const LogicalSize&) const = default;

  [[nodiscard]] bool is_finite_and_positive() const noexcept {
    return std::isfinite(this->width) && std::isfinite(this->height) &&
        (this->width > 0.0) && (this->height > 0.0);
  }
};

struct LogicalRect {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;

  bool operator==(const LogicalRect&) const = default;

  [[nodiscard]] double right() const noexcept {
    return this->x + this->width;
  }

  [[nodiscard]] double bottom() const noexcept {
    return this->y + this->height;
  }

  [[nodiscard]] bool is_finite_and_nonnegative() const noexcept {
    return std::isfinite(this->x) && std::isfinite(this->y) &&
        std::isfinite(this->width) && std::isfinite(this->height) &&
        (this->width >= 0.0) && (this->height >= 0.0);
  }

  // Hit regions use half-open bounds. Adjacent widgets therefore cannot both
  // claim an event that lands exactly on their shared edge.
  [[nodiscard]] bool contains(LogicalPoint point) const noexcept {
    return (point.x >= this->x) && (point.y >= this->y) &&
        (point.x < this->right()) && (point.y < this->bottom());
  }

  [[nodiscard]] bool contains(LogicalRect rect) const noexcept {
    return (rect.x >= this->x) && (rect.y >= this->y) &&
        (rect.right() <= this->right()) &&
        (rect.bottom() <= this->bottom());
  }
};

struct PhysicalPoint {
  int32_t x = 0;
  int32_t y = 0;

  bool operator==(const PhysicalPoint&) const = default;
};

struct PhysicalRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  bool operator==(const PhysicalRect&) const = default;

  [[nodiscard]] int32_t right() const noexcept {
    return this->x + this->width;
  }

  [[nodiscard]] int32_t bottom() const noexcept {
    return this->y + this->height;
  }
};

// Converts between SDL-style logical window coordinates and backing pixels.
// The transform intentionally knows nothing about the game's content scale;
// that separate concern is handled by UniformContentTransform below.
class BackingTransform {
public:
  explicit BackingTransform(double backing_scale = 1.0)
      : backing_scale_(backing_scale) {
    if (!std::isfinite(backing_scale) || (backing_scale <= 0.0)) {
      throw std::invalid_argument("backing scale must be finite and positive");
    }
  }

  [[nodiscard]] double scale() const noexcept {
    return this->backing_scale_;
  }

  [[nodiscard]] PhysicalPoint to_physical(LogicalPoint point) const noexcept {
    return {
        static_cast<int32_t>(std::lround(point.x * this->backing_scale_)),
        static_cast<int32_t>(std::lround(point.y * this->backing_scale_)),
    };
  }

  // Rectangle edges are rounded independently. This keeps adjacent logical
  // rectangles adjacent in physical space at fractional backing scales.
  [[nodiscard]] PhysicalRect to_physical(LogicalRect rect) const noexcept {
    const int32_t left = static_cast<int32_t>(std::lround(rect.x * this->backing_scale_));
    const int32_t top = static_cast<int32_t>(std::lround(rect.y * this->backing_scale_));
    const int32_t right = static_cast<int32_t>(std::lround(rect.right() * this->backing_scale_));
    const int32_t bottom = static_cast<int32_t>(std::lround(rect.bottom() * this->backing_scale_));
    return {left, top, right - left, bottom - top};
  }

  [[nodiscard]] LogicalPoint to_logical(PhysicalPoint point) const noexcept {
    return {
        point.x / this->backing_scale_,
        point.y / this->backing_scale_,
    };
  }

private:
  double backing_scale_;
};

// Maps a fixed logical content extent into a uniformly-scaled window-space
// rectangle. It cannot expand the source extent, which is the core invariant
// that prevents a wider display from revealing more terrain.
class UniformContentTransform {
public:
  UniformContentTransform(LogicalSize content_size, LogicalRect viewport)
      : content_size_(content_size), viewport_(viewport) {
    if (!content_size.is_finite_and_positive()) {
      throw std::invalid_argument("content size must be finite and positive");
    }
    if (!viewport.is_finite_and_nonnegative() ||
        (viewport.width <= 0.0) || (viewport.height <= 0.0)) {
      throw std::invalid_argument("viewport must be finite and positive");
    }

    const double scale_x = viewport.width / content_size.width;
    const double scale_y = viewport.height / content_size.height;
    constexpr double kTolerance = 1e-9;
    if (std::abs(scale_x - scale_y) >
        kTolerance * std::max({1.0, std::abs(scale_x), std::abs(scale_y)})) {
      throw std::invalid_argument("content viewport must use uniform scaling");
    }
    this->scale_ = (scale_x + scale_y) / 2.0;
  }

  [[nodiscard]] const LogicalSize& content_size() const noexcept {
    return this->content_size_;
  }

  [[nodiscard]] const LogicalRect& viewport() const noexcept {
    return this->viewport_;
  }

  [[nodiscard]] double scale() const noexcept {
    return this->scale_;
  }

  [[nodiscard]] LogicalPoint content_to_window(LogicalPoint point) const noexcept {
    return {
        this->viewport_.x + point.x * this->scale_,
        this->viewport_.y + point.y * this->scale_,
    };
  }

  [[nodiscard]] LogicalRect content_to_window(LogicalRect rect) const noexcept {
    const auto origin = this->content_to_window(LogicalPoint{rect.x, rect.y});
    return {
        origin.x,
        origin.y,
        rect.width * this->scale_,
        rect.height * this->scale_,
    };
  }

  [[nodiscard]] std::optional<LogicalPoint> window_to_content(LogicalPoint point) const noexcept {
    if (!this->viewport_.contains(point)) {
      return std::nullopt;
    }
    return LogicalPoint{
        (point.x - this->viewport_.x) / this->scale_,
        (point.y - this->viewport_.y) / this->scale_,
    };
  }

private:
  LogicalSize content_size_;
  LogicalRect viewport_;
  double scale_ = 1.0;
};

[[nodiscard]] inline LogicalRect aspect_fit(LogicalSize content_size, LogicalRect slot) {
  if (!content_size.is_finite_and_positive()) {
    throw std::invalid_argument("content size must be finite and positive");
  }
  if (!slot.is_finite_and_nonnegative() ||
      (slot.width <= 0.0) || (slot.height <= 0.0)) {
    throw std::invalid_argument("layout slot must be finite and positive");
  }

  const double width_scale = slot.width / content_size.width;
  const double height_scale = slot.height / content_size.height;

  // Pin the limiting axis to the slot exactly. Recomputing both dimensions
  // from a shared scale can round the limiting extent just beyond the slot
  // (for example, 600 * (868 / 600) becomes 868.0000000000001). That tiny
  // overshoot also produces a negative centered origin and violates the
  // strict containment contract used by input hit testing.
  if (width_scale <= height_scale) {
    const double height = std::min(
        slot.height, content_size.height * width_scale);
    return {
        slot.x,
        slot.y + std::max(0.0, slot.height - height) / 2.0,
        slot.width,
        height,
    };
  }

  const double width = std::min(
      slot.width, content_size.width * height_scale);
  return {
      slot.x + std::max(0.0, slot.width - width) / 2.0,
      slot.y,
      width,
      slot.height,
  };
}

} // namespace realmz::presentation
