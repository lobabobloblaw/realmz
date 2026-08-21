#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "presentation/LegacyGameSnapshotSource.hpp"
#include "presentation/LegacyPresentationContext.h"
#include "presentation/RuntimeLegacyCommandBridge.hpp"
#include "presentation/SemanticInputBoundary.h"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

constexpr CombatantId kActingCombatant = 2;
constexpr PartyMemberId kSelectedMember = 4;
constexpr PartyMemberId kReplacementSelectedMember = 5;
constexpr CombatFieldCell kCursorCell{.x = 42, .y = 17};
constexpr uint32_t kUnchangedClassicMessage = 0xA5A5A5A5U;

constexpr RuntimeLegacyCommandContext kRuntimeContext{
    .screen = ScreenContext::combat,
    .world_presentation = WorldPresentation::none,
    .adaptive_eligible = true,
};

enum class CombatCommand {
  guard,
  finish,
  delay,
  center,
  weapon,
  previous,
  next,
  auto_combatant,
  center_combat_cursor,
  show_combat_range,
  bandage_combatant,
  undo_combatant,
  open_combat_spellbook,
  open_combat_targeting,
  escape_combat,
  open_combat_scroll_case,
  items,
};

struct CombatCase {
  CombatCommand command = CombatCommand::guard;
  std::string_view label;
  uint32_t expected_classic_message = 0;
  uint32_t expected_semantic_tag = 0;
  std::optional<CombatFocusDirection> direction;
  std::optional<PartyMemberId> selected_member;
  std::optional<CombatFieldCell> cell;
};

constexpr std::array kCombatCases{
    CombatCase{
        .command = CombatCommand::guard,
        .label = "Guard",
        .expected_classic_message = 0x00000567U,
        .expected_semantic_tag = 0x52470302U,
    },
    CombatCase{
        .command = CombatCommand::finish,
        .label = "Finish",
        .expected_classic_message = 0x00000366U,
        .expected_semantic_tag = 0x52460302U,
    },
    CombatCase{
        .command = CombatCommand::delay,
        .label = "Delay",
        .expected_classic_message = 0x00000264U,
        .expected_semantic_tag = 0x52440302U,
    },
    CombatCase{
        .command = CombatCommand::center,
        .label = "Center",
        .expected_classic_message = 0x00000863U,
        .expected_semantic_tag = 0x52430302U,
    },
    CombatCase{
        .command = CombatCommand::weapon,
        .label = "Weapon",
        .expected_classic_message = 0x00000D77U,
        .expected_semantic_tag = 0x52570302U,
    },
    CombatCase{
        .command = CombatCommand::previous,
        .label = "Previous",
        .expected_classic_message = 0x00002370U,
        .expected_semantic_tag = 0x52420302U,
        .direction = CombatFocusDirection::previous,
    },
    CombatCase{
        .command = CombatCommand::next,
        .label = "Next",
        .expected_classic_message = 0x00002D6EU,
        .expected_semantic_tag = 0x524E0302U,
        .direction = CombatFocusDirection::next,
    },
    CombatCase{
        .command = CombatCommand::auto_combatant,
        .label = "Auto",
        .expected_classic_message = 0x00000061U,
        .expected_semantic_tag = 0x52410302U,
    },
    CombatCase{
        .command = CombatCommand::center_combat_cursor,
        .label = "Center Cursor",
        .expected_classic_message = 0x00002E6DU,
        .expected_semantic_tag = 0x4D022A11U,
        .cell = kCursorCell,
    },
    CombatCase{
        .command = CombatCommand::show_combat_range,
        .label = "Range",
        .expected_classic_message = 0x00000F72U,
        .expected_semantic_tag = 0x52520302U,
    },
    CombatCase{
        .command = CombatCommand::bandage_combatant,
        .label = "Bandage",
        .expected_classic_message = 0x00000B62U,
        .expected_semantic_tag = 0x52480302U,
    },
    CombatCase{
        .command = CombatCommand::undo_combatant,
        .label = "Undo",
        .expected_classic_message = 0x00002075U,
        .expected_semantic_tag = 0x52550302U,
    },
    CombatCase{
        .command = CombatCommand::open_combat_spellbook,
        .label = "Cast Spell",
        .expected_classic_message = 0x00000173U,
        .expected_semantic_tag = 0x53430302U,
    },
    CombatCase{
        .command = CombatCommand::open_combat_targeting,
        .label = "Combat Target",
        .expected_classic_message = 0x00001174U,
        .expected_semantic_tag = 0x52540302U,
    },
    CombatCase{
        .command = CombatCommand::escape_combat,
        .label = "Escape Combat",
        .expected_classic_message = 0x00000E65U,
        .expected_semantic_tag = 0x52450302U,
    },
    CombatCase{
        .command = CombatCommand::open_combat_scroll_case,
        .label = "Use Scroll",
        .expected_classic_message = 0x0000256CU,
        .expected_semantic_tag = 0x55530302U,
    },
    CombatCase{
        .command = CombatCommand::items,
        .label = "Combat Items",
        .expected_classic_message = 0x00002269U,
        .expected_semantic_tag = 0x49030204U,
        .selected_member = kSelectedMember,
    },
};

