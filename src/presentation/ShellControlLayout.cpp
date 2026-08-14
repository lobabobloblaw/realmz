#include "ShellControlLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace realmz::presentation {
namespace {

constexpr uint32_t kMovementRegionBase = 1000U;
constexpr uint32_t kInventoryRegion = 1100U;
constexpr uint32_t kSpellbookRegion = 1101U;
constexpr uint32_t kSaveGameRegion = 1102U;
constexpr uint32_t kLoadGameRegion = 1103U;
constexpr uint32_t kGuardCombatantRegion = 1104U;
constexpr uint32_t kFinishCombatantRegion = 1105U;
constexpr uint32_t kDelayCombatantRegion = 1106U;
constexpr uint32_t kCenterActiveCombatantRegion = 1107U;
constexpr uint32_t kCombatActionPageRegion = 1108U;
constexpr uint32_t kSwitchWeaponSetRegion = 1109U;
constexpr double kHorizontalInset = 14.0;
constexpr double kHeaderTopInset = 10.0;
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
  const bool world_controls = !descriptors.empty();
  const bool primary_combat_page =
      request.combat_action_page == CombatActionPage::primary;
  const bool secondary_combat_page =
      request.combat_action_page == CombatActionPage::secondary;
  if (!primary_combat_page && !secondary_combat_page) {
    return {};
  }
  const auto valid_combatant = [](CombatantId combatant) {
    return (combatant >= 0) && (combatant <= 0xFF);
  };
  const bool valid_guard = request.guard_combatant &&
      valid_combatant(*request.guard_combatant);
  const bool valid_finish = request.finish_combatant &&
      valid_combatant(*request.finish_combatant);
  const bool valid_delay = request.delay_combatant &&
      valid_combatant(*request.delay_combatant);
  const bool valid_center = request.center_active_combatant &&
      valid_combatant(*request.center_active_combatant);
  const bool valid_switch_weapon = request.switch_weapon_combatant &&
      valid_combatant(*request.switch_weapon_combatant);
  const std::array combatants{
      request.guard_combatant,
      request.finish_combatant,
      request.delay_combatant,
      request.center_active_combatant,
      request.switch_weapon_combatant,
  };
  std::optional<CombatantId> common_combatant;
  bool invalid_combatant = false;
  bool mismatched_combatants = false;
  for (const auto combatant : combatants) {
    if (!combatant) {
      continue;
    }
    if (!valid_combatant(*combatant)) {
      invalid_combatant = true;
      continue;
    }
    if (common_combatant && (*common_combatant != *combatant)) {
      mismatched_combatants = true;
    } else {
      common_combatant = combatant;
    }
  }
  const size_t primary_combat_control_count =
      (request.guard_combatant ? 1U : 0U) +
      (request.finish_combatant ? 1U : 0U) +
      (request.delay_combatant ? 1U : 0U) +
      (request.center_active_combatant ? 1U : 0U);
  const size_t combat_control_count = primary_combat_page
      ? primary_combat_control_count
      : (request.switch_weapon_combatant ? 1U : 0U);
  const bool combat_page_control_visible =
      secondary_combat_page || valid_switch_weapon;
  const bool has_combatant_request = std::ranges::any_of(
      combatants,
      [](const auto& combatant) { return combatant.has_value(); });
  const bool combat_controls =
      (request.screen == ScreenContext::combat) &&
      (combat_control_count > 0U) && !invalid_combatant &&
      !mismatched_combatants;
  const bool has_combat_request = has_combatant_request ||
      request.guard_available || request.finish_available ||
      request.delay_available || request.center_active_available ||
      request.switch_weapon_available || secondary_combat_page;
  if ((!world_controls && !combat_controls) ||
      (world_controls && has_combat_request) ||
      (combat_controls &&
          (request.navigation_available || request.inventory_member ||
              request.spellbook_member || request.save_control_visible ||
              request.load_control_visible)) ||
      (request.inventory_available && !request.inventory_member) ||
      (request.spellbook_available && !request.spellbook_member) ||
      (request.save_available && !request.save_control_visible) ||
      (request.load_available && !request.load_control_visible) ||
      (request.guard_available && !valid_guard) ||
      (request.finish_available && !valid_finish) ||
      (request.delay_available && !valid_delay) ||
      (request.center_active_available && !valid_center) ||
      (request.switch_weapon_available && !valid_switch_weapon)) {
    return {};
  }

  const size_t control_count = combat_controls
      ? combat_control_count
      : descriptors.size() +
          (request.inventory_member ? 1U : 0U) +
          (request.spellbook_member ? 1U : 0U) +
          (request.save_control_visible ? 1U : 0U) +
          (request.load_control_visible ? 1U : 0U);

  const double available_width =
      request.action_panel.width - 2.0 * kHorizontalInset;
  const double available_height = request.action_panel.height -
      kControlsTopInset - kBottomInset;
  const double gap = std::clamp(available_width * 0.008, 6.0, 10.0);
  const double computed_button_width =
      (available_width - gap * (control_count - 1U)) /
      control_count;
  const double button_width = combat_controls
      ? std::min(160.0, computed_button_width)
      : computed_button_width;
  const double button_height = std::min(52.0, available_height);
  if (!std::isfinite(button_width) || !std::isfinite(button_height) ||
      (button_width < kMinimumTargetExtent) ||
      (button_height < kMinimumTargetExtent)) {
    return {};
  }

