#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PartyRailLayout.hpp"
#include "RemasteredInputMapper.hpp"
#include "UIAction.hpp"

namespace realmz::presentation {

// A party-card control deliberately carries the exact renderer card bounds.
// The same value can therefore be passed to RemasteredInputMapper as its hit
// region without maintaining a second, drifting geometry calculation.
struct PartyRailControlPlacement {
  ShellRegionId region;
  LogicalRect bounds;
  PartyMemberId member_id = 0;
  std::string label;
  std::string accessibility_label;
  FocusIdentifier focus_identifier;
  CommandIdentifier command_identifier;
  int32_t tab_order = 0;
  bool enabled = false;
  bool selected = false;
  UIActionPayload payload;

  bool operator==(const PartyRailControlPlacement&) const = default;
};

struct PartyRailControlLayoutRequest {
  ScreenContext screen = ScreenContext::title;

  // The runtime owner sets this only while its guarded, top-level semantic
  // gameplay route is live. Nested Classic loops and modal screens pass false.
  bool selection_available = false;
};

// Party-member region IDs occupy a namespace separate from the movement
// controls in ShellControlLayout. They are derived from the semantic member ID
// rather than vector position, so recomposition cannot retarget a held press.
[[nodiscard]] constexpr ShellRegionId party_member_region_id(
    PartyMemberId member) noexcept {
  return ShellRegionId{2000U + static_cast<uint32_t>(member)};
}

// Returns one control per model/layout member for eligible exploration and
// dungeon screens. Any mismatch, duplicate semantic identity, sub-44-point
// card, or unsupported count fails closed with no partial controls.
[[nodiscard]] std::vector<PartyRailControlPlacement>
compute_party_rail_control_layout(
    const PartyRailControlLayoutRequest& request,
    const PartyRailModel& party,
    const PartyRailLayout& layout);

} // namespace realmz::presentation
