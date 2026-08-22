#include "ShellControlLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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
constexpr uint32_t kOpenScrollCaseRegion = 1108U;
constexpr uint32_t kSwitchWeaponSetRegion = 1109U;
constexpr uint32_t kCenterPreviousCombatantRegion = 1110U;
constexpr uint32_t kCenterNextCombatantRegion = 1111U;
constexpr uint32_t kOpenCombatItemsRegion = 1112U;
constexpr uint32_t kAutoCombatantRegion = 1114U;
constexpr uint32_t kShowCombatRangeRegion = 1115U;
constexpr uint32_t kBandageCombatantRegion = 1116U;
constexpr uint32_t kUndoCombatantRegion = 1117U;
constexpr uint32_t kOpenCombatSpellbookRegion = 1119U;
constexpr uint32_t kOpenCombatTargetingRegion = 1120U;
constexpr uint32_t kEscapeCombatRegion = 1121U;
constexpr uint32_t kOpenCombatScrollCaseRegion = 1122U;
constexpr uint32_t kCenterCombatCursorRegion = 1123U;
constexpr uint32_t kCombatTurnPageRegion = 1200U;
constexpr uint32_t kCombatGearPageRegion = 1201U;
constexpr uint32_t kCombatTacticsPageRegion = 1202U;
constexpr uint32_t kCombatSpecialPageRegion = 1203U;
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

struct CombatPageDescriptor {
  CombatActionPage page;
  uint32_t region;
  std::string_view label;
  std::string_view accessibility_label;
  std::string_view identifier;
};