struct ClassicRequestTrace {
  CombatCommand command = CombatCommand::guard;
  CombatantId actor = 0;
  std::optional<CombatFocusDirection> direction;
  std::optional<PartyMemberId> selected_member;
  std::optional<CombatFieldCell> cell;
  uint32_t key_message = 0;
  RuntimeLegacyCommandContext context;

  bool operator==(const ClassicRequestTrace&) const = default;
};

struct QueuedSemanticTrace {
  ClassicRequestTrace classic_request;
  uint32_t semantic_tag = 0;

  bool operator==(const QueuedSemanticTrace&) const = default;
};

// These bytes are a bounded, test-owned pre-Classic state image. They are not
// Realmz save-file bytes and do not model a Classic combat mutation. The real
// semantic boundary sees only a detached GameSnapshot projected from them.
enum class StateByte : std::size_t {
  magic_r,
  magic_z,
  magic_c,
  magic_e,
  snapshot_is_combat,
  combat_present,
  combat_active,
  bandage_available,
  undo_available,
  cast_spell_available,
  target_available,
  use_scroll_available,
  acting_combatant,
  actor_party_member_present,
  actor_combatant_present,
  actor_active,
  actor_targetable,
  actor_stamina,
  actor_movement,
  actor_movement_maximum,
  field_origin_x,
  field_origin_y,
  visible_columns,
  visible_rows,
  selected_member,
  canary_0,
  canary_1,
  count,
};

using PreClassicStateBytes =
    std::array<uint8_t, static_cast<std::size_t>(StateByte::count)>;

PreClassicStateBytes pre_classic_state{};
RealmzLegacyPresentationContext captured_legacy_context{};
int legacy_context_capture_calls = 0;
int snapshot_capture_calls = 0;

[[nodiscard]] constexpr std::size_t offset(StateByte byte) noexcept {
  return static_cast<std::size_t>(byte);
}

[[nodiscard]] uint8_t state(StateByte byte) noexcept {
  return pre_classic_state[offset(byte)];
}

void set_state(StateByte byte, uint8_t value) noexcept {
  pre_classic_state[offset(byte)] = value;
}

void reset_fixture_state() {
  RealmzInvalidateSemanticInputBoundary();
  pre_classic_state.fill(0);
  set_state(StateByte::magic_r, static_cast<uint8_t>('R'));
  set_state(StateByte::magic_z, static_cast<uint8_t>('Z'));
  set_state(StateByte::magic_c, static_cast<uint8_t>('C'));
  set_state(StateByte::magic_e, static_cast<uint8_t>('E'));
  set_state(StateByte::snapshot_is_combat, 1);
  set_state(StateByte::combat_present, 1);
  set_state(StateByte::combat_active, 1);
  set_state(StateByte::bandage_available, 1);
  set_state(StateByte::undo_available, 1);
  set_state(StateByte::cast_spell_available, 1);
  set_state(StateByte::target_available, 1);
  set_state(StateByte::use_scroll_available, 1);
  set_state(
      StateByte::acting_combatant,
      static_cast<uint8_t>(kActingCombatant));
  set_state(StateByte::actor_party_member_present, 1);
  set_state(StateByte::actor_combatant_present, 1);
  set_state(StateByte::actor_active, 1);
  set_state(StateByte::actor_targetable, 1);
  set_state(StateByte::actor_stamina, 12);
  set_state(StateByte::actor_movement, 9);
  set_state(StateByte::actor_movement_maximum, 9);
  set_state(StateByte::field_origin_x, 70);
  set_state(StateByte::field_origin_y, 60);
  set_state(StateByte::visible_columns, 15);
  set_state(StateByte::visible_rows, 13);
  set_state(StateByte::selected_member, kSelectedMember);
  set_state(StateByte::canary_0, 0x5A);
  set_state(StateByte::canary_1, 0xC3);

  captured_legacy_context = {
      .screen = REALMZ_LEGACY_SCREEN_COMBAT,
      .gameplay_window_active = 1,
      .adaptive_eligible = 1,
      .requires_full_frame = 0,
  };
  legacy_context_capture_calls = 0;
  snapshot_capture_calls = 0;
}

[[nodiscard]] PartyMemberView party_member(
    PartyMemberId id,
    std::string name,
    bool selected) {
  return PartyMemberView{
      .id = id,
      .name = std::move(name),
      .stamina = {12, 20},
      .movement = static_cast<int16_t>(state(StateByte::actor_movement)),
      .movement_maximum =
          static_cast<int16_t>(state(StateByte::actor_movement_maximum)),
      .selected = selected,
      .conscious = true,
  };
}

