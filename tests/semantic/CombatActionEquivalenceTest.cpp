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
  items,
};

struct CombatCase {
  CombatCommand command = CombatCommand::guard;
  std::string_view label;
  uint32_t expected_classic_message = 0;
  uint32_t expected_semantic_tag = 0;
  std::optional<CombatFocusDirection> direction;
  std::optional<PartyMemberId> selected_member;
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
  acting_combatant,
  actor_party_member_present,
  actor_combatant_present,
  actor_active,
  actor_targetable,
  actor_stamina,
  actor_movement,
  actor_movement_maximum,
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
        .round = 3,
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
    uint32_t tag) {
  traces.emplace_back(QueuedSemanticTrace{
      .classic_request = ClassicRequestTrace{
          .command = command,
          .actor = actor,
          .direction = direction,
          .selected_member = selected_member,
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
    uint32_t& classic_message) {
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
    CHECK(consume_semantic_request(
        action_case.command, queued.semantic_tag, consumed_message));
    CHECK(consumed_message == direct.key_message);
    CHECK(consumed_message == action_case.expected_classic_message);
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
