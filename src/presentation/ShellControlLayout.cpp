#include "ShellControlLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace realmz::presentation {
namespace {

constexpr uint32_t kMovementRegionBase = 1000U;
constexpr double kHorizontalInset = 14.0;
constexpr double kControlsTopInset = 64.0;
constexpr double kBottomInset = 12.0;
constexpr double kMinimumTargetExtent = 44.0;

struct MovementDescriptor {
  MovementCommand command;
  uint32_t semantic_region_offset;
  std::string_view label;
  std::string_view accessibility_label;
  std::string_view identifier;
};

constexpr std::array<MovementDescriptor, 8> kOutdoorMovement{
    MovementDescriptor{MovementCommand::northwest, 11, "NW", "Move northwest", "northwest"},
    MovementDescriptor{MovementCommand::north, 4, "N", "Move north", "north"},
    MovementDescriptor{MovementCommand::northeast, 5, "NE", "Move northeast", "northeast"},
    MovementDescriptor{MovementCommand::west, 10, "W", "Move west", "west"},
    MovementDescriptor{MovementCommand::east, 6, "E", "Move east", "east"},
    MovementDescriptor{MovementCommand::southwest, 9, "SW", "Move southwest", "southwest"},
    MovementDescriptor{MovementCommand::south, 8, "S", "Move south", "south"},
    MovementDescriptor{MovementCommand::southeast, 7, "SE", "Move southeast", "southeast"},
};

constexpr std::array<MovementDescriptor, 4> kDungeonMovement{
    MovementDescriptor{MovementCommand::turn_left, 2, "TURN LEFT", "Turn left", "turn_left"},
    MovementDescriptor{MovementCommand::step_forward, 0, "FORWARD", "Step forward", "step_forward"},
    MovementDescriptor{MovementCommand::step_backward, 1, "BACK", "Step backward", "step_backward"},
    MovementDescriptor{MovementCommand::turn_right, 3, "TURN RIGHT", "Turn right", "turn_right"},
};

std::span<const MovementDescriptor> descriptors_for(
    const ShellControlLayoutRequest& request) noexcept {
  if ((request.screen == ScreenContext::exploration) &&
      (request.world_presentation == WorldPresentation::outdoor)) {
    return kOutdoorMovement;
  }
  const bool dungeon_presentation =
      (request.world_presentation == WorldPresentation::dungeon_map) ||
      (request.world_presentation == WorldPresentation::dungeon_first_person);
  if ((request.screen == ScreenContext::dungeon) && dungeon_presentation) {
    return kDungeonMovement;
  }
  return {};
}

} // namespace

std::vector<ShellControlPlacement> compute_shell_control_layout(
    const ShellControlLayoutRequest& request) {
  if (!request.action_panel.is_finite_and_nonnegative() ||
      (request.action_panel.width <= 0.0) ||
      (request.action_panel.height <= 0.0)) {
    return {};
  }
  const auto descriptors = descriptors_for(request);
  if (descriptors.empty()) {
    return {};
  }

  const double available_width =
      request.action_panel.width - 2.0 * kHorizontalInset;
  const double available_height = request.action_panel.height -
      kControlsTopInset - kBottomInset;
  const double gap = std::clamp(available_width * 0.008, 6.0, 10.0);
  const double button_width =
      (available_width - gap * (descriptors.size() - 1U)) /
      descriptors.size();
  const double button_height = std::min(52.0, available_height);
  if (!std::isfinite(button_width) || !std::isfinite(button_height) ||
      (button_width < kMinimumTargetExtent) ||
      (button_height < kMinimumTargetExtent)) {
    return {};
  }

  std::vector<ShellControlPlacement> result;
  result.reserve(descriptors.size());
  double x = request.action_panel.x + kHorizontalInset;
  const double y = request.action_panel.y + kControlsTopInset;
  for (size_t index = 0; index < descriptors.size(); ++index) {
    const auto& descriptor = descriptors[index];
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{
            kMovementRegionBase + descriptor.semantic_region_offset},
        .kind = ShellControlKind::movement,
        .bounds = {x, y, button_width, button_height},
        .label = std::string(descriptor.label),
        .accessibility_label = std::string(descriptor.accessibility_label),
        .focus_identifier =
            "focus.action.move." + std::string(descriptor.identifier),
        .tab_order = 1000 + static_cast<int32_t>(index),
        .enabled = request.navigation_available,
        .payload = MovePartyAction{descriptor.command},
    });
    x += button_width + gap;
  }
  return result;
}

} // namespace realmz::presentation
