#include "ShellKeyboardInteraction.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace realmz::presentation {
namespace {

std::vector<const ShellControlPlacement*> ordered_enabled_controls(
    std::span<const ShellControlPlacement> controls) {
  std::vector<const ShellControlPlacement*> ordered;
  ordered.reserve(controls.size());
  std::unordered_map<std::string_view, size_t> identity_counts;
  for (const auto& control : controls) {
    if (control.enabled && !control.focus_identifier.empty()) {
      ++identity_counts[control.focus_identifier];
    }
  }
  for (const auto& control : controls) {
    if (control.enabled && !control.focus_identifier.empty() &&
        (identity_counts[control.focus_identifier] == 1U)) {
      ordered.emplace_back(&control);
    }
  }
  std::stable_sort(
      ordered.begin(), ordered.end(),
      [](const auto* first, const auto* second) {
        return first->tab_order < second->tab_order;
      });
  return ordered;
}

} // namespace

const ShellControlPlacement*
ShellKeyboardInteraction::unique_enabled_control(
    std::span<const ShellControlPlacement> controls,
    std::string_view focus_identifier) noexcept {
  const ShellControlPlacement* match = nullptr;
  for (const auto& control : controls) {
    if (!control.enabled || control.focus_identifier.empty() ||
        (control.focus_identifier != focus_identifier)) {
      continue;
    }
    if (match) {
      return nullptr;
    }
    match = &control;
  }
  return match;
}

bool ShellKeyboardInteraction::same_semantic_control(
    const ShellControlPlacement& first,
    const ShellControlPlacement& second) noexcept {
  return first.enabled && second.enabled &&
      !first.focus_identifier.empty() &&
      (first.focus_identifier == second.focus_identifier) &&
      (first.region == second.region) &&
      (first.kind == second.kind) &&
      (first.payload == second.payload);
}

bool ShellKeyboardInteraction::reconcile(
    std::span<const ShellControlPlacement> controls,
    bool route_enabled) {
  bool changed = false;
  if (!route_enabled) {
    changed = this->focused_identifier_.has_value() ||
        this->activation_capture_.has_value();
    this->focused_identifier_.reset();
    this->focused_control_.reset();
    this->activation_capture_.reset();
    this->activation_token_.reset();
    for (auto& owned : this->owned_keys_) {
      owned.release_only = true;
    }
    return changed;
  }

  if (this->focused_identifier_) {
    const auto* current = unique_enabled_control(
        controls, *this->focused_identifier_);
    if (!current || !this->focused_control_ ||
        !same_semantic_control(*this->focused_control_, *current)) {
      this->focused_identifier_.reset();
      this->focused_control_.reset();
      changed = true;
    } else {
      this->focused_control_ = *current;
    }
  }

  if (this->activation_capture_) {
    const auto* current = unique_enabled_control(
        controls, this->activation_capture_->focus_identifier);
    if (!current ||
        !same_semantic_control(*this->activation_capture_, *current)) {
      this->activation_capture_.reset();
      this->activation_token_.reset();
      for (auto& owned : this->owned_keys_) {
        owned.release_only = true;
      }
      changed = true;
    }
  }
  return changed;
}

bool ShellKeyboardInteraction::focus_control(
    std::string_view focus_identifier,
    std::span<const ShellControlPlacement> controls,
    bool route_enabled) {
  (void)this->reconcile(controls, route_enabled);
  const auto* control = route_enabled
      ? unique_enabled_control(controls, focus_identifier)
      : nullptr;
  if (!control) {
    return false;
  }
  if (this->focused_identifier_ &&
      (*this->focused_identifier_ == focus_identifier)) {
    return false;
  }
  if (this->activation_capture_) {
    this->activation_capture_.reset();
    this->activation_token_.reset();
    for (auto& owned : this->owned_keys_) {
      owned.release_only = true;
    }
  }
  this->focused_identifier_ = focus_identifier;
  this->focused_control_ = *control;
  return true;
}

bool ShellKeyboardInteraction::clear_focus() noexcept {
  const bool changed = this->focused_identifier_.has_value() ||
      this->activation_capture_.has_value();
  this->focused_identifier_.reset();
  this->focused_control_.reset();
  this->activation_capture_.reset();
  this->activation_token_.reset();
  for (auto& owned : this->owned_keys_) {
    owned.release_only = true;
  }
  return changed;
}

