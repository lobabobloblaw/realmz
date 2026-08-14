#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Geometry.hpp"

namespace realmz::presentation {

using WidgetId = uint64_t;
inline constexpr WidgetId kInvalidWidgetId = 0;

struct HitRegion {
  WidgetId id = kInvalidWidgetId;
  LogicalRect bounds;
  int32_t z_order = 0;
  bool enabled = true;
  bool visible = true;

  bool operator==(const HitRegion&) const = default;
};

// Returns the enabled, visible region with the highest z-order. When regions
// share a z-order, the later entry is treated as being painted later and wins.
[[nodiscard]] std::optional<WidgetId> hit_test(
    std::span<const HitRegion> regions,
    LogicalPoint point) noexcept;

struct FocusEntry {
  WidgetId id = kInvalidWidgetId;
  int32_t tab_order = 0;
  bool enabled = true;
  bool visible = true;

  bool operator==(const FocusEntry&) const = default;

  [[nodiscard]] bool accepts_focus() const noexcept {
    return (this->id != kInvalidWidgetId) && this->enabled && this->visible;
  }
};

enum class FocusMove {
  next,
  previous,
};

class FocusNavigator {
public:
  void set_entries(std::vector<FocusEntry> entries);

  [[nodiscard]] const std::vector<FocusEntry>& entries() const noexcept {
    return this->entries_;
  }

  [[nodiscard]] std::optional<WidgetId> focused() const noexcept {
    return this->focused_;
  }

  // Returns false and preserves focus when the requested widget is not an
  // enabled, visible focus target.
  bool set_focus(WidgetId id) noexcept;
  void clear_focus() noexcept;

  // Traversal wraps and is stable for equal tab_order values.
  [[nodiscard]] std::optional<WidgetId> move(FocusMove direction);

private:
  [[nodiscard]] std::vector<WidgetId> ordered_focusable_ids() const;

  std::vector<FocusEntry> entries_;
  std::optional<WidgetId> focused_;
};

} // namespace realmz::presentation
