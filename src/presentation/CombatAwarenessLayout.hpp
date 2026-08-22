#pragma once

#include <span>
#include <string>
#include <vector>

#include "Geometry.hpp"
#include "PartyRailModel.hpp"
#include "ShellControlLayout.hpp"

namespace realmz::presentation {

enum class CombatAwarenessLayoutDensity {
  compact,
  wide,
};

struct CombatAwarenessLineLayout {
  LogicalRect bounds;
  std::string text;
  TextStyleModel text_style;
  StateEmphasis emphasis = StateEmphasis::information;

  bool operator==(const CombatAwarenessLineLayout&) const = default;
};

// Internal semantic accessibility text is retained in full. This value-only
// layout makes no claim that an operating-system accessibility publisher is
// present.
struct CombatAwarenessLayout {
  LogicalRect bounds;
  CombatAwarenessLayoutDensity density =
      CombatAwarenessLayoutDensity::compact;
  std::vector<CombatAwarenessLineLayout> lines;
  std::string accessibility_text;
  // An empty combat command deck leaves no tab-owned header. In that exact
  // shape this layout owns the complete inset header row, allowing a renderer
  // to suppress its otherwise-fallback ACTIONS label without inference.
  bool owns_full_zero_tab_header = false;

  bool operator==(const CombatAwarenessLayout&) const = default;
};

struct CombatAwarenessLayoutRequest {
  const CombatAwarenessModel& combat_awareness;
  ScreenContext screen;
  LogicalRect action_panel;
  TypographyModel typography;
  std::span<const ShellControlPlacement> action_controls;
};

// Accepts only the two authoritative combat-header shapes: a canonical four-
// tab combat deck or an exactly empty command deck. Malformed model values,
// typography, panel geometry, tabs, or mixed controls throw
// std::invalid_argument rather than returning partial awareness content.
[[nodiscard]] CombatAwarenessLayout compute_combat_awareness_layout(
    const CombatAwarenessLayoutRequest& request);

} // namespace realmz::presentation