ShellKeyboardResult ShellKeyboardInteraction::handle(
    const ShellKeyboardEvent& event,
    std::span<const ShellControlPlacement> controls,
    bool route_enabled) {
  ShellKeyboardResult result;
  result.visual_state_changed = this->reconcile(controls, route_enabled);
  auto owned = std::find_if(
      this->owned_keys_.begin(), this->owned_keys_.end(),
      [&event](const auto& candidate) {
        return candidate.token == event.token;
      });

  if (event.phase == ShellKeyboardPhase::up) {
    if (owned == this->owned_keys_.end()) {
      return result;
    }
    result.consumed = true;
    const auto released = *owned;
    this->owned_keys_.erase(owned);
    if (this->activation_token_ &&
        (*this->activation_token_ == released.token)) {
      const auto captured = this->activation_capture_;
      this->activation_capture_.reset();
      this->activation_token_.reset();
      result.visual_state_changed = true;
      if (route_enabled && captured) {
        const auto* current = unique_enabled_control(
            controls, captured->focus_identifier);
        if (current && same_semantic_control(*captured, *current) &&
            this->focused_identifier_ &&
            (*this->focused_identifier_ == captured->focus_identifier)) {
          result.invoked_control = *captured;
        }
      }
    }
    return result;
  }

  if (owned != this->owned_keys_.end()) {
    if (owned->release_only && !event.repeat) {
      // Focus loss and route transitions can prevent the original key-up from
      // arriving. A later nonrepeat down is a fresh physical press: retire the
      // tombstone instead of making that key unusable until another release.
      this->owned_keys_.erase(owned);
    } else {
      result.consumed = true;
      return result;
    }
  }

  // Never take ownership halfway through a physical hold. Platform repeat
  // events are consumed only when their initial down was already captured.
  if (event.repeat || !route_enabled) {
    return result;
  }

  const auto ordered = ordered_enabled_controls(controls);
  if (ordered.empty()) {
    return result;
  }

  if (event.key == ShellKeyboardKey::tab) {
    this->owned_keys_.emplace_back(OwnedKey{event.token, event.key, false});
    result.consumed = true;
    if (this->activation_capture_) {
      this->activation_capture_.reset();
      this->activation_token_.reset();
      for (auto& candidate : this->owned_keys_) {
        candidate.release_only = true;
      }
      this->owned_keys_.back().release_only = false;
      result.visual_state_changed = true;
    }

    const auto current = this->focused_identifier_
        ? std::find_if(
              ordered.begin(), ordered.end(),
              [this](const auto* control) {
                return control->focus_identifier ==
                    *this->focused_identifier_;
              })
        : ordered.end();
    const ShellControlPlacement* next = nullptr;
    if (current == ordered.end()) {
      next = event.shift ? ordered.back() : ordered.front();
    } else if (event.shift) {
      next = (current == ordered.begin()) ? ordered.back()
                                          : *std::prev(current);
    } else {
      next = (std::next(current) == ordered.end()) ? ordered.front()
                                                   : *std::next(current);
    }
    if (!this->focused_identifier_ ||
        (*this->focused_identifier_ != next->focus_identifier)) {
      this->focused_identifier_ = next->focus_identifier;
      this->focused_control_ = *next;
      result.visual_state_changed = true;
    }
    return result;
  }

  if (!this->focused_identifier_) {
    return result;
  }
  const auto* focused = unique_enabled_control(
      controls, *this->focused_identifier_);
  if (!focused) {
    return result;
  }

  this->owned_keys_.emplace_back(OwnedKey{event.token, event.key, false});
  result.consumed = true;
  if (!this->activation_capture_) {
    this->activation_capture_ = *focused;
    this->activation_token_ = event.token;
    result.visual_state_changed = true;
  }
  return result;
}

void ShellKeyboardInteraction::cancel_route() noexcept {
  this->focused_identifier_.reset();
  this->focused_control_.reset();
  this->activation_capture_.reset();
  this->activation_token_.reset();
  for (auto& owned : this->owned_keys_) {
    owned.release_only = true;
  }
}

void ShellKeyboardInteraction::reset() noexcept {
  this->cancel_route();
  this->owned_keys_.clear();
}

std::optional<std::string_view>
ShellKeyboardInteraction::pressed_identifier() const noexcept {
  if (!this->activation_capture_) {
    return std::nullopt;
  }
  return this->activation_capture_->focus_identifier;
}

bool ShellKeyboardInteraction::owns_key(ShellKeyboardKey key) const noexcept {
  return std::ranges::any_of(
      this->owned_keys_, [key](const auto& owned) {
        return owned.key == key;
      });
}

bool ShellKeyboardInteraction::owns_token(
    ShellPhysicalKeyToken token) const noexcept {
  return std::ranges::any_of(
      this->owned_keys_, [token](const auto& owned) {
        return owned.token == token;
      });
}

} // namespace realmz::presentation
