#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "GameEvent.hpp"

namespace realmz::presentation {

// This file contains presentation values only. It deliberately has no renderer,
// SDL, resource, or legacy-engine dependency and cannot dispatch a UIAction.

enum class StateEmphasis {
  neutral,
  information,
  positive,
  caution,
  critical,
  inactive,
};

// A renderer must expose the label and/or marker; color alone is not a state
// signal. The marker is semantic so each renderer can draw it in code.
enum class StateMarker {
  none,
  selection,
  information,
  check,
  alert,
  stop,
  unavailable,
  condition,
};

struct StateTokenModel {
  std::string identifier;
  std::string label;
  StateEmphasis emphasis = StateEmphasis::neutral;
  StateMarker marker = StateMarker::none;

  bool operator==(const StateTokenModel&) const = default;
};

struct MeterModel {
  int32_t current = 0;
  int32_t maximum = 0;
  double fill_fraction = 0.0;
  StateTokenModel state;

  bool operator==(const MeterModel&) const = default;
};

// Stable semantic command identifiers are intentionally separate from physical
// keys. A later input layer may remap keys without changing this model.
using CommandIdentifier = std::string;
using FocusIdentifier = std::string;

struct PartyRailMemberModel {
  PartyMemberId id = 0;
  std::string name;
  int16_t level = 0;
  int32_t portrait_id = 0;
  MeterModel stamina;
  MeterModel spell_points;
  std::vector<StateTokenModel> states;
  bool selected = false;
  bool conscious = true;
  FocusIdentifier focus_identifier;
  CommandIdentifier select_command;
  int32_t tab_order = 0;

  bool operator==(const PartyRailMemberModel&) const = default;
};

struct PartyRailModel {
  SnapshotRevision revision = 0;
  std::vector<PartyRailMemberModel> members;
  std::optional<PartyMemberId> selected_member;
  std::array<int32_t, 3> pooled_money{};
  int16_t fatigue = 0;
  StateTokenModel fatigue_state;

  bool operator==(const PartyRailModel&) const = default;
};

struct SelectedPartyDetailsModel {
  std::optional<PartyMemberId> member;
  std::string name;
  int16_t level = 0;
  int16_t armor_class = 0;
  int16_t movement = 0;
  int16_t movement_maximum = 0;
  MeterModel stamina;
  MeterModel spell_points;
  // This is the complete, unelided semantic state sequence for the selected
  // member. Its first token is always the explicit Conscious/Unconscious cue;
  // redundant selection state is omitted. Conditions use deterministic
  // ascending code order with duplicate condition codes removed. Presentation
  // layouts may elide only their own visible summary, never this source.
  std::vector<StateTokenModel> states;
  bool conscious = true;

  bool operator==(const SelectedPartyDetailsModel&) const = default;
};

enum class ActionIntent {
  navigate,
  open_inventory,
  cast_spell,
  open_scroll_case,
  open_character_sheet,
  save_game,
  load_game,
  rest,
  guard,
  finish,
  delay,
  center_active,
  switch_weapon,
  center_previous,
  center_next,
  combat_items,
  auto_combatant,
  show_combat_range,
  bandage_combatant,
  undo_combatant,
  open_combat_spellbook,
  open_combat_targeting,
  escape_combat,
  open_combat_scroll_case,
  center_combat_cursor,
  cancel,
  encounter_choice,
};

// "deferred_to_engine" means that the snapshot satisfies the prerequisites
// available to this read-only model, but the legacy rules remain authoritative.
enum class ActionAvailability {
  available,
  unavailable,
  deferred_to_engine,
};

struct ActionControlModel {
  ActionIntent intent = ActionIntent::navigate;
  CommandIdentifier command;
  std::string label;
  ActionAvailability availability = ActionAvailability::unavailable;
  std::optional<StateTokenModel> availability_reason;
  std::optional<PartyMemberId> party_member;
  std::optional<CombatantId> combatant;
  std::optional<int32_t> encounter_choice;
  FocusIdentifier focus_identifier;
  int32_t tab_order = 0;

