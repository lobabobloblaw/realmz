#include "DrawerControlLayout.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace realmz::presentation {
namespace {

constexpr double kInset = 8.0;
constexpr double kGap = 8.0;
constexpr double kTabHeight = 44.0;
constexpr double kMinimumTargetExtent = 44.0;

[[nodiscard]] bool valid_panel(DrawerPanel panel) noexcept {
  return panel == DrawerPanel::details || panel == DrawerPanel::event_log;
}

} // namespace

std::vector<ShellControlPlacement> compute_drawer_control_layout(
    const DrawerModel& drawers,
    LogicalRect panel_bounds,
    bool controls_available) {
  if (!drawers.collapsed || drawers.tabs.size() != 2U ||
      !panel_bounds.is_finite_and_nonnegative() ||
      panel_bounds.width < (2.0 * kMinimumTargetExtent + 2.0 * kInset + kGap) ||
      panel_bounds.height < (kTabHeight + 2.0 * kInset)) {
    return {};
  }

  std::set<DrawerPanel> panels;
  std::set<uint32_t> regions;
  std::set<std::string_view> focus_identifiers;
  std::set<std::string_view> command_identifiers;
  std::set<int32_t> tab_orders;
  size_t active_count = 0;
  for (const auto& tab : drawers.tabs) {
    const auto region = drawer_panel_region_id(tab.panel);
    if (!valid_panel(tab.panel) || tab.label.empty() || tab.command.empty() ||
        tab.focus_identifier.empty() || !panels.emplace(tab.panel).second ||
        !regions.emplace(region.value).second ||
        !focus_identifiers.emplace(tab.focus_identifier).second ||
        !command_identifiers.emplace(tab.command).second ||
        !tab_orders.emplace(tab.tab_order).second) {
      return {};
    }
    if (tab.active) {
      ++active_count;
      if (!drawers.active_panel || *drawers.active_panel != tab.panel) {
        return {};
      }
    }
  }
  if (panels != std::set<DrawerPanel>{DrawerPanel::details, DrawerPanel::event_log}) {
    return {};
  }
  if ((active_count > 1U) ||
      (drawers.active_panel.has_value() != (active_count == 1U))) {
    return {};
  }

  const double available_width = panel_bounds.width - 2.0 * kInset - kGap;
  const double first_width = available_width / 2.0;
  const std::array<LogicalRect, 2> bounds{
      LogicalRect{
          panel_bounds.x + kInset,
          panel_bounds.y + kInset,
          first_width,
          kTabHeight,
      },
      LogicalRect{
          panel_bounds.x + kInset + first_width + kGap,
          panel_bounds.y + kInset,
          available_width - first_width,
          kTabHeight,
      },
  };
  if (!panel_bounds.contains(bounds[0]) ||
      !panel_bounds.contains(bounds[1]) ||
      bounds[0].width < kMinimumTargetExtent ||
      bounds[1].width < kMinimumTargetExtent) {
    return {};
  }

  std::vector<ShellControlPlacement> controls;
  controls.reserve(drawers.tabs.size());
  for (size_t index = 0; index < drawers.tabs.size(); ++index) {
    const auto& tab = drawers.tabs[index];
    controls.emplace_back(ShellControlPlacement{
        .region = drawer_panel_region_id(tab.panel),
        .kind = ShellControlKind::drawer_tab,
        .bounds = bounds[index],
        .label = tab.label,
        .accessibility_label =
            std::string(tab.active ? "Close " : "Open ") +
            tab.label + " drawer",
        .focus_identifier = tab.focus_identifier,
        .tab_order = tab.tab_order,
        .enabled = controls_available,
        .payload = SetDrawerPanelAction{
            tab.active
                ? std::nullopt
                : std::optional<DrawerPanel>{tab.panel},
        },
    });
  }
  return controls;
}

} // namespace realmz::presentation