constexpr std::array<CombatPageDescriptor, 4> kCombatPages{
    CombatPageDescriptor{
        CombatActionPage::primary,
        kCombatTurnPageRegion,
        "TURN",
        "Turn combat commands",
        "turn"},
    CombatPageDescriptor{
        CombatActionPage::secondary,
        kCombatGearPageRegion,
        "GEAR",
        "Gear and view combat commands",
        "gear"},
    CombatPageDescriptor{
        CombatActionPage::utility,
        kCombatTacticsPageRegion,
        "TACTICS",
        "Tactical combat commands",
        "tactics"},
    CombatPageDescriptor{
        CombatActionPage::special,
        kCombatSpecialPageRegion,
        "SPECIAL",
        "Special combat commands",
        "special"},
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
  const bool utility_combat_page =
      request.combat_action_page == CombatActionPage::utility;
  const bool special_combat_page =
      request.combat_action_page == CombatActionPage::special;
  if (!primary_combat_page && !secondary_combat_page &&
      !utility_combat_page && !special_combat_page) {
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
  const bool valid_center_previous = request.center_previous_combatant &&
      valid_combatant(*request.center_previous_combatant);
  const bool valid_center_next = request.center_next_combatant &&
      valid_combatant(*request.center_next_combatant);
  const std::optional<CombatantId> combat_items_combatant =
      request.combat_items
      ? std::optional<CombatantId>{request.combat_items->combatant}
      : std::nullopt;
  const bool valid_combat_items = request.combat_items &&
      valid_combatant(request.combat_items->combatant);
  const bool valid_auto_combatant = request.auto_combatant &&
      valid_combatant(*request.auto_combatant);
  const bool valid_show_combat_range = request.show_combat_range_combatant &&
      valid_combatant(*request.show_combat_range_combatant);
  const bool valid_bandage_combatant = request.bandage_combatant &&
      valid_combatant(*request.bandage_combatant);
  const bool valid_undo_combatant = request.undo_combatant &&
      valid_combatant(*request.undo_combatant);
  const bool valid_open_combat_spellbook = request.open_combat_spellbook &&
      valid_combatant(*request.open_combat_spellbook);
  const bool valid_open_combat_targeting = request.open_combat_targeting &&
      valid_combatant(*request.open_combat_targeting);
  const bool valid_escape_combat = request.escape_combat &&
      valid_combatant(*request.escape_combat);
  const bool valid_open_combat_scroll_case = request.open_combat_scroll_case &&
      valid_combatant(*request.open_combat_scroll_case);
  const std::optional<CombatantId> center_combat_cursor_combatant =
      request.center_combat_cursor
      ? std::optional<CombatantId>{
            request.center_combat_cursor->combatant}
      : std::nullopt;
  const bool valid_center_combat_cursor = request.center_combat_cursor &&
      valid_combatant(request.center_combat_cursor->combatant) &&
      request.center_combat_cursor->cell.x <= 89U &&
      request.center_combat_cursor->cell.y <= 89U;
  const std::array combatants{
      request.guard_combatant,
      request.finish_combatant,
      request.delay_combatant,
      request.center_active_combatant,
      request.switch_weapon_combatant,
      request.center_previous_combatant,
      request.center_next_combatant,
      combat_items_combatant,
      request.auto_combatant,
      request.show_combat_range_combatant,
      request.bandage_combatant,
      request.undo_combatant,
      request.open_combat_spellbook,
      request.open_combat_targeting,
      request.escape_combat,
      request.open_combat_scroll_case,
      center_combat_cursor_combatant,
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
  const size_t secondary_combat_control_count =
      (request.switch_weapon_combatant ? 1U : 0U) +
      (request.center_previous_combatant ? 1U : 0U) +
      (request.center_next_combatant ? 1U : 0U) +
      (request.combat_items ? 1U : 0U);
  const size_t utility_combat_control_count =
      (request.auto_combatant ? 1U : 0U) +
      (request.show_combat_range_combatant ? 1U : 0U) +
      (request.bandage_combatant ? 1U : 0U) +
      (request.undo_combatant ? 1U : 0U);
  const size_t special_combat_control_count =
      (request.open_combat_spellbook ? 1U : 0U) +
      (request.open_combat_targeting ? 1U : 0U) +
      (request.escape_combat ? 1U : 0U) +
      (request.open_combat_scroll_case ? 1U : 0U) +
      (request.center_combat_cursor ? 1U : 0U);
  const size_t combat_control_count = primary_combat_page
      ? primary_combat_control_count
      : (secondary_combat_page ? secondary_combat_control_count
          : (utility_combat_page ? utility_combat_control_count
                                 : special_combat_control_count));
  const size_t total_combat_control_count =
      primary_combat_control_count + secondary_combat_control_count +
      utility_combat_control_count + special_combat_control_count;
  const size_t maximum_combat_control_count = std::max({
      primary_combat_control_count,
      secondary_combat_control_count,
      utility_combat_control_count,
      special_combat_control_count,
  });
  constexpr size_t combat_page_control_count = kCombatPages.size();
  const size_t required_combat_control_capacity = std::max(
      combat_page_control_count,
      maximum_combat_control_count);
  const bool has_combatant_request = std::ranges::any_of(
      combatants,
      [](const auto& combatant) { return combatant.has_value(); });
  const bool combat_controls =
      (request.screen == ScreenContext::combat) &&
      !invalid_combatant && !mismatched_combatants &&
      (total_combat_control_count > 0U);
  const bool has_combat_request = has_combatant_request ||
      request.guard_available || request.finish_available ||
      request.delay_available || request.center_active_available ||
      request.switch_weapon_available || request.center_previous_available ||
      request.center_next_available || request.combat_items ||
      request.combat_items_available || request.auto_combatant ||
      request.auto_combatant_available ||
      request.show_combat_range_combatant ||
      request.show_combat_range_available || request.bandage_combatant ||
      request.bandage_combatant_available || request.undo_combatant ||
      request.undo_combatant_available || secondary_combat_page ||
      request.open_combat_spellbook ||
      request.open_combat_spellbook_available ||
      request.open_combat_targeting ||
      request.open_combat_targeting_available || request.escape_combat ||
      request.escape_combat_available || request.open_combat_scroll_case ||
      request.open_combat_scroll_case_available ||
      request.center_combat_cursor ||
      request.center_combat_cursor_available || utility_combat_page ||
      special_combat_page;
  if ((!world_controls && !combat_controls) ||
      (world_controls && has_combat_request) ||
      (combat_controls &&
          (request.navigation_available || request.inventory_member ||
              request.spellbook_member || request.scroll_case_member ||
              request.save_control_visible || request.load_control_visible)) ||
      (request.inventory_available && !request.inventory_member) ||
      (request.spellbook_available && !request.spellbook_member) ||
      (request.scroll_case_available && !request.scroll_case_member) ||
      (request.save_available && !request.save_control_visible) ||
      (request.load_available && !request.load_control_visible) ||
      (request.guard_available && !valid_guard) ||
      (request.finish_available && !valid_finish) ||
      (request.delay_available && !valid_delay) ||
      (request.center_active_available && !valid_center) ||
      (request.switch_weapon_available && !valid_switch_weapon) ||
      (request.center_previous_available && !valid_center_previous) ||
      (request.center_next_available && !valid_center_next) ||
      (request.combat_items && !valid_combat_items) ||
      (request.combat_items_available && !valid_combat_items) ||
      (request.auto_combatant_available && !valid_auto_combatant) ||
      (request.show_combat_range_available && !valid_show_combat_range) ||
      (request.bandage_combatant_available && !valid_bandage_combatant) ||
      (request.undo_combatant_available && !valid_undo_combatant) ||
      (request.open_combat_spellbook_available &&
          !valid_open_combat_spellbook) ||
      (request.open_combat_targeting_available &&
          !valid_open_combat_targeting) ||
      (request.escape_combat_available && !valid_escape_combat) ||
      (request.open_combat_scroll_case_available &&
          !valid_open_combat_scroll_case) ||
      (request.center_combat_cursor && !valid_center_combat_cursor) ||
      (request.center_combat_cursor_available &&
          !valid_center_combat_cursor)) {
    return {};
  }

  const size_t control_count = combat_controls
      ? combat_control_count
      : descriptors.size() +
          (request.inventory_member ? 1U : 0U) +
          (request.spellbook_member ? 1U : 0U) +
          (request.scroll_case_member ? 1U : 0U) +
          (request.save_control_visible ? 1U : 0U) +
          (request.load_control_visible ? 1U : 0U);

  const double available_width =
      request.action_panel.width - 2.0 * kHorizontalInset;
  const double available_height = request.action_panel.height -
      kControlsTopInset - kBottomInset;
  const double gap = std::clamp(available_width * 0.008, 6.0, 10.0);
  double button_width = 0.0;
  const double button_height = std::min(52.0, available_height);
  if (control_count > 0U) {
    const double computed_button_width =
        (available_width - gap * (control_count - 1U)) /
        control_count;
    button_width = combat_controls
        ? std::min(160.0, computed_button_width)
        : computed_button_width;
    if (!std::isfinite(button_width) || !std::isfinite(button_height) ||
        (button_width < kMinimumTargetExtent) ||
        (button_height < kMinimumTargetExtent)) {
      return {};
    }
  }
  // Every combat tab is a live destination. Accept the deck only when the
  // panel can contain both the persistent tab row and the largest reachable
  // action row; otherwise a valid page selection could make the deck vanish.
  if (combat_controls && (required_combat_control_capacity > 0U) &&
      ((request.action_panel.width <
              2.0 * kHorizontalInset +
                  kMinimumTargetExtent *
                      required_combat_control_capacity +
                  gap * (required_combat_control_capacity - 1U)) ||
          (request.action_panel.height <
              kControlsTopInset + kMinimumTargetExtent + kBottomInset))) {
    return {};
  }

  std::vector<ShellControlPlacement> result;
  result.reserve(control_count + combat_page_control_count);
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
  if (request.scroll_case_member) {
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{kOpenScrollCaseRegion},
        .kind = ShellControlKind::open_scroll_case,
        .bounds = {x, y, button_width, button_height},
        .label = "SCROLL",
        .accessibility_label = "Use scroll",
        .focus_identifier = "focus.action.scroll_case.open",
        .tab_order = 1108,
        .enabled = request.scroll_case_available,
        .payload = OpenScrollCaseAction{*request.scroll_case_member},
    });
    x += button_width + gap;
  }
  if (!combat_controls) {
    return result;
  }

  const double combat_page_width = std::min(
      112.0,
      (available_width - gap * (combat_page_control_count - 1U)) /
          combat_page_control_count);
  if (!std::isfinite(combat_page_width) ||
      (combat_page_width < kMinimumTargetExtent)) {
    return {};
  }
  double combat_page_x = request.action_panel.x + kHorizontalInset;
  for (size_t index = 0; index < kCombatPages.size(); ++index) {
    const auto& descriptor = kCombatPages[index];
    if (!is_valid_combat_action_page_transition(
            request.combat_action_page, descriptor.page)) {
      return {};
    }
    const bool selected = request.combat_action_page == descriptor.page;
    std::string accessibility_label =
        std::string(descriptor.accessibility_label) + " tab";
    if (selected) {
      accessibility_label += ", selected";
    }
    result.emplace_back(ShellControlPlacement{
        .region = ShellRegionId{descriptor.region},
        .kind = ShellControlKind::combat_action_page,
        .bounds = {
            combat_page_x,
            request.action_panel.y + kHeaderTopInset,
            combat_page_width,
            kMinimumTargetExtent,
        },
        .label = std::string(descriptor.label),
        .accessibility_label = std::move(accessibility_label),
        .focus_identifier =
            "focus.action.combat.page." + std::string(descriptor.identifier),
        .tab_order = 1100 + static_cast<int32_t>(index),
        .enabled = true,
        .selected = selected,
        .payload = SetCombatActionPageAction{descriptor.page},
    });
    combat_page_x += combat_page_width + gap;
  }

  if (special_combat_page) {
    if (request.open_combat_spellbook) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kOpenCombatSpellbookRegion},
          .kind = ShellControlKind::open_combat_spellbook,
          .bounds = {x, y, button_width, button_height},
          .label = "CAST",
          .accessibility_label = "Open combat spell chooser",
          .focus_identifier = "focus.action.combat.spellbook.open",
          .tab_order = 1119,
          .enabled = request.open_combat_spellbook_available,
          .payload = OpenCombatSpellbookAction{
              *request.open_combat_spellbook},
      });
      x += button_width + gap;
    }
    if (request.open_combat_targeting) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kOpenCombatTargetingRegion},
          .kind = ShellControlKind::open_combat_targeting,
          .bounds = {x, y, button_width, button_height},
          .label = "TARGET",
          .accessibility_label = "Begin combat targeting",
          .focus_identifier = "focus.action.combat.targeting.open",
          .tab_order = 1120,
          .enabled = request.open_combat_targeting_available,
          .payload = OpenCombatTargetingAction{
              *request.open_combat_targeting},
      });
      x += button_width + gap;
    }
    if (request.escape_combat) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kEscapeCombatRegion},
          .kind = ShellControlKind::escape_combat,
          .bounds = {x, y, button_width, button_height},
          .label = "ESCAPE",
          .accessibility_label = "Attempt to escape combat",
          .focus_identifier = "focus.action.combat.escape",
          .tab_order = 1121,
          .enabled = request.escape_combat_available,
          .payload = EscapeCombatAction{*request.escape_combat},
      });
      x += button_width + gap;
    }
    if (request.open_combat_scroll_case) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kOpenCombatScrollCaseRegion},
          .kind = ShellControlKind::open_combat_scroll_case,
          .bounds = {x, y, button_width, button_height},
          .label = "SCROLL",
          .accessibility_label = "Open combat scroll chooser",
          .focus_identifier = "focus.action.combat.scroll_case.open",
          .tab_order = 1122,
          .enabled = request.open_combat_scroll_case_available,
          .payload = OpenCombatScrollCaseAction{
              *request.open_combat_scroll_case},
      });
      x += button_width + gap;
    }
    if (request.center_combat_cursor) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kCenterCombatCursorRegion},
          .kind = ShellControlKind::center_combat_cursor,
          .bounds = {x, y, button_width, button_height},
          .label = "CURSOR",
          .accessibility_label = "Center combat view on cursor",
          .focus_identifier = "focus.action.combat.center.cursor",
          .tab_order = 1123,
          .enabled = request.center_combat_cursor_available,
          .payload = *request.center_combat_cursor,
      });
    }
    return result;
  }

  if (utility_combat_page) {
    if (request.auto_combatant) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kAutoCombatantRegion},
          .kind = ShellControlKind::auto_combatant,
          .bounds = {x, y, button_width, button_height},
          .label = "AUTO",
          .accessibility_label = "Auto-play active combatant's turn",
          .focus_identifier = "focus.action.combat.auto",
          .tab_order = 1114,
          .enabled = request.auto_combatant_available,
          .payload = AutoCombatantAction{*request.auto_combatant},
      });
      x += button_width + gap;
    }
    if (request.show_combat_range_combatant) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kShowCombatRangeRegion},
          .kind = ShellControlKind::show_combat_range,
          .bounds = {x, y, button_width, button_height},
          .label = "RANGE",
          .accessibility_label = "Show combat ranges; press any key to close",
          .focus_identifier = "focus.action.combat.range",
          .tab_order = 1115,
          .enabled = request.show_combat_range_available,
          .payload = ShowCombatRangeAction{
              *request.show_combat_range_combatant},
      });
      x += button_width + gap;
    }
    if (request.bandage_combatant) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kBandageCombatantRegion},
          .kind = ShellControlKind::bandage_combatant,
          .bounds = {x, y, button_width, button_height},
          .label = "BANDAGE",
          .accessibility_label = "Choose a party member to bandage",
          .focus_identifier = "focus.action.combat.bandage",
          .tab_order = 1116,
          .enabled = request.bandage_combatant_available,
          .payload = BandageCombatantAction{*request.bandage_combatant},
      });
      x += button_width + gap;
    }
    if (request.undo_combatant) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kUndoCombatantRegion},
          .kind = ShellControlKind::undo_combatant,
          .bounds = {x, y, button_width, button_height},
          .label = "UNDO",
          .accessibility_label = "Undo active combatant's movement",
          .focus_identifier = "focus.action.combat.undo",
          .tab_order = 1117,
          .enabled = request.undo_combatant_available,
          .payload = UndoCombatantAction{*request.undo_combatant},
      });
    }
    return result;
  }

  if (secondary_combat_page) {
    if (request.switch_weapon_combatant) {
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
      x += button_width + gap;
    }
    if (request.center_previous_combatant) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kCenterPreviousCombatantRegion},
          .kind = ShellControlKind::cycle_combat_focus,
          .bounds = {x, y, button_width, button_height},
          .label = "PREV",
          .accessibility_label = "Center view on previous combatant",
          .focus_identifier = "focus.action.combat.center.previous",
          .tab_order = 1110,
          .enabled = request.center_previous_available,
          .payload = CycleCombatFocusAction{
              *request.center_previous_combatant,
              CombatFocusDirection::previous},
      });
      x += button_width + gap;
    }
    if (request.center_next_combatant) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kCenterNextCombatantRegion},
          .kind = ShellControlKind::cycle_combat_focus,
          .bounds = {x, y, button_width, button_height},
          .label = "NEXT",
          .accessibility_label = "Center view on next combatant",
          .focus_identifier = "focus.action.combat.center.next",
          .tab_order = 1111,
          .enabled = request.center_next_available,
          .payload = CycleCombatFocusAction{
              *request.center_next_combatant,
              CombatFocusDirection::next},
      });
      x += button_width + gap;
    }
    if (request.combat_items) {
      result.emplace_back(ShellControlPlacement{
          .region = ShellRegionId{kOpenCombatItemsRegion},
          .kind = ShellControlKind::open_combat_items,
          .bounds = {x, y, button_width, button_height},
          .label = "ITEMS",
          .accessibility_label = "Open combat items",
          .focus_identifier = "focus.action.combat.items",
          .tab_order = 1112,
          .enabled = request.combat_items_available,
          .payload = *request.combat_items,
      });
    }
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
  return result;
}

} // namespace realmz::presentation
