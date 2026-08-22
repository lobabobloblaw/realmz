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
    case ReplayDriverFailure::invalid_party_member:
      return "invalid_party_member";
    case ReplayDriverFailure::invalid_combatant:
      return "invalid_combatant";
    case ReplayDriverFailure::gameplay_poll_while_delivery_pending:
      return "gameplay_poll_while_delivery_pending";
    case ReplayDriverFailure::gameplay_poll_after_finalization:
      return "gameplay_poll_after_finalization";
    case ReplayDriverFailure::acknowledgement_without_pending_delivery:
      return "acknowledgement_without_pending_delivery";
    case ReplayDriverFailure::delivered_action_sequence_mismatch:
      return "delivered_action_sequence_mismatch";
    case ReplayDriverFailure::delivered_action_kind_mismatch:
      return "delivered_action_kind_mismatch";
    case ReplayDriverFailure::expected_key_down_message_invalid:
      return "expected_key_down_message_invalid";
    case ReplayDriverFailure::delivered_event_not_key_down:
      return "delivered_event_not_key_down";
    case ReplayDriverFailure::delivered_event_message_mismatch:
      return "delivered_event_message_mismatch";
    case ReplayDriverFailure::delivered_party_member_mismatch:
      return "delivered_party_member_mismatch";
    case ReplayDriverFailure::party_selection_rejected:
      return "party_selection_rejected";
    case ReplayDriverFailure::delivered_combatant_mismatch:
      return "delivered_combatant_mismatch";
  }
  return "unknown";
}

ReplayDriver::ReplayDriver(
    std::vector<presentation::UIAction> actions) noexcept
    : ReplayDriver(
          std::move(actions), ReplayActionVocabulary::native_v1) {}

ReplayDriver::ReplayDriver(
    std::vector<presentation::UIAction> actions,
    ReplayActionVocabulary vocabulary) noexcept
    : actions_(std::move(actions)), vocabulary_(vocabulary) {
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
  if (!validate_pending_acknowledgement(action_sequence)) {
    return false;
  }
  if (!std::holds_alternative<presentation::MovePartyAction>(
          actions_[next_action_index_].payload)) {
    fail(ReplayDriverFailure::delivered_action_kind_mismatch);
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

  accept_pending_delivery();
  return true;
}

bool ReplayDriver::acknowledge_party_selection_delivery(
    presentation::ActionSequence action_sequence,
    presentation::PartyMemberId delivered_member,
    ReplayPartySelectionDeliveryOutcome outcome) noexcept {
  if (!validate_pending_acknowledgement(action_sequence)) {
    return false;
  }
  const auto* selection =
      std::get_if<presentation::SelectPartyMemberAction>(
          &actions_[next_action_index_].payload);
  if (!selection) {
    fail(ReplayDriverFailure::delivered_action_kind_mismatch);
    return false;
  }
  if (delivered_member != selection->member) {
    fail(ReplayDriverFailure::delivered_party_member_mismatch);
    return false;
  }
  switch (outcome) {
    case ReplayPartySelectionDeliveryOutcome::changed:
    case ReplayPartySelectionDeliveryOutcome::unchanged:
      break;
    case ReplayPartySelectionDeliveryOutcome::rejected:
    default:
      fail(ReplayDriverFailure::party_selection_rejected);
      return false;
  }

  accept_pending_delivery();
  return true;
}

bool ReplayDriver::acknowledge_switch_weapon_delivery(
    presentation::ActionSequence action_sequence,
    presentation::CombatantId delivered_combatant,
    std::uint32_t expected_key_down_message,
    ReplayObservedEvent observed) noexcept {
  if (!validate_pending_acknowledgement(action_sequence)) {
    return false;
  }
  const auto* switch_weapon =
      std::get_if<presentation::SwitchWeaponSetAction>(
          &actions_[next_action_index_].payload);
  if (!switch_weapon) {
    fail(ReplayDriverFailure::delivered_action_kind_mismatch);
    return false;
  }
  if (delivered_combatant != switch_weapon->combatant) {
    fail(ReplayDriverFailure::delivered_combatant_mismatch);
    return false;
  }
  if (expected_key_down_message != kReplaySwitchWeaponKeyMessage) {
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

  accept_pending_delivery();
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

std::optional<presentation::PartyMemberId>
ReplayDriver::selected_member_for_action(
    std::uint32_t action_index) const noexcept {
  if (action_index >= actions_.size()) {
    return std::nullopt;
  }
  const auto* selection =
      std::get_if<presentation::SelectPartyMemberAction>(
          &actions_[action_index].payload);
  return selection
      ? std::optional<presentation::PartyMemberId>(selection->member)
      : std::nullopt;
}

std::optional<presentation::CombatantId>
ReplayDriver::switch_weapon_combatant_for_action(
    std::uint32_t action_index) const noexcept {
  if (action_index >= actions_.size()) {
    return std::nullopt;
  }
  const auto* switch_weapon =
      std::get_if<presentation::SwitchWeaponSetAction>(
          &actions_[action_index].payload);
  return switch_weapon
      ? std::optional<presentation::CombatantId>(switch_weapon->combatant)
      : std::nullopt;
}

bool ReplayDriver::validate_pending_acknowledgement(
    presentation::ActionSequence action_sequence) noexcept {
  if (phase_ == ReplayDriverPhase::failed) {
    return false;
  }
  if (phase_ != ReplayDriverPhase::awaiting_delivery_acknowledgement ||
      !pending_delivery_sequence_ || next_action_index_ >= actions_.size()) {
    fail(ReplayDriverFailure::acknowledgement_without_pending_delivery);
    return false;
  }
  if (action_sequence != *pending_delivery_sequence_) {
    fail(ReplayDriverFailure::delivered_action_sequence_mismatch);
    return false;
  }
  return true;
}

void ReplayDriver::accept_pending_delivery() noexcept {
  pending_settlement_action_index_ =
      static_cast<std::uint32_t>(next_action_index_);
  pending_delivery_sequence_.reset();
  ++next_action_index_;
  phase_ = ReplayDriverPhase::awaiting_gameplay_poll;
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
  switch (vocabulary_) {
    case ReplayActionVocabulary::native_v1:
    case ReplayActionVocabulary::native_v2:
    case ReplayActionVocabulary::native_v3:
      break;
    default:
      fail(ReplayDriverFailure::unsupported_action);
      return;
  }
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
    const bool movement =
        std::holds_alternative<presentation::MovePartyAction>(
            actions_[index].payload);
    const auto* selection =
        (vocabulary_ == ReplayActionVocabulary::native_v2 ||
            vocabulary_ == ReplayActionVocabulary::native_v3)
        ? std::get_if<presentation::SelectPartyMemberAction>(
              &actions_[index].payload)
        : nullptr;
    const auto* switch_weapon =
        vocabulary_ == ReplayActionVocabulary::native_v3
        ? std::get_if<presentation::SwitchWeaponSetAction>(
              &actions_[index].payload)
        : nullptr;
    if (!movement && !selection && !switch_weapon) {
      fail(ReplayDriverFailure::unsupported_action);
      return;
    }
    if (selection && selection->member > 5U) {
      fail(ReplayDriverFailure::invalid_party_member);
      return;
    }
    if (switch_weapon &&
        (switch_weapon->combatant < 0 || switch_weapon->combatant > 5)) {
      fail(ReplayDriverFailure::invalid_combatant);
      return;
    }
  }
}

} // namespace realmz::replay
