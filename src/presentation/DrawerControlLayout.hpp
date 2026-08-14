#pragma once

#include <cstdint>
#include <vector>

#include "PartyRailModel.hpp"
#include "ShellControlLayout.hpp"

namespace realmz::presentation {

// Drawer controls use a namespace separate from movement (1000-range), party
// cards (2000-range), and informational panel regions.
[[nodiscard]] constexpr ShellRegionId drawer_panel_region_id(
    DrawerPanel panel) noexcept {
  return ShellRegionId{
      3000U + static_cast<uint32_t>(panel),
  };
}

// Lays out the two compact-shell drawer tabs. The returned controls carry an
// explicit SetDrawerPanelAction: selecting a closed tab opens that panel and
// selecting the active tab closes it. Malformed model or geometry input fails
// closed with no partial controls.
[[nodiscard]] std::vector<ShellControlPlacement>
compute_drawer_control_layout(
    const DrawerModel& drawers,
    LogicalRect panel_bounds,
    bool controls_available);

} // namespace realmz::presentation
