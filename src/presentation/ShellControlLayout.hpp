#pragma once

#include <optional>
#include <string>
#include <vector>

#include "RemasteredInputMapper.hpp"
#include "UIAction.hpp"

namespace realmz::presentation {

enum class ShellControlKind {
  world_action_page,
  movement,
  party_member,
  open_inventory,
  open_spellbook,
  open_scroll_case,
  open_character_sheet,
  open_save_game,
  open_load_game,
  rest_party,
  guard_combatant,
  finish_combatant,
  delay_combatant,
  center_active_combatant,
  combat_action_page,
  switch_weapon_set,
  cycle_combat_focus,
  open_combat_items,
  auto_combatant,
  show_combat_range,
  bandage_combatant,
  undo_combatant,
  open_combat_spellbook,
  open_combat_targeting,
  escape_combat,
  open_combat_scroll_case,
  center_combat_cursor,
  drawer_tab,
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
  // Selected is orthogonal to enabled: the active action-deck tab remains an
  // operable, focusable idempotent target and is rendered as the current page.
  bool selected = false;
  UIActionPayload payload;

  bool operator==(const ShellControlPlacement&) const = default;
};

struct ShellControlLayoutRequest {
  ScreenContext screen = ScreenContext::title;
  WorldPresentation world_presentation = WorldPresentation::none;
  LogicalRect action_panel;
  WorldActionPage world_action_page = WorldActionPage::travel;
  bool navigation_available = false;
  std::optional<PartyMemberId> inventory_member;
  bool inventory_available = false;
  std::optional<PartyMemberId> spellbook_member;
  bool spellbook_available = false;
  std::optional<PartyMemberId> scroll_case_member;
  bool scroll_case_available = false;
  std::optional<PartyMemberId> character_sheet_member;
  bool character_sheet_available = false;
  bool save_control_visible = false;
  bool save_available = false;
  bool load_control_visible = false;
  bool load_available = false;
  bool rest_control_visible = false;
  bool rest_available = false;
  std::optional<CombatantId> guard_combatant;
  bool guard_available = false;
  std::optional<CombatantId> finish_combatant;
  bool finish_available = false;
  std::optional<CombatantId> delay_combatant;
  bool delay_available = false;
  std::optional<CombatantId> center_active_combatant;
  bool center_active_available = false;
  CombatActionPage combat_action_page = CombatActionPage::primary;
  std::optional<CombatantId> switch_weapon_combatant;
  bool switch_weapon_available = false;
  std::optional<CombatantId> center_previous_combatant;
  bool center_previous_available = false;
  std::optional<CombatantId> center_next_combatant;
  bool center_next_available = false;
  std::optional<OpenCombatItemsAction> combat_items;
  bool combat_items_available = false;
  std::optional<CombatantId> auto_combatant;
  bool auto_combatant_available = false;
  std::optional<CombatantId> show_combat_range_combatant;
  bool show_combat_range_available = false;
  std::optional<CombatantId> bandage_combatant;
  bool bandage_combatant_available = false;
  std::optional<CombatantId> undo_combatant;
  bool undo_combatant_available = false;
  std::optional<CombatantId> open_combat_spellbook;
  bool open_combat_spellbook_available = false;
  std::optional<CombatantId> open_combat_targeting;
  bool open_combat_targeting_available = false;
  std::optional<CombatantId> escape_combat;
  bool escape_combat_available = false;
  std::optional<CombatantId> open_combat_scroll_case;
  bool open_combat_scroll_case_available = false;
  std::optional<CenterCombatCursorAction> center_combat_cursor;
  bool center_combat_cursor_available = false;
};

// Produces code-native controls within the action bar. Empty output is a
// fail-closed result: the complete Classic frame remains the only interaction
// surface for any command that lacks a typed, live handler.
[[nodiscard]] std::vector<ShellControlPlacement>
compute_shell_control_layout(const ShellControlLayoutRequest& request);

} // namespace realmz::presentation
