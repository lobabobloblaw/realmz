#include "replay/ReplayDriver.hpp"

#include "replay/ReplayChildConfig.hpp"

#include <cstddef>
#include <utility>
#include <variant>

namespace realmz::replay {

std::string_view replay_driver_failure_name(
    ReplayDriverFailure failure) noexcept {
  switch (failure) {
    case ReplayDriverFailure::none:
      return "none";
    case ReplayDriverFailure::action_limit_exceeded:
      return "action_limit_exceeded";
    case ReplayDriverFailure::invalid_action_sequence:
      return "invalid_action_sequence";
    case ReplayDriverFailure::unsupported_action:
      return "unsupported_action";
    case ReplayDriverFailure::gameplay_poll_while_delivery_pending:
      return "gameplay_poll_while_delivery_pending";
    case ReplayDriverFailure::gameplay_poll_after_finalization:
      return "gameplay_poll_after_finalization";
    case ReplayDriverFailure::acknowledgement_without_pending_delivery:
      return "acknowledgement_without_pending_delivery";
    case ReplayDriverFailure::delivered_action_sequence_mismatch:
      return "delivered_action_sequence_mismatch";
    case ReplayDriverFailure::expected_key_down_message_invalid:
      return "expected_key_down_message_invalid";
    case ReplayDriverFailure::delivered_event_not_key_down:
      return "delivered_event_not_key_down";
    case ReplayDriverFailure::delivered_event_message_mismatch:
      return "delivered_event_message_mismatch";
  }
  return "unknown";
}

ReplayDriver::ReplayDriver(
    std::vector<presentation::UIAction> actions) noexcept
    : actions_(std::move(actions)) {
  validate_plan();
}

ReplayPollDirective ReplayDriver::on_gameplay_poll() noexcept {
  ReplayPollDirective directive;

  if (phase_ == ReplayDriverPhase::failed) {
    return directive;
  }
  if (phase_ == ReplayDriverPhase::finalizing) {
    fail(ReplayDriverFailure::gameplay_poll_after_finalization);
    return directive;
  }
  if (phase_ == ReplayDriverPhase::awaiting_delivery_acknowledgement) {
    fail(ReplayDriverFailure::gameplay_poll_while_delivery_pending);
    return directive;
  }

  if (initial_checkpoint_pending_) {
    directive.checkpoint = ReplayCheckpoint{
        .kind = ReplayCheckpointKind::initial,
        .action_index = std::nullopt,
    };
    initial_checkpoint_pending_ = false;
  } else if (pending_settlement_action_index_) {
    directive.checkpoint = ReplayCheckpoint{
        .kind = ReplayCheckpointKind::action_settled,
        .action_index = *pending_settlement_action_index_,
    };
    pending_settlement_action_index_.reset();
  }

  if (next_action_index_ == actions_.size()) {
    directive.finalize = true;
    phase_ = ReplayDriverPhase::finalizing;
    return directive;
  }

  directive.action = &actions_[next_action_index_];
  pending_delivery_sequence_ = directive.action->sequence;
  phase_ = ReplayDriverPhase::awaiting_delivery_acknowledgement;
  return directive;
}

bool ReplayDriver::acknowledge_delivery(
    presentation::ActionSequence action_sequence,
    std::uint32_t expected_key_down_message,
    ReplayObservedEvent observed) noexcept {
  if (phase_ == ReplayDriverPhase::failed) {
    return false;
  }
  if (phase_ != ReplayDriverPhase::awaiting_delivery_acknowledgement ||
      !pending_delivery_sequence_) {
    fail(ReplayDriverFailure::acknowledgement_without_pending_delivery);
    return false;
  }
  if (action_sequence != *pending_delivery_sequence_) {
    fail(ReplayDriverFailure::delivered_action_sequence_mismatch);
    return false;
  }
  // Every supported Classic movement message contains a nonzero character or
  // virtual-key byte, and every semantic movement tag contains the nonzero
  // "RM" signature. Zero therefore signals a missing route mapping, not a
  // deliverable movement event.
  if (expected_key_down_message == 0U) {
    fail(ReplayDriverFailure::expected_key_down_message_invalid);
    return false;
  }
  if (observed.kind != ReplayObservedEventKind::key_down) {
    fail(ReplayDriverFailure::delivered_event_not_key_down);
    return false;
  }
  if (observed.message != expected_key_down_message) {
    fail(ReplayDriverFailure::delivered_event_message_mismatch);
    return false;
  }

  pending_settlement_action_index_ =
      static_cast<std::uint32_t>(next_action_index_);
  pending_delivery_sequence_.reset();
  ++next_action_index_;
  phase_ = ReplayDriverPhase::awaiting_gameplay_poll;
  return true;
}

ReplayDriverPhase ReplayDriver::phase() const noexcept {
  return phase_;
}

ReplayDriverFailure ReplayDriver::failure() const noexcept {
  return failure_;
}

std::size_t ReplayDriver::action_count() const noexcept {
  return actions_.size();
}

std::size_t ReplayDriver::acknowledged_action_count() const noexcept {
  return next_action_index_;
}

void ReplayDriver::fail(ReplayDriverFailure failure) noexcept {
  if (phase_ == ReplayDriverPhase::failed) {
    return;
  }
  phase_ = ReplayDriverPhase::failed;
  failure_ = failure;
  pending_delivery_sequence_.reset();
  pending_settlement_action_index_.reset();
}

void ReplayDriver::validate_plan() noexcept {
  if (actions_.size() > kMaximumReplayActions) {
    fail(ReplayDriverFailure::action_limit_exceeded);
    return;
  }
  for (std::size_t index = 0; index < actions_.size(); ++index) {
    const auto expected_sequence =
        static_cast<presentation::ActionSequence>(index) + 1U;
    if (actions_[index].sequence != expected_sequence) {
      fail(ReplayDriverFailure::invalid_action_sequence);
      return;
    }
    if (!std::holds_alternative<presentation::MovePartyAction>(
            actions_[index].payload)) {
      fail(ReplayDriverFailure::unsupported_action);
      return;
    }
  }
}

} // namespace realmz::replay