[[nodiscard]] GameSnapshot snapshot_from_pre_classic_state() {
  GameSnapshot snapshot;
  snapshot.screen = state(StateByte::snapshot_is_combat) != 0
      ? ScreenContext::combat
      : ScreenContext::exploration;
  snapshot.revision = 17;

  const PartyMemberId selected = state(StateByte::selected_member);
  if (state(StateByte::actor_party_member_present) != 0) {
    snapshot.party.members.emplace_back(party_member(
        static_cast<PartyMemberId>(kActingCombatant),
        "Acting party member",
        selected == static_cast<PartyMemberId>(kActingCombatant)));
  }
  snapshot.party.members.emplace_back(party_member(
      kSelectedMember,
      "Selected inventory owner",
      selected == kSelectedMember));
  snapshot.party.members.emplace_back(party_member(
      kReplacementSelectedMember,
      "Replacement inventory owner",
      selected == kReplacementSelectedMember));
  snapshot.party.selected_member = selected;

  if (state(StateByte::combat_present) != 0) {
    CombatView combat{
        .active = state(StateByte::combat_active) != 0,
        .bandage_available = state(StateByte::bandage_available) != 0,
        .undo_available = state(StateByte::undo_available) != 0,
        .cast_spell_available =
            state(StateByte::cast_spell_available) != 0,
        .target_available = state(StateByte::target_available) != 0,
        .use_scroll_available =
            state(StateByte::use_scroll_available) != 0,
        .round = 3,
        .field_origin_x = state(StateByte::field_origin_x),
        .field_origin_y = state(StateByte::field_origin_y),
        .visible_columns = state(StateByte::visible_columns),
        .visible_rows = state(StateByte::visible_rows),
        .acting_combatant =
            static_cast<CombatantId>(state(StateByte::acting_combatant)),
    };
    if (state(StateByte::actor_combatant_present) != 0) {
      combat.combatants.emplace_back(CombatantView{
          .id = kActingCombatant,
          .kind = CombatantKind::party_member,
          .name = "Acting party combatant",
          .stamina = {
              static_cast<int32_t>(state(StateByte::actor_stamina)), 20},
          .active = state(StateByte::actor_active) != 0,
          .targetable = state(StateByte::actor_targetable) != 0,
      });
    }
    snapshot.combat = std::move(combat);
  }
  return snapshot;
}

