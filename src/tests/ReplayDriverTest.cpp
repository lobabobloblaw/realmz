#include "replay/ReplayDriver.hpp"
#include "replay/ReplayChildConfig.hpp"
#include "replay/ReplayStateOracle.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace realmz::presentation;
using namespace realmz::replay;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

[[nodiscard]] UIAction movement(
    ActionSequence sequence,
    MovementCommand command) {
  return UIAction{
      .sequence = sequence,
      .payload = MovePartyAction{command},
  };
}

[[nodiscard]] UIAction selection(
    ActionSequence sequence,
    PartyMemberId member) {
  return UIAction{
      .sequence = sequence,
      .payload = SelectPartyMemberAction{member},
  };
}

[[nodiscard]] ReplayObservedEvent key_down(std::uint32_t message) {
  return ReplayObservedEvent{
      .kind = ReplayObservedEventKind::key_down,
      .message = message,
  };
}

[[nodiscard]] std::vector<UIAction> movement_plan(std::size_t count) {
  std::vector<UIAction> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.emplace_back(movement(
        static_cast<ActionSequence>(index) + 1U,
        MovementCommand::north));
  }
  return result;
}

void require_initial_checkpoint(const ReplayPollDirective& directive) {
  CHECK(directive.checkpoint.has_value());
  CHECK(directive.checkpoint->kind == ReplayCheckpointKind::initial);
  CHECK(!directive.checkpoint->action_index.has_value());
}

void require_settled_checkpoint(
    const ReplayPollDirective& directive,
    std::uint32_t action_index) {
  CHECK(directive.checkpoint.has_value());
  CHECK(directive.checkpoint->kind == ReplayCheckpointKind::action_settled);
  CHECK(directive.checkpoint->action_index == action_index);
}

void require_failed(
    const ReplayDriver& driver,
    ReplayDriverFailure failure) {
  CHECK(driver.phase() == ReplayDriverPhase::failed);
  CHECK(driver.failure() == failure);
  CHECK(replay_driver_failure_name(failure) != "none");
}

void test_zero_actions_finalize_at_first_poll() {
  ReplayDriver driver({});
  CHECK(driver.phase() == ReplayDriverPhase::awaiting_gameplay_poll);
  CHECK(driver.failure() == ReplayDriverFailure::none);
  CHECK(driver.action_count() == 0U);

  const auto directive = driver.on_gameplay_poll();
  require_initial_checkpoint(directive);
  CHECK(directive.action == nullptr);
  CHECK(directive.finalize);
  CHECK(driver.phase() == ReplayDriverPhase::finalizing);
  CHECK(driver.acknowledged_action_count() == 0U);
}

void test_one_action_settles_on_following_poll() {
  ReplayDriver driver({movement(1, MovementCommand::north)});

  const auto first = driver.on_gameplay_poll();
  require_initial_checkpoint(first);
  CHECK(first.action != nullptr);
  CHECK(first.action->sequence == 1U);
  const auto* movement_payload =
      std::get_if<MovePartyAction>(&first.action->payload);
  CHECK(movement_payload != nullptr);
  CHECK(movement_payload->command == MovementCommand::north);
  CHECK(!first.finalize);
  CHECK(driver.phase() ==
      ReplayDriverPhase::awaiting_delivery_acknowledgement);
  CHECK(driver.acknowledged_action_count() == 0U);

  constexpr std::uint32_t expected_message = 0x00007E1EU;
  CHECK(driver.acknowledge_delivery(
      first.action->sequence,
      expected_message,
      key_down(expected_message)));
  CHECK(driver.phase() == ReplayDriverPhase::awaiting_gameplay_poll);
  CHECK(driver.acknowledged_action_count() == 1U);

  const auto second = driver.on_gameplay_poll();
  require_settled_checkpoint(second, 0U);
  CHECK(second.action == nullptr);
  CHECK(second.finalize);
  CHECK(driver.phase() == ReplayDriverPhase::finalizing);
}

void test_each_poll_delivers_at_most_one_action() {
  ReplayDriver driver({
      movement(1, MovementCommand::west),
      movement(2, MovementCommand::east),
      movement(3, MovementCommand::south),
  });
  constexpr std::uint32_t messages[] = {
      0x00007B1CU,
      0x00007C1DU,
      0x00007D1FU,
  };

  for (std::size_t index = 0; index < 3U; ++index) {
    const auto directive = driver.on_gameplay_poll();
    if (index == 0U) {
      require_initial_checkpoint(directive);
    } else {
      require_settled_checkpoint(
          directive, static_cast<std::uint32_t>(index - 1U));
    }
    CHECK(directive.action != nullptr);
    CHECK(directive.action->sequence == index + 1U);
    CHECK(!directive.finalize);
    CHECK(driver.acknowledge_delivery(
        directive.action->sequence,
        messages[index],
        key_down(messages[index])));
  }

  const auto final = driver.on_gameplay_poll();
  require_settled_checkpoint(final, 2U);
  CHECK(final.action == nullptr);
  CHECK(final.finalize);
  CHECK(driver.acknowledged_action_count() == 3U);
}

