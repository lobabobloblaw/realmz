#pragma once

#include "presentation/UIAction.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace realmz::replay {

// The driver deliberately knows nothing about EventRecord, presentation
// routes, state capture, saving, or result publication. Route adapters turn
// their returned event into this small receipt after independently deriving
// the expected key message from the action and live gameplay context.
enum class ReplayObservedEventKind {
  key_down,
  other,
};

struct ReplayObservedEvent final {
  ReplayObservedEventKind kind = ReplayObservedEventKind::other;
  std::uint32_t message = 0;

  bool operator==(const ReplayObservedEvent&) const = default;
};

enum class ReplayCheckpointKind {
  initial,
  action_settled,
};

struct ReplayCheckpoint final {
  ReplayCheckpointKind kind = ReplayCheckpointKind::initial;
  // Initial state has no action index. A settled checkpoint carries the exact
  // zero-based action index consumed directly by ReplayStateTraceHasher.
  std::optional<std::uint32_t> action_index;

  bool operator==(const ReplayCheckpoint&) const = default;
};

// One call corresponds to one top-level semantic gameplay poll. The caller
// captures checkpoint first, then delivers action when non-null. A directive
// never contains more than one action. The action pointer remains valid for
// the lifetime of the non-movable driver.
struct ReplayPollDirective final {
  std::optional<ReplayCheckpoint> checkpoint;
  const presentation::UIAction* action = nullptr;
  bool finalize = false;
};

enum class ReplayDriverPhase {
  awaiting_gameplay_poll,
  awaiting_delivery_acknowledgement,
  finalizing,
  failed,
};

enum class ReplayDriverFailure {
  none,
  action_limit_exceeded,
  invalid_action_sequence,
  unsupported_action,
  gameplay_poll_while_delivery_pending,
  gameplay_poll_after_finalization,
  acknowledgement_without_pending_delivery,
  delivered_action_sequence_mismatch,
  expected_key_down_message_invalid,
  delivered_event_not_key_down,
  delivered_event_message_mismatch,
};

[[nodiscard]] std::string_view replay_driver_failure_name(
    ReplayDriverFailure failure) noexcept;

// Deterministic movement-only replay protocol:
//
//   first poll:       initial checkpoint + action 1 (or finalization)
//   exact delivery:   acknowledge action 1's expected keyDown message
//   following poll:   settled checkpoint 1 + action 2 (or finalization)
//
// Every invalid plan or transition enters a sticky failed state. This makes a
// caller bug stop the replay instead of silently skipping, duplicating, or
// reordering input.
class ReplayDriver final {
public:
  explicit ReplayDriver(std::vector<presentation::UIAction> actions) noexcept;

  ReplayDriver(const ReplayDriver&) = delete;
  ReplayDriver(ReplayDriver&&) = delete;
  ReplayDriver& operator=(const ReplayDriver&) = delete;
  ReplayDriver& operator=(ReplayDriver&&) = delete;

  [[nodiscard]] ReplayPollDirective on_gameplay_poll() noexcept;

  // expected_key_down_message is produced by the selected route's production
  // mapper before this call. Delivery succeeds only when the pending action's
  // sequence and the observed keyDown message both match exactly.
  [[nodiscard]] bool acknowledge_delivery(
      presentation::ActionSequence action_sequence,
      std::uint32_t expected_key_down_message,
      ReplayObservedEvent observed) noexcept;

  [[nodiscard]] ReplayDriverPhase phase() const noexcept;
  [[nodiscard]] ReplayDriverFailure failure() const noexcept;
  [[nodiscard]] std::size_t action_count() const noexcept;
  [[nodiscard]] std::size_t acknowledged_action_count() const noexcept;

private:
  void fail(ReplayDriverFailure failure) noexcept;
  void validate_plan() noexcept;

  std::vector<presentation::UIAction> actions_;
  ReplayDriverPhase phase_ = ReplayDriverPhase::awaiting_gameplay_poll;
  ReplayDriverFailure failure_ = ReplayDriverFailure::none;
  std::size_t next_action_index_ = 0;
  std::optional<presentation::ActionSequence> pending_delivery_sequence_;
  std::optional<std::uint32_t> pending_settlement_action_index_;
  bool initial_checkpoint_pending_ = true;
};

} // namespace realmz::replay
