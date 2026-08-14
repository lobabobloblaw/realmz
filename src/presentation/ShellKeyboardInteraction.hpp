#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ShellControlLayout.hpp"

namespace realmz::presentation {

enum class ShellKeyboardKey {
  tab,
  enter,
  space,
};

enum class ShellKeyboardPhase {
  down,
  up,
};

struct ShellPhysicalKeyToken {
  uint32_t keyboard = 0;
  uint32_t scancode = 0;

  bool operator==(const ShellPhysicalKeyToken&) const = default;
};

struct ShellKeyboardEvent {
  ShellPhysicalKeyToken token;
  ShellKeyboardKey key = ShellKeyboardKey::tab;
  ShellKeyboardPhase phase = ShellKeyboardPhase::down;
  bool shift = false;
  bool repeat = false;

  bool operator==(const ShellKeyboardEvent&) const = default;
};

struct ShellKeyboardResult {
  bool consumed = false;
  bool visual_state_changed = false;
  std::optional<ShellControlPlacement> invoked_control;

  bool operator==(const ShellKeyboardResult&) const = default;
};

// Owns only the code-native shell keyboard route. It never synthesizes a
// Classic key or mutates game state; callers dispatch invoked_control through
// LegacyCommandBridge after this state machine has validated a complete
// press/release pair.
class ShellKeyboardInteraction {
public:
  [[nodiscard]] ShellKeyboardResult handle(
      const ShellKeyboardEvent& event,
      std::span<const ShellControlPlacement> controls,
      bool route_enabled);

  // Reconciles persistent focus by semantic identifier across layout rebuilds.
  // Disabling the route cancels activation but retains key ownership long
  // enough to consume the matching key-up instead of leaking it to Classic.
  [[nodiscard]] bool reconcile(
      std::span<const ShellControlPlacement> controls,
      bool route_enabled);

  // Pointer interaction may move keyboard focus onto the same semantic
  // control. Returns true only when the visible focus changed.
  [[nodiscard]] bool focus_control(
      std::string_view focus_identifier,
      std::span<const ShellControlPlacement> controls,
      bool route_enabled);

  [[nodiscard]] bool clear_focus() noexcept;

  void cancel_route() noexcept;
  void reset() noexcept;

  [[nodiscard]] const std::optional<std::string>& focused_identifier()
      const noexcept {
    return this->focused_identifier_;
  }

  [[nodiscard]] std::optional<std::string_view> pressed_identifier()
      const noexcept;

  [[nodiscard]] bool owns_key(ShellKeyboardKey key) const noexcept;
  [[nodiscard]] bool owns_token(ShellPhysicalKeyToken token) const noexcept;

private:
  [[nodiscard]] static const ShellControlPlacement* unique_enabled_control(
      std::span<const ShellControlPlacement> controls,
      std::string_view focus_identifier) noexcept;
  [[nodiscard]] static bool same_semantic_control(
      const ShellControlPlacement& first,
      const ShellControlPlacement& second) noexcept;

  std::optional<std::string> focused_identifier_;
  std::optional<ShellControlPlacement> focused_control_;
  std::optional<ShellControlPlacement> activation_capture_;
  struct OwnedKey {
    ShellPhysicalKeyToken token;
    ShellKeyboardKey key = ShellKeyboardKey::tab;
    bool release_only = false;
  };

  std::optional<ShellPhysicalKeyToken> activation_token_;
  std::vector<OwnedKey> owned_keys_;
};

} // namespace realmz::presentation
