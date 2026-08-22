#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Geometry.hpp"
#include "PartyRailModel.hpp"
#include "ShellControlLayout.hpp"

namespace realmz::presentation {

enum class WorldContextLayoutDensity {
  compact,
  wide,
};

// A complete renderer-ready condition. The signed raw value remains attached
// to the model token as exact state evidence without assigning it a unit.
struct WorldContextConditionLayout {
  int16_t raw_value = 0;
  StateTokenModel state;
  std::string marker_text;
  std::string render_text;

  bool operator==(const WorldContextConditionLayout&) const = default;
};

struct WorldContextLineLayout {
  LogicalRect bounds;
  std::string text;
  TextStyleModel text_style;
  StateEmphasis emphasis = StateEmphasis::neutral;

  bool operator==(const WorldContextLineLayout&) const = default;
};

// Internal semantic accessibility text is retained in full. This value-only
// layout makes no claim that an operating-system accessibility publisher is
// present.
struct WorldContextLayout {
  LogicalRect bounds;
  WorldContextLayoutDensity density = WorldContextLayoutDensity::compact;
  std::vector<WorldContextLineLayout> lines;
  // Search is always first and Torch second.
  std::array<WorldContextConditionLayout, 2> condition_tokens;
  std::string accessibility_text;

  bool operator==(const WorldContextLayout&) const = default;
};

struct WorldContextLayoutRequest {
  const WorldContextModel& world_context;
  ScreenContext screen;
  LogicalRect action_panel;
  TypographyModel typography;
  std::span<const ShellControlPlacement> action_controls;
};

// Uses the existing persistent world-tab header without moving or resizing any
// action control. Malformed model, typography, panel, or tab geometry throws
// std::invalid_argument instead of returning partial World Context content.
[[nodiscard]] WorldContextLayout compute_world_context_layout(
    const WorldContextLayoutRequest& request);

} // namespace realmz::presentation