  std::vector<ShellControlPlacement> result;
  result.reserve(
      control_count + (combat_page_control_visible ? 1U : 0U));
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
  if (request.inventory_member) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kInventoryRegion},
        .kind = ShellControlKind::open_inventory,
        .bounds = {x, y, button_width, button_height},
        .label = "ITEMS",
        .accessibility_label = "Open inventory",
        .focus_identifier = "focus.action.inventory.open",
        .tab_order = 1100,
        .enabled = request.inventory_available,
        .payload = OpenInventoryAction{*request.inventory_member},
    });
    x += button_width + gap;
  }
  if (request.spellbook_member) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kSpellbookRegion},
        .kind = ShellControlKind::open_spellbook,
        .bounds = {x, y, button_width, button_height},
        .label = "SPELLS",
        .accessibility_label = "Cast spell",
        .focus_identifier = "focus.action.spellbook.open",
        .tab_order = 1101,
        .enabled = request.spellbook_available,
        .payload = OpenSpellbookAction{*request.spellbook_member},
    });
    x += button_width + gap;
  }
  if (request.save_control_visible) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kSaveGameRegion},
        .kind = ShellControlKind::open_save_game,
        .bounds = {x, y, button_width, button_height},
        .label = "SAVE",
        .accessibility_label = "Open save dialog",
        .focus_identifier = "focus.action.save.open",
        .tab_order = 1102,
        .enabled = request.save_available,
        .payload = OpenSaveGameAction{},
    });
    x += button_width + gap;
  }
  if (request.load_control_visible) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kLoadGameRegion},
        .kind = ShellControlKind::open_load_game,
        .bounds = {x, y, button_width, button_height},
        .label = "LOAD",
        .accessibility_label = "Open load dialog",
        .focus_identifier = "focus.action.load.open",
        .tab_order = 1103,
        .enabled = request.load_available,
        .payload = OpenLoadGameAction{},
    });
    x += button_width + gap;
  }
  if (!combat_controls) {
    return result;
  }

  const auto append_page_control = [&request, &result]() {
    const bool primary =
        request.combat_action_page == CombatActionPage::primary;
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kCombatActionPageRegion},
        .kind = ShellControlKind::combat_action_page,
        .bounds = {
            request.action_panel.right() - kHorizontalInset -
                kMinimumTargetExtent,
            request.action_panel.y + kHeaderTopInset,
            kMinimumTargetExtent,
            kMinimumTargetExtent,
        },
        .label = primary ? "MORE" : "BACK",
        .accessibility_label = primary
            ? "Open more combat actions"
            : "Return to primary combat actions",
        .focus_identifier = "focus.action.combat.more",
        .tab_order = 1108,
        .enabled = true,
        .payload = SetCombatActionPageAction{
            primary ? CombatActionPage::secondary
                    : CombatActionPage::primary},
    });
  };

  if (secondary_combat_page) {
    append_page_control();
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kSwitchWeaponSetRegion},
        .kind = ShellControlKind::switch_weapon_set,
        .bounds = {x, y, button_width, button_height},
        .label = "WEAPON",
        .accessibility_label = "Switch active combatant's weapon set",
        .focus_identifier = "focus.action.combat.weapon",
        .tab_order = 1109,
        .enabled = request.switch_weapon_available,
        .payload = SwitchWeaponSetAction{
            *request.switch_weapon_combatant},
    });
    return result;
  }

  if (request.guard_combatant) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kGuardCombatantRegion},
        .kind = ShellControlKind::guard_combatant,
        .bounds = {x, y, button_width, button_height},
        .label = "GUARD",
        .accessibility_label = "Guard active combatant",
        .focus_identifier = "focus.action.combat.guard",
        .tab_order = 1104,
        .enabled = request.guard_available,
        .payload = GuardCombatantAction{*request.guard_combatant},
    });
    x += button_width + gap;
  }
  if (request.finish_combatant) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kFinishCombatantRegion},
        .kind = ShellControlKind::finish_combatant,
        .bounds = {x, y, button_width, button_height},
        .label = "FINISH",
        .accessibility_label = "Finish active combatant's turn",
        .focus_identifier = "focus.action.combat.finish",
        .tab_order = 1105,
        .enabled = request.finish_available,
        .payload = FinishCombatantAction{*request.finish_combatant},
    });
    x += button_width + gap;
  }
  if (request.delay_combatant) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kDelayCombatantRegion},
        .kind = ShellControlKind::delay_combatant,
        .bounds = {x, y, button_width, button_height},
        .label = "DELAY",
        .accessibility_label = "Delay active combatant's turn",
        .focus_identifier = "focus.action.combat.delay",
        .tab_order = 1106,
        .enabled = request.delay_available,
        .payload = DelayCombatantAction{*request.delay_combatant},
    });
    x += button_width + gap;
  }
  if (request.center_active_combatant) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kCenterActiveCombatantRegion},
        .kind = ShellControlKind::center_active_combatant,
        .bounds = {x, y, button_width, button_height},
        .label = "CENTER",
        .accessibility_label = "Center view on active combatant",
        .focus_identifier = "focus.action.combat.center",
        .tab_order = 1107,
        .enabled = request.center_active_available,
        .payload = CenterActiveCombatantAction{
            *request.center_active_combatant},
    });
  }
  if (valid_switch_weapon) {
    append_page_control();
  }
  return result;
}

} // namespace realmz::presentation
