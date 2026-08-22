#pragma once

#include "replay/DeterministicReplayRng.hpp"
#include "replay/ReplayChildConfig.hpp"
#include "replay/ReplayDriver.hpp"
#include "replay/ReplayStateOracle.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace realmz::replay {

enum class ReplayReadPolicy {
  normal_bundled_fallback,
  user_data_only,
};

class ReplayRuntimeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Holds the immutable child policy, deterministic replay RNG, and logical
// event clock. Creating ordinary instances is supported for isolated tests.
// Production startup can publish exactly one instance with
// install_replay_runtime; there is no reset or replacement API.
class ReplayRuntime final {
public:
  explicit ReplayRuntime(ReplayChildConfig config);

  ReplayRuntime(const ReplayRuntime&) = delete;
  ReplayRuntime(ReplayRuntime&&) = delete;
  ReplayRuntime& operator=(const ReplayRuntime&) = delete;
  ReplayRuntime& operator=(ReplayRuntime&&) = delete;

  [[nodiscard]] const ReplayChildConfig& config() const noexcept;

  [[nodiscard]] ReplayRoute replay_route() const noexcept;
  [[nodiscard]] ReplayPresentationMode presentation_mode() const noexcept;
  [[nodiscard]] const std::filesystem::path& user_data_root() const noexcept;
  [[nodiscard]] char input_slot() const noexcept;
  [[nodiscard]] char output_slot() const noexcept;
  [[nodiscard]] bool preferences_writes_enabled() const noexcept;
  [[nodiscard]] const std::filesystem::path& result_path() const noexcept;

  // The no-fallback policy is intentionally narrow. It applies only to the
  // configured Save/Game <input slot> directory and its descendants; bundled
  // Data Files, Scenarios, fonts, and other ordinary reads keep their normal
  // fallback behavior.
  [[nodiscard]] ReplayReadPolicy read_policy_for_user_relative_path(
      const std::filesystem::path& relative_path) const;

  [[nodiscard]] std::int16_t next_classic_random() noexcept;
  [[nodiscard]] std::uint64_t rng_draw_count() const noexcept;

  // Event Manager uses this logical clock instead of wall time while replay
  // is active. Advancing one Classic tick per observation keeps preserved
  // delay loops finite while making their progress independent of SDL and the
  // host scheduler.
  [[nodiscard]] std::uint32_t next_event_tick() noexcept;

  // Installs the already-decoded action plan exactly once. Construction
  // validates it against the child config's schema-selected native vocabulary
  // before either the driver or its state trace becomes observable. A
  // configured runtime without a started plan retains the existing
  // input-isolation behavior used during startup.
  void start_action_plan(std::vector<presentation::UIAction> actions);
  [[nodiscard]] bool action_plan_started() const noexcept;

  // These methods form the only mutable gameplay protocol surface. Invalid
  // ordering or delivery enters the driver's sticky failure state and throws,
  // so the native child can terminate without silently skipping an action.
  [[nodiscard]] ReplayPollDirective next_gameplay_poll();
  void record_checkpoint(
      const ReplayCheckpoint& checkpoint,
      const ReplayStateSnapshot& snapshot);
  void acknowledge_action_delivery(
      presentation::ActionSequence action_sequence,
      std::uint32_t expected_key_down_message,
      ReplayObservedEvent observed);
  void acknowledge_party_selection_delivery(
      presentation::ActionSequence action_sequence,
      presentation::PartyMemberId delivered_member,
      ReplayPartySelectionDeliveryOutcome outcome);
  void acknowledge_switch_weapon_delivery(
      presentation::ActionSequence action_sequence,
      presentation::CombatantId delivered_combatant,
      std::uint32_t expected_key_down_message,
      ReplayObservedEvent observed);
  [[nodiscard]] Sha256Digest finalize_state_trace() const;
  [[nodiscard]] std::size_t planned_action_count() const noexcept;
  // Counts post-action checkpoints, not merely delivered/acknowledged events.
  [[nodiscard]] std::size_t settled_action_count() const noexcept;

private:
  struct SwitchWeaponSettlementExpectation final {
    std::uint32_t action_index = 0;
    presentation::CombatantId combatant = 0;
    bool expected_alternate_weapon_set = false;
  };

  [[nodiscard]] ReplayDriver& require_driver();
  [[nodiscard]] const ReplayDriver& require_driver() const;
  [[nodiscard]] ReplayStateTraceHasher& require_state_trace();
  [[nodiscard]] const ReplayStateTraceHasher& require_state_trace() const;

  ReplayChildConfig config_;
  DeterministicReplayRng rng_;
  std::atomic<std::uint32_t> event_tick_{0};
  std::unique_ptr<ReplayDriver> driver_;
  std::unique_ptr<ReplayStateTraceHasher> state_trace_;
  std::optional<SwitchWeaponSettlementExpectation>
      switch_weapon_settlement_expectation_;
};

// Thread-safe, process-lifetime, one-shot publication. A second installation
// throws std::logic_error. The installed object is never mutable or reset as a
// whole, though its replay RNG and logical clock advance through their narrow
// deterministic APIs.
[[nodiscard]] ReplayRuntime& install_replay_runtime(ReplayChildConfig config);
[[nodiscard]] ReplayRuntime* installed_replay_runtime() noexcept;

} // namespace realmz::replay