void test_plan_validation_is_strict_and_fail_closed() {
  ReplayDriver starts_at_zero({movement(0, MovementCommand::north)});
  require_failed(
      starts_at_zero, ReplayDriverFailure::invalid_action_sequence);
  const auto zero_poll = starts_at_zero.on_gameplay_poll();
  CHECK(!zero_poll.checkpoint);
  CHECK(zero_poll.action == nullptr);
  CHECK(!zero_poll.finalize);

  ReplayDriver skips_sequence({
      movement(1, MovementCommand::north),
      movement(3, MovementCommand::south),
  });
  require_failed(
      skips_sequence, ReplayDriverFailure::invalid_action_sequence);

  ReplayDriver duplicate_sequence({
      movement(1, MovementCommand::north),
      movement(1, MovementCommand::south),
  });
  require_failed(
      duplicate_sequence, ReplayDriverFailure::invalid_action_sequence);

  ReplayDriver unsupported({UIAction{
      .sequence = 1,
      .payload = ConfirmAction{},
  }});
  require_failed(unsupported, ReplayDriverFailure::unsupported_action);

  ReplayDriver v1_selection({selection(1, 2)});
  require_failed(v1_selection, ReplayDriverFailure::unsupported_action);

  ReplayDriver v2_selection(
      {selection(1, 2)}, ReplayActionVocabulary::native_v2);
  CHECK(v2_selection.phase() == ReplayDriverPhase::awaiting_gameplay_poll);
  CHECK(v2_selection.failure() == ReplayDriverFailure::none);

  ReplayDriver invalid_v2_selection(
      {selection(1, 255)}, ReplayActionVocabulary::native_v2);
  require_failed(
      invalid_v2_selection, ReplayDriverFailure::invalid_party_member);
}

void test_v2_party_selection_delivery_and_settlement() {
  ReplayDriver driver(
      {
          selection(1, 2),
          movement(2, MovementCommand::east),
          selection(3, 2),
      },
      ReplayActionVocabulary::native_v2);
  CHECK(driver.selected_member_for_action(0U) == 2U);
  CHECK(!driver.selected_member_for_action(1U));
  CHECK(driver.selected_member_for_action(2U) == 2U);
  CHECK(!driver.selected_member_for_action(3U));

  auto directive = driver.on_gameplay_poll();
  require_initial_checkpoint(directive);
  CHECK(directive.action != nullptr);
  CHECK(driver.acknowledge_party_selection_delivery(
      directive.action->sequence,
      2,
      ReplayPartySelectionDeliveryOutcome::changed));
  CHECK(driver.acknowledged_action_count() == 1U);

  directive = driver.on_gameplay_poll();
  require_settled_checkpoint(directive, 0U);
  CHECK(directive.action != nullptr);
  constexpr std::uint32_t east_message = 0x00007C1DU;
  CHECK(driver.acknowledge_delivery(
      directive.action->sequence,
      east_message,
      key_down(east_message)));

  directive = driver.on_gameplay_poll();
  require_settled_checkpoint(directive, 1U);
  CHECK(directive.action != nullptr);
  CHECK(driver.acknowledge_party_selection_delivery(
      directive.action->sequence,
      2,
      ReplayPartySelectionDeliveryOutcome::unchanged));
  CHECK(driver.acknowledged_action_count() == 3U);

  directive = driver.on_gameplay_poll();
  require_settled_checkpoint(directive, 2U);
  CHECK(directive.finalize);
}