[[nodiscard]] std::optional<uint32_t> direct_classic_message(
    const CombatCase& action_case) noexcept {
  switch (action_case.command) {
    case CombatCommand::guard:
      return legacy_key_message_for_guard_combatant(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::finish:
      return legacy_key_message_for_finish_combatant(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::delay:
      return legacy_key_message_for_delay_combatant(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::center:
      return legacy_key_message_for_center_active_combatant(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::weapon:
      return legacy_key_message_for_switch_weapon(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::previous:
    case CombatCommand::next:
      if (!action_case.direction) {
        return std::nullopt;
      }
      return legacy_key_message_for_cycle_combat_focus(
          kActingCombatant, *action_case.direction, kRuntimeContext);
    case CombatCommand::auto_combatant:
      return legacy_key_message_for_auto_combatant(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::center_combat_cursor:
      if (!action_case.cell) {
        return std::nullopt;
      }
      return legacy_key_message_for_center_combat_cursor(
          kActingCombatant, *action_case.cell, kRuntimeContext);
    case CombatCommand::show_combat_range:
      return legacy_key_message_for_show_combat_range(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::bandage_combatant:
      return legacy_key_message_for_bandage_combatant(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::undo_combatant:
      return legacy_key_message_for_undo_combatant(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::open_combat_spellbook:
      return legacy_key_message_for_open_combat_spellbook(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::open_combat_targeting:
      return legacy_key_message_for_open_combat_targeting(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::escape_combat:
      return legacy_key_message_for_escape_combat(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::open_combat_scroll_case:
      return legacy_key_message_for_open_combat_scroll_case(
          kActingCombatant, kRuntimeContext);
    case CombatCommand::items:
      if (!action_case.selected_member) {
        return std::nullopt;
      }
      return legacy_key_message_for_open_combat_items(
          kActingCombatant,
          *action_case.selected_member,
          kRuntimeContext);
  }
  return std::nullopt;
}

[[nodiscard]] ClassicRequestTrace direct_classic_trace(
    const CombatCase& action_case) {
  const auto message = direct_classic_message(action_case);
  if (!message) {
    throw std::runtime_error(
        "direct Classic mapper rejected " + std::string(action_case.label));
  }
  return ClassicRequestTrace{
      .command = action_case.command,
      .actor = kActingCombatant,
      .direction = action_case.direction,
      .selected_member = action_case.selected_member,
      .cell = action_case.cell,
      .key_message = *message,
      .context = kRuntimeContext,
  };
}

[[nodiscard]] UIActionPayload semantic_payload(const CombatCase& action_case) {
  switch (action_case.command) {
    case CombatCommand::guard:
      return GuardCombatantAction{kActingCombatant};
    case CombatCommand::finish:
      return FinishCombatantAction{kActingCombatant};
    case CombatCommand::delay:
      return DelayCombatantAction{kActingCombatant};
    case CombatCommand::center:
      return CenterActiveCombatantAction{kActingCombatant};
    case CombatCommand::weapon:
      return SwitchWeaponSetAction{kActingCombatant};
    case CombatCommand::previous:
    case CombatCommand::next:
      if (!action_case.direction) {
        throw std::logic_error("cycle-focus case has no direction");
      }
      return CycleCombatFocusAction{
          .combatant = kActingCombatant,
          .direction = *action_case.direction,
      };
    case CombatCommand::auto_combatant:
      return AutoCombatantAction{kActingCombatant};
    case CombatCommand::center_combat_cursor:
      if (!action_case.cell) {
        throw std::logic_error("Center Cursor case has no field cell");
      }
      return CenterCombatCursorAction{
          .combatant = kActingCombatant,
          .cell = *action_case.cell,
      };
    case CombatCommand::show_combat_range:
      return ShowCombatRangeAction{kActingCombatant};
    case CombatCommand::bandage_combatant:
      return BandageCombatantAction{kActingCombatant};
    case CombatCommand::undo_combatant:
      return UndoCombatantAction{kActingCombatant};
    case CombatCommand::open_combat_spellbook:
      return OpenCombatSpellbookAction{kActingCombatant};
    case CombatCommand::open_combat_targeting:
      return OpenCombatTargetingAction{kActingCombatant};
    case CombatCommand::escape_combat:
      return EscapeCombatAction{kActingCombatant};
    case CombatCommand::open_combat_scroll_case:
      return OpenCombatScrollCaseAction{kActingCombatant};
    case CombatCommand::items:
      if (!action_case.selected_member) {
        throw std::logic_error("Combat Items case has no selected member");
      }
      return OpenCombatItemsAction{
          .combatant = kActingCombatant,
          .member = *action_case.selected_member,
      };
  }
  throw std::logic_error("unknown combat command");
}

void append_trace(
    std::vector<QueuedSemanticTrace>& traces,
    CombatCommand command,
    CombatantId actor,
    std::optional<CombatFocusDirection> direction,
    std::optional<PartyMemberId> selected_member,
    uint32_t message,
    const RuntimeLegacyCommandContext& context,
    uint32_t tag,
    std::optional<CombatFieldCell> cell = std::nullopt) {
  traces.emplace_back(QueuedSemanticTrace{
      .classic_request = ClassicRequestTrace{
          .command = command,
          .actor = actor,
          .direction = direction,
          .selected_member = selected_member,
          .cell = cell,
          .key_message = message,
          .context = context,
      },
      .semantic_tag = tag,
  });
}

[[nodiscard]] RuntimeLegacyCommandBridge make_runtime_bridge(
    std::vector<QueuedSemanticTrace>& traces) {
  RuntimeLegacyCombatActionSinks combat_sinks{
      .guard_combatant = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_guard_combatant_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::guard,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .finish_combatant = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_finish_combatant_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::finish,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .delay_combatant = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_delay_combatant_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::delay,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .center_active_combatant = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_center_active_combatant_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::center,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .switch_weapon = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_switch_weapon_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::weapon,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .cycle_combat_focus = [&traces](
          CombatantId actor,
          CombatFocusDirection direction,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const CombatCommand command =
            direction == CombatFocusDirection::previous
            ? CombatCommand::previous
            : CombatCommand::next;
        const uint32_t tag = semantic_cycle_combat_focus_tag(
            actor, direction, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            command,
            actor,
            direction,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .open_combat_items = [&traces](
          CombatantId actor,
          PartyMemberId selected_member,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_open_combat_items_tag(
            actor, selected_member, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::items,
            actor,
            std::nullopt,
            selected_member,
            message,
            context,
            tag);
        return tag != 0;
      },
      .auto_combatant = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_auto_combatant_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::auto_combatant,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .show_combat_range = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_show_combat_range_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::show_combat_range,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .bandage_combatant = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_bandage_combatant_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::bandage_combatant,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .undo_combatant = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_undo_combatant_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::undo_combatant,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .open_combat_spellbook = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_open_combat_spellbook_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::open_combat_spellbook,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .open_combat_targeting = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_open_combat_targeting_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::open_combat_targeting,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .escape_combat = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_escape_combat_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::escape_combat,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .open_combat_scroll_case = [&traces](
          CombatantId actor,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_open_combat_scroll_case_tag(
            actor, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::open_combat_scroll_case,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag);
        return tag != 0;
      },
      .center_combat_cursor = [&traces](
          CombatantId actor,
          CombatFieldCell cell,
          uint32_t message,
          const RuntimeLegacyCommandContext& context) {
        const uint32_t tag = semantic_center_combat_cursor_tag(
            actor, cell, REALMZ_SEMANTIC_INPUT_COMBAT);
        append_trace(
            traces,
            CombatCommand::center_combat_cursor,
            actor,
            std::nullopt,
            std::nullopt,
            message,
            context,
            tag,
            cell);
        return tag != 0;
      },
  };

  return RuntimeLegacyCommandBridge(
      [] { return kRuntimeContext; },
      RuntimeLegacyMovementSink{
          [](MovementCommand,
              uint32_t,
              const RuntimeLegacyCommandContext&) { return false; }},
      RuntimeLegacyPartySelectionSink{
          [](PartyMemberId, const RuntimeLegacyCommandContext&) {
            return false;
          }},
      RuntimeLegacyOpenInventorySink{
          [](PartyMemberId,
              uint32_t,
              const RuntimeLegacyCommandContext&) { return false; }},
      RuntimeLegacyOpenSpellbookSink{
          [](PartyMemberId,
              uint32_t,
              const RuntimeLegacyCommandContext&) { return false; }},
      RuntimeLegacyOpenSaveGameSink{
          [](RuntimeLegacyMenuCommand,
              const RuntimeLegacyCommandContext&) { return false; }},
      RuntimeLegacyOpenLoadGameSink{
          [](RuntimeLegacyMenuCommand,
              const RuntimeLegacyCommandContext&) { return false; }},
      std::move(combat_sinks));
}

[[nodiscard]] bool consume_semantic_request(
    CombatCommand command,
    uint32_t tag,
    uint32_t& classic_message,
    CombatFieldCell* absolute_cell = nullptr) {
  switch (command) {
    case CombatCommand::guard:
      return RealmzConsumeSemanticGuardCombatantEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::finish:
      return RealmzConsumeSemanticFinishCombatantEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::delay:
      return RealmzConsumeSemanticDelayCombatantEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::center:
      return RealmzConsumeSemanticCenterActiveCombatantEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::weapon:
      return RealmzConsumeSemanticSwitchWeaponEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::previous:
    case CombatCommand::next:
      return RealmzConsumeSemanticCycleCombatFocusEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::auto_combatant:
      return RealmzConsumeSemanticAutoCombatantEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::center_combat_cursor: {
      uint8_t absolute_x = 0xA5;
      uint8_t absolute_y = 0x5A;
      const bool consumed = RealmzConsumeSemanticCenterCombatCursorEvent(
          REALMZ_SEMANTIC_INPUT_COMBAT,
          tag,
          &classic_message,
          &absolute_x,
          &absolute_y) != 0;
      if (consumed && absolute_cell) {
        *absolute_cell = {.x = absolute_x, .y = absolute_y};
      }
      return consumed;
    }
    case CombatCommand::show_combat_range:
      return RealmzConsumeSemanticShowCombatRangeEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::bandage_combatant:
      return RealmzConsumeSemanticBandageCombatantEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::undo_combatant:
      return RealmzConsumeSemanticUndoCombatantEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::open_combat_spellbook:
      return RealmzConsumeSemanticOpenCombatSpellbookEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::open_combat_targeting:
      return RealmzConsumeSemanticOpenCombatTargetingEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::escape_combat:
      return RealmzConsumeSemanticEscapeCombatEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::open_combat_scroll_case:
      return RealmzConsumeSemanticOpenCombatScrollCaseEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
    case CombatCommand::items:
      return RealmzConsumeSemanticOpenCombatItemsEvent(
                 REALMZ_SEMANTIC_INPUT_COMBAT,
                 tag,
                 &classic_message) != 0;
  }
  return false;
}

void complete_combat_input_scope() {
  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzCurrentSemanticInputSurface() ==
      REALMZ_SEMANTIC_INPUT_COMBAT);
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() ==
      REALMZ_SEMANTIC_INPUT_NONE);
}

[[nodiscard]] QueuedSemanticTrace dispatch_semantic_action(
    const CombatCase& action_case,
    ActionSequence sequence) {
  std::vector<QueuedSemanticTrace> traces;
  auto bridge = make_runtime_bridge(traces);
  const auto result = bridge.dispatch(UIAction{
      .sequence = sequence,
      .payload = semantic_payload(action_case),
  });
  CHECK(result.status == DispatchStatus::handled);
  CHECK(result.detail.empty());
  CHECK(result.events.empty());
  CHECK(traces.size() == 1);
  return traces.front();
}

void check_second_consume_is_unauthorized(
    const CombatCase& action_case,
    uint32_t tag,
    const PreClassicStateBytes& expected_state,
    int expected_legacy_capture_calls,
    int expected_snapshot_capture_calls) {
  uint32_t second_output = kUnchangedClassicMessage;
  CHECK(!consume_semantic_request(
      action_case.command, tag, second_output));
  CHECK(second_output == kUnchangedClassicMessage);
  CHECK(legacy_context_capture_calls == expected_legacy_capture_calls);
  CHECK(snapshot_capture_calls == expected_snapshot_capture_calls);
  CHECK(pre_classic_state == expected_state);
}

void test_exact_request_equivalence_and_success_is_single_use() {
  ActionSequence sequence = 1;
  for (const auto& action_case : kCombatCases) {
    reset_fixture_state();
    const PreClassicStateBytes initial_state = pre_classic_state;

    const ClassicRequestTrace direct = direct_classic_trace(action_case);
    CHECK(direct.key_message == action_case.expected_classic_message);
    CHECK(pre_classic_state == initial_state);

    const QueuedSemanticTrace queued =
        dispatch_semantic_action(action_case, sequence++);
    CHECK(queued.classic_request == direct);
    CHECK(queued.semantic_tag == action_case.expected_semantic_tag);
    CHECK(queued.semantic_tag != 0);
    CHECK(RealmzIsSemanticGameplayTag(queued.semantic_tag) != 0);
    CHECK(RealmzSemanticGameplayTagSurface(queued.semantic_tag) ==
        REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(pre_classic_state == initial_state);

    complete_combat_input_scope();
    uint32_t consumed_message = kUnchangedClassicMessage;
    CombatFieldCell consumed_cell{.x = 0xA5, .y = 0x5A};
    CHECK(consume_semantic_request(
        action_case.command,
        queued.semantic_tag,
        consumed_message,
        &consumed_cell));
    CHECK(consumed_message == direct.key_message);
    CHECK(consumed_message == action_case.expected_classic_message);
    if (action_case.cell) {
      CHECK(consumed_cell == *action_case.cell);
    } else {
      CHECK(consumed_cell == (CombatFieldCell{.x = 0xA5, .y = 0x5A}));
    }
    CHECK(legacy_context_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);
    CHECK(pre_classic_state == initial_state);

    check_second_consume_is_unauthorized(
        action_case,
        queued.semantic_tag,
        initial_state,
        1,
        1);
  }
}

void test_stale_actor_rejection_is_single_use_for_every_action() {
  ActionSequence sequence = 100;
  for (const auto& action_case : kCombatCases) {
    reset_fixture_state();
    const QueuedSemanticTrace queued =
        dispatch_semantic_action(action_case, sequence++);
    CHECK(queued.classic_request.actor == kActingCombatant);
    CHECK(queued.classic_request.direction == action_case.direction);
    CHECK(queued.classic_request.selected_member ==
        action_case.selected_member);
    CHECK(queued.classic_request.cell == action_case.cell);

    // The live turn changes after queueing. The tag must not silently retarget
    // to the new actor, and the failed attempt must spend its authorization.
    set_state(StateByte::acting_combatant, 3);
    const PreClassicStateBytes stale_state = pre_classic_state;
    complete_combat_input_scope();
    uint32_t output = kUnchangedClassicMessage;
    CHECK(!consume_semantic_request(
        action_case.command, queued.semantic_tag, output));
    CHECK(output == kUnchangedClassicMessage);
    CHECK(legacy_context_capture_calls == 1);
    CHECK(snapshot_capture_calls == 1);
    CHECK(pre_classic_state == stale_state);

    set_state(
        StateByte::acting_combatant,
        static_cast<uint8_t>(kActingCombatant));
    const PreClassicStateBytes repaired_state = pre_classic_state;
    check_second_consume_is_unauthorized(
        action_case,
        queued.semantic_tag,
        repaired_state,
        1,
        1);
  }
}

void test_combat_items_stale_selected_member_is_single_use() {
  const CombatCase& items = kCombatCases.back();
  CHECK(items.command == CombatCommand::items);
  CHECK(items.selected_member == kSelectedMember);

  reset_fixture_state();
  const QueuedSemanticTrace queued = dispatch_semantic_action(items, 200);
  CHECK(queued.classic_request.actor == kActingCombatant);
  CHECK(queued.classic_request.selected_member == kSelectedMember);
  CHECK(queued.semantic_tag == items.expected_semantic_tag);

  // The selected party member is independent from the acting combatant. A
  // queued Items request for member 4 must not open member 5's Classic modal.
  set_state(StateByte::selected_member, kReplacementSelectedMember);
  const PreClassicStateBytes stale_selection_state = pre_classic_state;
  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(!consume_semantic_request(
      CombatCommand::items, queued.semantic_tag, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(legacy_context_capture_calls == 1);
  CHECK(snapshot_capture_calls == 1);
  CHECK(pre_classic_state == stale_selection_state);

  set_state(StateByte::selected_member, kSelectedMember);
  const PreClassicStateBytes repaired_state = pre_classic_state;
  check_second_consume_is_unauthorized(
      items, queued.semantic_tag, repaired_state, 1, 1);
}

void test_combat_items_stops_at_modal_request_handoff() {
  const CombatCase& items = kCombatCases.back();
  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(items, 300);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::items, queued.semantic_tag, output));
  CHECK(output == 0x00002269U);
  CHECK(pre_classic_state == initial_state);

  // This is the complete claim of the fixture: the preserved lowercase "i"
  // request reached the Classic modal handoff. No item choice, live-engine
  // mutation, turn effect, or save equivalence is simulated here.
}

void test_center_combat_cursor_stops_at_key_and_cell_handoff() {
  const CombatCase& center_cursor =
      kCombatCases[kCombatCases.size() - 9];
  CHECK(center_cursor.command == CombatCommand::center_combat_cursor);
  CHECK(center_cursor.cell == kCursorCell);
  CHECK(center_cursor.expected_semantic_tag == 0x4D022A11U);

  reset_fixture_state();
  const QueuedSemanticTrace queued =
      dispatch_semantic_action(center_cursor, 350);
  CHECK(queued.classic_request.cell == kCursorCell);

  // Move the camera after queueing. The absolute cell is intentionally outside
  // this still-sane viewport and must remain byte-for-byte unchanged.
  set_state(StateByte::field_origin_x, 75);
  set_state(StateByte::field_origin_y, 77);
  const PreClassicStateBytes handoff_state = pre_classic_state;
  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CombatFieldCell absolute_cell{.x = 0xA5, .y = 0x5A};
  CHECK(consume_semantic_request(
      CombatCommand::center_combat_cursor,
      queued.semantic_tag,
      output,
      &absolute_cell));
  CHECK(output == 0x00002E6DU);
  CHECK(absolute_cell == kCursorCell);
  CHECK(pre_classic_state == handoff_state);

  // The fixture ends at the exact lowercase "m" plus absolute-cell handoff.
  // It makes no centerfield, camera-origin, redraw, or save-state equivalence
  // claim; all of those effects remain owned by the preserved Classic branch.
}

void test_combat_range_stops_at_classic_modal_handoff() {
  const CombatCase& show_range =
      kCombatCases[kCombatCases.size() - 8];
  CHECK(show_range.command == CombatCommand::show_combat_range);

  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(show_range, 400);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::show_combat_range, queued.semantic_tag, output));
  CHECK(output == 0x00000F72U);
  CHECK(pre_classic_state == initial_state);

  // The fixture ends at the preserved lowercase "r" request. Classic owns
  // drawing, the raw dismissal wait, recentering, and later input; none of
  // those modal effects are simulated by this pre-Classic state image.
}

void test_bandage_stops_at_classic_target_picker_handoff() {
  const CombatCase& bandage = kCombatCases[kCombatCases.size() - 7];
  CHECK(bandage.command == CombatCommand::bandage_combatant);

  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(bandage, 500);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::bandage_combatant, queued.semantic_tag, output));
  CHECK(output == 0x00000B62U);
  CHECK(pre_classic_state == initial_state);

  // The fixture ends at the preserved lowercase "b" request. Classic owns
  // canundo enforcement, target selection, bleeding mutation, feedback, and
  // turn handling; none of those effects are simulated here.
}

void test_bandage_unavailable_rejection_is_single_use() {
  const CombatCase& bandage = kCombatCases[kCombatCases.size() - 7];
  reset_fixture_state();
  const QueuedSemanticTrace queued = dispatch_semantic_action(bandage, 600);

  set_state(StateByte::bandage_available, 0);
  const PreClassicStateBytes unavailable_state = pre_classic_state;
  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(!consume_semantic_request(
      CombatCommand::bandage_combatant, queued.semantic_tag, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(pre_classic_state == unavailable_state);

  set_state(StateByte::bandage_available, 1);
  check_second_consume_is_unauthorized(
      bandage, queued.semantic_tag, pre_classic_state, 1, 1);
}

void test_undo_stops_at_classic_rollback_handoff() {
  const CombatCase& undo = kCombatCases[kCombatCases.size() - 6];
  CHECK(undo.command == CombatCommand::undo_combatant);

  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(undo, 700);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::undo_combatant, queued.semantic_tag, output));
  CHECK(output == 0x00002075U);
  CHECK(pre_classic_state == initial_state);

  // The fixture stops at lowercase "u". Classic owns its condition checks,
  // position/field rollback, attack restoration, queue rewind, redraw, and
  // getup turn handling; none is simulated in the pre-Classic byte image.
}

void test_undo_unavailable_rejection_is_single_use() {
  const CombatCase& undo = kCombatCases[kCombatCases.size() - 6];
  reset_fixture_state();
  const QueuedSemanticTrace queued = dispatch_semantic_action(undo, 800);

  set_state(StateByte::undo_available, 0);
  const PreClassicStateBytes unavailable_state = pre_classic_state;
  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(!consume_semantic_request(
      CombatCommand::undo_combatant, queued.semantic_tag, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(pre_classic_state == unavailable_state);

  set_state(StateByte::undo_available, 1);
  check_second_consume_is_unauthorized(
      undo, queued.semantic_tag, pre_classic_state, 1, 1);
}

void test_combat_spellbook_stops_at_classic_cast_handoff() {
  const CombatCase& cast = kCombatCases[kCombatCases.size() - 5];
  CHECK(cast.command == CombatCommand::open_combat_spellbook);

  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(cast, 900);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::open_combat_spellbook, queued.semantic_tag, output));
  CHECK(output == 0x00000173U);
  CHECK(pre_classic_state == initial_state);

  // The fixture stops at lowercase "s". Classic owns cancast, combatchoice,
  // castspell, targeting, spell effects, mutations, and turn handling.
}

void test_combat_spellbook_unavailable_rejection_is_single_use() {
  const CombatCase& cast = kCombatCases[kCombatCases.size() - 5];
  reset_fixture_state();
  const QueuedSemanticTrace queued = dispatch_semantic_action(cast, 1000);

  set_state(StateByte::cast_spell_available, 0);
  const PreClassicStateBytes unavailable_state = pre_classic_state;
  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(!consume_semantic_request(
      CombatCommand::open_combat_spellbook, queued.semantic_tag, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(pre_classic_state == unavailable_state);

  set_state(StateByte::cast_spell_available, 1);
  check_second_consume_is_unauthorized(
      cast, queued.semantic_tag, pre_classic_state, 1, 1);
}

void test_combat_targeting_stops_at_classic_target_handoff() {
  const CombatCase& target = kCombatCases[kCombatCases.size() - 4];
  CHECK(target.command == CombatCommand::open_combat_targeting);

  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(target, 1100);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::open_combat_targeting, queued.semantic_tag, output));
  CHECK(output == 0x00001174U);
  CHECK(pre_classic_state == initial_state);

  // The fixture stops at lowercase "t". Classic owns combatitem/quiver
  // selection, charge/RNG handling, the raw target loop, and all mutations.
}

void test_combat_targeting_unavailable_rejection_is_single_use() {
  const CombatCase& target = kCombatCases[kCombatCases.size() - 4];
  reset_fixture_state();
  const QueuedSemanticTrace queued = dispatch_semantic_action(target, 1200);

  set_state(StateByte::target_available, 0);
  const PreClassicStateBytes unavailable_state = pre_classic_state;
  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(!consume_semantic_request(
      CombatCommand::open_combat_targeting, queued.semantic_tag, output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(pre_classic_state == unavailable_state);

  set_state(StateByte::target_available, 1);
  check_second_consume_is_unauthorized(
      target, queued.semantic_tag, pre_classic_state, 1, 1);
}

void test_escape_combat_stops_at_classic_key_handoff() {
  const CombatCase& escape = kCombatCases[kCombatCases.size() - 3];
  CHECK(escape.command == CombatCommand::escape_combat);
  CHECK(escape.expected_semantic_tag == 0x52450302U);

  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(escape, 1300);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::escape_combat, queued.semantic_tag, output));
  CHECK(output == 0x00000E65U);
  CHECK(pre_classic_state == initial_state);

  // The equivalence boundary ends at lowercase "e". Classic still owns the
  // range/condition checks, warning precedence, confirmation, retreat-state
  // mutations, reputation/loyalty/cowardice effects, killmon, and getup.
}

void test_open_combat_scroll_case_stops_at_classic_key_handoff() {
  const CombatCase& scroll = kCombatCases[kCombatCases.size() - 2];
  CHECK(scroll.command == CombatCommand::open_combat_scroll_case);
  CHECK(scroll.expected_semantic_tag == 0x55530302U);

  reset_fixture_state();
  const PreClassicStateBytes initial_state = pre_classic_state;
  const QueuedSemanticTrace queued = dispatch_semantic_action(scroll, 1400);

  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(consume_semantic_request(
      CombatCommand::open_combat_scroll_case,
      queued.semantic_tag,
      output));
  CHECK(output == 0x0000256CU);
  CHECK(pre_classic_state == initial_state);

  // The equivalence boundary ends at lowercase "l". The semantic action has
  // no scroll slot, spell, power, target, or recipient. Classic still owns
  // getscroll, its raw modal loop, early charge consumption, wand targeting,
  // movement/attack costs, effects, and turn handling.
}

void test_open_combat_scroll_case_unavailable_rejection_is_single_use() {
  const CombatCase& scroll = kCombatCases[kCombatCases.size() - 2];
  reset_fixture_state();
  const QueuedSemanticTrace queued = dispatch_semantic_action(scroll, 1500);

  set_state(StateByte::use_scroll_available, 0);
  const PreClassicStateBytes unavailable_state = pre_classic_state;
  complete_combat_input_scope();
  uint32_t output = kUnchangedClassicMessage;
  CHECK(!consume_semantic_request(
      CombatCommand::open_combat_scroll_case,
      queued.semantic_tag,
      output));
  CHECK(output == kUnchangedClassicMessage);
  CHECK(pre_classic_state == unavailable_state);

  set_state(StateByte::use_scroll_available, 1);
  check_second_consume_is_unauthorized(
      scroll, queued.semantic_tag, pre_classic_state, 1, 1);
}

} // namespace

extern "C" RealmzLegacyPresentationContext
RealmzCaptureLegacyPresentationContext(void) {
  ++legacy_context_capture_calls;
  return captured_legacy_context;
}

namespace realmz::presentation {

GameSnapshot LegacyGameSnapshotSource::capture() const {
  ++snapshot_capture_calls;
  return snapshot_from_pre_classic_state();
}

} // namespace realmz::presentation

int main() {
  try {
    test_exact_request_equivalence_and_success_is_single_use();
    test_stale_actor_rejection_is_single_use_for_every_action();
    test_combat_items_stale_selected_member_is_single_use();
    test_combat_items_stops_at_modal_request_handoff();
    test_center_combat_cursor_stops_at_key_and_cell_handoff();
    test_combat_range_stops_at_classic_modal_handoff();
    test_bandage_stops_at_classic_target_picker_handoff();
    test_bandage_unavailable_rejection_is_single_use();
    test_undo_stops_at_classic_rollback_handoff();
    test_undo_unavailable_rejection_is_single_use();
    test_combat_spellbook_stops_at_classic_cast_handoff();
    test_combat_spellbook_unavailable_rejection_is_single_use();
    test_combat_targeting_stops_at_classic_target_handoff();
    test_combat_targeting_unavailable_rejection_is_single_use();
    test_escape_combat_stops_at_classic_key_handoff();
    test_open_combat_scroll_case_stops_at_classic_key_handoff();
    test_open_combat_scroll_case_unavailable_rejection_is_single_use();
    RealmzInvalidateSemanticInputBoundary();
    std::cout << "CombatActionEquivalenceTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    RealmzInvalidateSemanticInputBoundary();
    std::cerr << "CombatActionEquivalenceTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
