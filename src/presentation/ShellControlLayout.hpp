#pragma once

#include <optional>
#include <string>
#include <vector>

#include "RemasteredInputMapper.hpp"
#include "UIAction.hpp"

namespace realmz::presentation {

enum class ShellControlKind {
  movement,
  party_member,
  open_inventory,
  open_spellbook,
  open_save_game,
  open_load_game,
  guard_combatant,
  finish_combatant,
  delay_combatant,
  center_active_combatant,
  combat_action_page,
  switch_weapon_set,
  cycle_combat_focus,
  open_combat_items,
  auto_combatant,
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
  UIActionPayload payload;

  bool operator==(const ShellControlPlacement&) const = default;
};

struct ShellControlLayoutRequest {
  ScreenContext screen = ScreenContext::title;
  WorldPresentation world_presentation = WorldPresentation::none;
  LogicalRect action_panel;
  bool navigation_available = false;
  std::optional<PartyMemberId> inventory_member;
  bool inventory_available = false;
  std::optional<PartyMemberId> spellbook_member;
  bool spellbook_available = false;
  bool save_control_visible = false;
  bool save_available = false;
  bool load_control_visible = false;
  bool load_available = false;
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
};

// Produces code-native controls within the action bar. Empty output is a
// fail-closed result: the complete Classic frame remains the only interaction
// surface for any command that lacks a typed, live handler.
[[nodiscard]] std::vector<ShellControlPlacement>
compute_shell_control_layout(const ShellControlLayoutRequest& request);

} // namespace realmz::presentation