void test_v2_party_selection_acknowledgement_fails_closed() {
  ReplayDriver movement_as_selection(
      {movement(1, MovementCommand::north)},
      ReplayActionVocabulary::native_v2);
  CHECK(movement_as_selection.on_gameplay_poll().action != nullptr);
  CHECK(!movement_as_selection.acknowledge_party_selection_delivery(
      1, 0, ReplayPartySelectionDeliveryOutcome::changed));
  require_failed(
      movement_as_selection,
      ReplayDriverFailure::delivered_action_kind_mismatch);

  ReplayDriver selection_as_movement(
      {selection(1, 2)}, ReplayActionVocabulary::native_v2);
  CHECK(selection_as_movement.on_gameplay_poll().action != nullptr);
  CHECK(!selection_as_movement.acknowledge_delivery(
      1, 0x00007E1EU, key_down(0x00007E1EU)));
  require_failed(
      selection_as_movement,
      ReplayDriverFailure::delivered_action_kind_mismatch);

  ReplayDriver wrong_sequence(
      {selection(1, 2)}, ReplayActionVocabulary::native_v2);
  CHECK(wrong_sequence.on_gameplay_poll().action != nullptr);
  CHECK(!wrong_sequence.acknowledge_party_selection_delivery(
      2, 2, ReplayPartySelectionDeliveryOutcome::changed));
  require_failed(
      wrong_sequence,
      ReplayDriverFailure::delivered_action_sequence_mismatch);

  ReplayDriver wrong_member(
      {selection(1, 2)}, ReplayActionVocabulary::native_v2);
  CHECK(wrong_member.on_gameplay_poll().action != nullptr);
  CHECK(!wrong_member.acknowledge_party_selection_delivery(
      1, 1, ReplayPartySelectionDeliveryOutcome::changed));
  require_failed(
      wrong_member,
      ReplayDriverFailure::delivered_party_member_mismatch);

  ReplayDriver rejected(
      {selection(1, 2)}, ReplayActionVocabulary::native_v2);
  CHECK(rejected.on_gameplay_poll().action != nullptr);
  CHECK(!rejected.acknowledge_party_selection_delivery(
      1, 2, ReplayPartySelectionDeliveryOutcome::rejected));
  require_failed(rejected, ReplayDriverFailure::party_selection_rejected);

  ReplayDriver invalid_outcome(
      {selection(1, 2)}, ReplayActionVocabulary::native_v2);
  CHECK(invalid_outcome.on_gameplay_poll().action != nullptr);
  CHECK(!invalid_outcome.acknowledge_party_selection_delivery(
      1,
      2,
      static_cast<ReplayPartySelectionDeliveryOutcome>(255)));
  require_failed(
      invalid_outcome, ReplayDriverFailure::party_selection_rejected);

  ReplayDriver without_poll(
      {selection(1, 2)}, ReplayActionVocabulary::native_v2);
  CHECK(!without_poll.acknowledge_party_selection_delivery(
      1, 2, ReplayPartySelectionDeliveryOutcome::changed));
  require_failed(
      without_poll,
      ReplayDriverFailure::acknowledgement_without_pending_delivery);
}

void test_plan_action_limit_boundary() {
  ReplayDriver maximum(movement_plan(kMaximumReplayActions));
  CHECK(maximum.phase() == ReplayDriverPhase::awaiting_gameplay_poll);
  CHECK(maximum.failure() == ReplayDriverFailure::none);
  CHECK(maximum.action_count() == kMaximumReplayActions);
  const auto first = maximum.on_gameplay_poll();
  require_initial_checkpoint(first);
  CHECK(first.action != nullptr);
  CHECK(first.action->sequence == 1U);
  CHECK(!first.finalize);

  ReplayDriver excessive(movement_plan(kMaximumReplayActions + 1U));
  require_failed(excessive, ReplayDriverFailure::action_limit_exceeded);
  CHECK(excessive.action_count() == kMaximumReplayActions + 1U);
  CHECK(excessive.acknowledged_action_count() == 0U);
  const auto rejected = excessive.on_gameplay_poll();
  CHECK(!rejected.checkpoint);
  CHECK(rejected.action == nullptr);
  CHECK(!rejected.finalize);
}

void test_poll_before_acknowledgement_fails_sticky() {
  ReplayDriver driver({movement(1, MovementCommand::north)});
  const auto first = driver.on_gameplay_poll();
  CHECK(first.action != nullptr);

  const auto invalid = driver.on_gameplay_poll();
  CHECK(!invalid.checkpoint);
  CHECK(invalid.action == nullptr);
  CHECK(!invalid.finalize);
  require_failed(
      driver,
      ReplayDriverFailure::gameplay_poll_while_delivery_pending);
  CHECK(!driver.acknowledge_delivery(
      1U, 0x00007E1EU, key_down(0x00007E1EU)));
  require_failed(
      driver,
      ReplayDriverFailure::gameplay_poll_while_delivery_pending);
}

