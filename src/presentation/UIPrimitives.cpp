#include "UIPrimitives.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace realmz::presentation {

std::optional<WidgetId> hit_test(
    std::span<const HitRegion> regions,
    LogicalPoint point) noexcept {
  std::optional<WidgetId> result;
  int32_t result_z = 0;

  for (const auto& region : regions) {
    if ((region.id == kInvalidWidgetId) || !region.enabled || !region.visible ||
        !region.bounds.contains(point)) {
      continue;
    }
    if (!result || (region.z_order >= result_z)) {
      result = region.id;
      result_z = region.z_order;
    }
  }
  return result;
}

void FocusNavigator::set_entries(std::vector<FocusEntry> entries) {
  std::unordered_set<WidgetId> ids;
  for (const auto& entry : entries) {
    if (entry.id == kInvalidWidgetId) {
      throw std::invalid_argument("focus entry uses the invalid widget id");
    }
    if (!ids.emplace(entry.id).second) {
      throw std::invalid_argument("focus entries contain a duplicate widget id");
    }
  }

  this->entries_ = std::move(entries);
  if (this->focused_ && !this->set_focus(*this->focused_)) {
    this->focused_.reset();
  }
}

bool FocusNavigator::set_focus(WidgetId id) noexcept {
  for (const auto& entry : this->entries_) {
    if (entry.id == id) {
      if (entry.accepts_focus()) {
        this->focused_ = id;
        return true;
      }
      return false;
    }
  }
  return false;
}

void FocusNavigator::clear_focus() noexcept {
  this->focused_.reset();
}

std::vector<WidgetId> FocusNavigator::ordered_focusable_ids() const {
  struct OrderedEntry {
    WidgetId id;
    int32_t tab_order;
    size_t insertion_order;
  };

  std::vector<OrderedEntry> ordered;
  ordered.reserve(this->entries_.size());
  for (size_t z = 0; z < this->entries_.size(); z++) {
    const auto& entry = this->entries_[z];
    if (entry.accepts_focus()) {
      ordered.push_back(OrderedEntry{entry.id, entry.tab_order, z});
    }
  }
  std::sort(ordered.begin(), ordered.end(),
      [](const OrderedEntry& a, const OrderedEntry& b) {
        if (a.tab_order != b.tab_order) {
          return a.tab_order < b.tab_order;
        }
        return a.insertion_order < b.insertion_order;
      });

  std::vector<WidgetId> result;
  result.reserve(ordered.size());
  for (const auto& entry : ordered) {
    result.emplace_back(entry.id);
  }
  return result;
}

std::optional<WidgetId> FocusNavigator::move(FocusMove direction) {
  const auto ordered = this->ordered_focusable_ids();
  if (ordered.empty()) {
    this->focused_.reset();
    return std::nullopt;
  }

  if (!this->focused_) {
    this->focused_ = (direction == FocusMove::next)
        ? ordered.front()
        : ordered.back();
    return this->focused_;
  }

  const auto current = std::find(
      ordered.begin(), ordered.end(), *this->focused_);
  if (current == ordered.end()) {
    this->focused_ = (direction == FocusMove::next)
        ? ordered.front()
        : ordered.back();
  } else if (direction == FocusMove::next) {
    this->focused_ = (std::next(current) == ordered.end())
        ? ordered.front()
        : *std::next(current);
  } else {
    this->focused_ = (current == ordered.begin())
        ? ordered.back()
        : *std::prev(current);
  }
  return this->focused_;
}

} // namespace realmz::presentation
