#pragma once

#include <string>
#include <vector>

#include "RemasteredInputMapper.hpp"
#include "UIAction.hpp"

namespace realmz::presentation {

enum class ShellControlKind {
  movement,
  party_member,
};

struct ShellControlPlacement {
  ShellRegionId region;
  ShellControlKind kind = ShellControlKind::movement;
  LogicalRect bounds;
  std::string label;
  std::string accessibility_label;
  std::string focus_identifier;
  int32_t tab_order = 0;
  bool enabled = false;
  UIActionPayload payload;

  bool operator==(const ShellControlPlacement&) const = default;
};

struct ShellControlLayoutRequest {
  ScreenContext screen = ScreenContext::title;
  WorldPresentation world_presentation = WorldPresentation::none;
  LogicalRect action_panel;
  bool navigation_available = false;
};

// Produces code-native controls within the action bar. Empty output is a
// fail-closed result: the complete Classic frame remains the only interaction
// surface for any command that lacks a typed, live handler.
[[nodiscard]] std::vector<ShellControlPlacement>
compute_shell_control_layout(const ShellControlLayoutRequest& request);

} // namespace realmz::presentation