void test_delivery_requires_pending_exact_sequence_and_event() {
  ReplayDriver without_poll({movement(1, MovementCommand::north)});
  CHECK(!without_poll.acknowledge_delivery(
      1U, 0x00007E1EU, key_down(0x00007E1EU)));
  require_failed(
      without_poll,
      ReplayDriverFailure::acknowledgement_without_pending_delivery);

  ReplayDriver wrong_sequence({movement(1, MovementCommand::north)});
  CHECK(wrong_sequence.on_gameplay_poll().action != nullptr);
  CHECK(!wrong_sequence.acknowledge_delivery(
      2U, 0x00007E1EU, key_down(0x00007E1EU)));
  require_failed(
      wrong_sequence,
      ReplayDriverFailure::delivered_action_sequence_mismatch);

  ReplayDriver wrong_kind({movement(1, MovementCommand::north)});
  CHECK(wrong_kind.on_gameplay_poll().action != nullptr);
  CHECK(!wrong_kind.acknowledge_delivery(
      1U,
      0x00007E1EU,
      ReplayObservedEvent{
          .kind = ReplayObservedEventKind::other,
          .message = 0x00007E1EU,
      }));
  require_failed(
      wrong_kind, ReplayDriverFailure::delivered_event_not_key_down);

  ReplayDriver wrong_message({movement(1, MovementCommand::north)});
  CHECK(wrong_message.on_gameplay_poll().action != nullptr);
  CHECK(!wrong_message.acknowledge_delivery(
      1U, 0x00007E1EU, key_down(0x00007D1FU)));
  require_failed(
      wrong_message,
      ReplayDriverFailure::delivered_event_message_mismatch);

  ReplayDriver zero_expected_message({
      movement(1, MovementCommand::north),
  });
  CHECK(zero_expected_message.on_gameplay_poll().action != nullptr);
  CHECK(!zero_expected_message.acknowledge_delivery(
      1U, 0U, key_down(0U)));
  require_failed(
      zero_expected_message,
      ReplayDriverFailure::expected_key_down_message_invalid);
}

void test_poll_after_finalization_fails_closed() {
  ReplayDriver driver({});
  CHECK(driver.on_gameplay_poll().finalize);
  const auto invalid = driver.on_gameplay_poll();
  CHECK(!invalid.checkpoint);
  CHECK(invalid.action == nullptr);
  CHECK(!invalid.finalize);
  require_failed(
      driver, ReplayDriverFailure::gameplay_poll_after_finalization);
}

void test_checkpoint_indexes_feed_state_trace_directly() {
  ReplayDriver driver({
      movement(1, MovementCommand::north),
      movement(2, MovementCommand::east),
  });
  ReplayStateSnapshot initial;
  initial.world.party_y = 10;
  ReplayStateSnapshot after_first = initial;
  after_first.world.party_y = 9;
  ReplayStateSnapshot after_second = after_first;
  after_second.world.party_x = 1;
  const std::array expected_post_actions{after_first, after_second};
  ReplayStateTraceHasher trace(2U);

  auto directive = driver.on_gameplay_poll();
  require_initial_checkpoint(directive);
  trace.append_initial(initial);
  CHECK(directive.action != nullptr);
  CHECK(driver.acknowledge_delivery(
      directive.action->sequence, 0x00007E1EU, key_down(0x00007E1EU)));

  directive = driver.on_gameplay_poll();
  require_settled_checkpoint(directive, 0U);
  trace.append_post_action(
      *directive.checkpoint->action_index, after_first);
  CHECK(directive.action != nullptr);
  CHECK(driver.acknowledge_delivery(
      directive.action->sequence, 0x00007C1DU, key_down(0x00007C1DU)));

  directive = driver.on_gameplay_poll();
  require_settled_checkpoint(directive, 1U);
  trace.append_post_action(
      *directive.checkpoint->action_index, after_second);
  CHECK(directive.finalize);
  CHECK(trace.complete());
  CHECK(trace.finalize() ==
      replay_state_trace_sha256_v1(initial, expected_post_actions));
}

} // namespace

int main() {
  try {
    test_zero_actions_finalize_at_first_poll();
    test_one_action_settles_on_following_poll();
    test_each_poll_delivers_at_most_one_action();
    test_plan_validation_is_strict_and_fail_closed();
    test_v2_party_selection_delivery_and_settlement();
    test_v2_party_selection_acknowledgement_fails_closed();
    test_plan_action_limit_boundary();
    test_poll_before_acknowledgement_fails_sticky();
    test_delivery_requires_pending_exact_sequence_and_event();
    test_poll_after_finalization_fails_closed();
    test_checkpoint_indexes_feed_state_trace_directly();
    std::cout << "ReplayDriverTest: " << checks_run << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayDriverTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