  bool operator==(const ActionControlModel&) const = default;

  [[nodiscard]] bool can_invoke() const noexcept {
    return this->availability != ActionAvailability::unavailable;
  }
};

struct EventLogEntryModel {
  EventSequence sequence = 0;
  MessageSeverity severity = MessageSeverity::information;
  std::string text;
  StateTokenModel state;

  bool operator==(const EventLogEntryModel&) const = default;
};

struct EventLogModel {
  std::vector<EventLogEntryModel> entries;
  size_t caution_count = 0;
  size_t critical_count = 0;

  bool operator==(const EventLogModel&) const = default;
};

struct DrawerTabModel {
  DrawerPanel panel = DrawerPanel::details;
  std::string label;
  CommandIdentifier command;
  FocusIdentifier focus_identifier;
  int32_t tab_order = 0;
  size_t badge_count = 0;
  bool active = false;

  bool operator==(const DrawerTabModel&) const = default;
};

struct DrawerModel {
  bool collapsed = false;
  std::optional<DrawerPanel> active_panel;
  std::vector<DrawerTabModel> tabs;

  bool operator==(const DrawerModel&) const = default;
};

struct TextStyleModel {
  double point_size = 0.0;
  double line_height = 0.0;

  bool operator==(const TextStyleModel&) const = default;
};

struct TypographyModel {
  double scale = 1.0;
  TextStyleModel caption;
  TextStyleModel body;
  TextStyleModel heading;

  bool operator==(const TypographyModel&) const = default;
};

struct MotionModel {
  bool reduced_motion = false;
  bool allow_nonessential_motion = true;

  bool operator==(const MotionModel&) const = default;
};

struct AnimationCueModel {
  EventSequence sequence = 0;
  std::string cue;
  ActionTarget target;
  bool essential_motion = false;
  bool should_animate = true;

  bool operator==(const AnimationCueModel&) const = default;
};

struct KeyboardTargetModel {
  FocusIdentifier focus_identifier;
  int32_t tab_order = 0;
  CommandIdentifier command;
  bool enabled = true;

  bool operator==(const KeyboardTargetModel&) const = default;
};

struct ShellViewPreferences {
  double text_scale = 1.0;
  bool reduced_motion = false;
  bool panels_collapsed = false;
  std::optional<DrawerPanel> active_drawer;
  WorldActionPage world_action_page = WorldActionPage::travel;
  CombatActionPage combat_action_page = CombatActionPage::primary;
  size_t event_log_limit = 100;

  bool operator==(const ShellViewPreferences&) const = default;
};

struct PresentationShellModel {
  SnapshotRevision revision = 0;
  ScreenContext screen = ScreenContext::title;
  WorldActionPage world_action_page = WorldActionPage::travel;
  CombatActionPage combat_action_page = CombatActionPage::primary;
  PartyRailModel party_rail;
  SelectedPartyDetailsModel selected_details;
  std::vector<ActionControlModel> actions;
  EventLogModel event_log;
  DrawerModel drawers;
  TypographyModel typography;
  MotionModel motion;
  std::vector<AnimationCueModel> animation_cues;
  std::vector<KeyboardTargetModel> keyboard_tab_order;

  bool operator==(const PresentationShellModel&) const = default;
};

[[nodiscard]] PartyRailModel build_party_rail_model(
    const GameSnapshot& snapshot);

// The model is deterministic for identical values. Message events are ordered
// by sequence (stable for ties), while non-message events never become user
// prose. The input snapshot and events are only read.
[[nodiscard]] PresentationShellModel build_presentation_shell_model(
    const GameSnapshot& snapshot,
    std::span<const GameEvent> events = {},
    const ShellViewPreferences& preferences = {});

} // namespace realmz::presentation
