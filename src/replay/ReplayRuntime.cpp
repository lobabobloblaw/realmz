#include "replay/ReplayRuntime.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace realmz::replay {

namespace {

[[nodiscard]] bool is_safe_relative_path(
    const std::filesystem::path& path) noexcept {
  if (path.empty() || path.is_absolute() || path.has_root_path()) {
    return false;
  }
  for (const auto& component : path) {
    if (component.empty() || component == "." || component == "..") {
      return false;
    }
  }
  return true;
}

template <typename Character>
[[nodiscard]] constexpr Character ascii_lower(Character value) noexcept {
  if (value >= static_cast<Character>('A') &&
      value <= static_cast<Character>('Z')) {
    return static_cast<Character>(
        value + (static_cast<Character>('a') -
                    static_cast<Character>('A')));
  }
  return value;
}

[[nodiscard]] bool path_component_ascii_case_insensitive_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept {
  const auto& left_native = left.native();
  const auto& right_native = right.native();
  if (left_native.size() != right_native.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left_native.size(); ++index) {
    if (ascii_lower(left_native[index]) != ascii_lower(right_native[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_ancestor_or_same_ascii_case_insensitive(
    const std::filesystem::path& ancestor,
    const std::filesystem::path& candidate) noexcept {
  auto ancestor_iterator = ancestor.begin();
  auto candidate_iterator = candidate.begin();
  while (ancestor_iterator != ancestor.end()) {
    if (candidate_iterator == candidate.end() ||
        !path_component_ascii_case_insensitive_equal(
            *ancestor_iterator, *candidate_iterator)) {
      return false;
    }
    ++ancestor_iterator;
    ++candidate_iterator;
  }
  return true;
}

struct RuntimeRegistry final {
  std::mutex mutex;
  std::unique_ptr<ReplayRuntime> runtime;
};

[[nodiscard]] RuntimeRegistry& registry() {
  static RuntimeRegistry instance;
  return instance;
}

} // namespace

ReplayRuntime::ReplayRuntime(ReplayChildConfig config)
    : config_(std::move(config)),
      rng_(config_.rng_seed(), config_.rng_stream()) {}

const ReplayChildConfig& ReplayRuntime::config() const noexcept {
  return config_;
}

ReplayRoute ReplayRuntime::replay_route() const noexcept {
  return config_.replay_route();
}

ReplayPresentationMode ReplayRuntime::presentation_mode() const noexcept {
  return config_.presentation_mode();
}

const std::filesystem::path& ReplayRuntime::user_data_root() const noexcept {
  return config_.user_data_root();
}

char ReplayRuntime::input_slot() const noexcept {
  return config_.input_slot();
}

char ReplayRuntime::output_slot() const noexcept {
  return config_.output_slot();
}

bool ReplayRuntime::preferences_writes_enabled() const noexcept {
  return false;
}

const std::filesystem::path& ReplayRuntime::result_path() const noexcept {
  return config_.result_path();
}

ReplayReadPolicy ReplayRuntime::read_policy_for_user_relative_path(
    const std::filesystem::path& relative_path) const {
  if (!is_safe_relative_path(relative_path)) {
    throw std::invalid_argument(
        "replay read-policy path must be safe and relative");
  }
  const std::filesystem::path input_root =
      std::filesystem::path("Save") /
      (std::string("Game ") + config_.input_slot());
  // The fixed Classic save components must not be escapable through alternate
  // casing on the case-insensitive filesystems used by macOS and Windows.
  if (is_ancestor_or_same_ascii_case_insensitive(
          input_root, relative_path)) {
    return ReplayReadPolicy::user_data_only;
  }
  return ReplayReadPolicy::normal_bundled_fallback;
}

std::int16_t ReplayRuntime::next_classic_random() noexcept {
  return rng_.next_classic_random();
}

std::uint64_t ReplayRuntime::rng_draw_count() const noexcept {
  return rng_.draw_count();
}

std::uint32_t ReplayRuntime::next_event_tick() noexcept {
  return event_tick_.fetch_add(1, std::memory_order_relaxed);
}

void ReplayRuntime::start_action_plan(
    std::vector<presentation::UIAction> actions) {
  if (driver_ || state_trace_) {
    throw std::logic_error("replay action plan is already started");
  }

  ReplayActionVocabulary vocabulary;
  if (config_.schema_version() == 1U) {
    vocabulary = ReplayActionVocabulary::native_v1;
  } else if (config_.schema_version() == 2U) {
    vocabulary = ReplayActionVocabulary::native_v2;
  } else if (config_.schema_version() == 3U) {
    vocabulary = ReplayActionVocabulary::native_v3;
  } else {
    throw ReplayRuntimeError("unsupported replay action vocabulary version");
  }
  auto driver =
      std::make_unique<ReplayDriver>(std::move(actions), vocabulary);
  if (driver->phase() == ReplayDriverPhase::failed) {
    throw ReplayRuntimeError(
        "replay action plan rejected by driver: " +
        std::string(replay_driver_failure_name(driver->failure())));
  }
  auto state_trace = std::make_unique<ReplayStateTraceHasher>(
      static_cast<std::uint32_t>(driver->action_count()));
  driver_ = std::move(driver);
  state_trace_ = std::move(state_trace);
}

bool ReplayRuntime::action_plan_started() const noexcept {
  return driver_ != nullptr && state_trace_ != nullptr;
}

ReplayPollDirective ReplayRuntime::next_gameplay_poll() {
  ReplayDriver& driver = require_driver();
  ReplayPollDirective directive = driver.on_gameplay_poll();
  if (driver.phase() == ReplayDriverPhase::failed) {
    throw ReplayRuntimeError(
        "replay gameplay poll failed: " +
        std::string(replay_driver_failure_name(driver.failure())));
  }
  return directive;
}

void ReplayRuntime::record_checkpoint(
    const ReplayCheckpoint& checkpoint,
    const ReplayStateSnapshot& snapshot) {
  ReplayStateTraceHasher& state_trace = require_state_trace();
  ReplayDriver& driver = require_driver();

  if (config_.schema_version() == 3U) {
    std::uint32_t next_action_index = 0;
    if (checkpoint.kind == ReplayCheckpointKind::initial) {
      if (checkpoint.action_index) {
        throw ReplayRuntimeError(
            "initial replay checkpoint unexpectedly has an action index");
      }
    } else {
      if (!checkpoint.action_index) {
        throw ReplayRuntimeError(
            "settled replay checkpoint is missing its action index");
      }

      const auto expected_member =
          driver.selected_member_for_action(*checkpoint.action_index);
      if (expected_member &&
          snapshot.party.selected_member !=
              static_cast<std::int32_t>(*expected_member)) {
        throw ReplayRuntimeError(
            "settled v3 party selection does not match the requested member");
      }

      const auto settled_switch =
          driver.switch_weapon_combatant_for_action(
              *checkpoint.action_index);
      if (settled_switch) {
        if (!switch_weapon_settlement_expectation_ ||
            switch_weapon_settlement_expectation_->action_index !=
                *checkpoint.action_index ||
            switch_weapon_settlement_expectation_->combatant !=
                *settled_switch) {
          throw ReplayRuntimeError(
              "settled v3 weapon switch has no matching precondition");
        }
        const auto member_index = static_cast<std::size_t>(*settled_switch);
        if (snapshot.party.members[member_index].alternate_weapon_set !=
            switch_weapon_settlement_expectation_
                ->expected_alternate_weapon_set) {
          throw ReplayRuntimeError(
              "settled v3 weapon switch did not flip the requested member's "
              "alternate weapon set");
        }
      } else if (switch_weapon_settlement_expectation_) {
        throw ReplayRuntimeError(
            "v3 weapon-switch settlement expectation is out of sequence");
      }
      next_action_index = *checkpoint.action_index + 1U;
    }

    std::optional<SwitchWeaponSettlementExpectation> next_expectation;
    if (const auto combatant =
            driver.switch_weapon_combatant_for_action(next_action_index)) {
      if (*combatant < 0 ||
          static_cast<std::size_t>(*combatant) >= kReplayPartyMemberCount) {
        throw ReplayRuntimeError(
            "v3 weapon switch combatant is outside the party range");
      }
      const auto member_index = static_cast<std::size_t>(*combatant);
      const auto& member = snapshot.party.members[member_index];
      if (!snapshot.combat.active || snapshot.combat.monster_turn ||
          snapshot.combat.active_party_member != *combatant ||
          !member.occupied || member.stamina <= 0) {
        throw ReplayRuntimeError(
            "v3 weapon switch requires the requested active party combatant");
      }
      if (!member.alternate_weapon_set && member.equipment[15] == 0) {
        throw ReplayRuntimeError(
            "v3 weapon switch requires a state-changing alternate weapon "
            "set");
      }
      next_expectation = SwitchWeaponSettlementExpectation{
          .action_index = next_action_index,
          .combatant = *combatant,
          .expected_alternate_weapon_set =
              !member.alternate_weapon_set,
      };
    }

    if (checkpoint.kind == ReplayCheckpointKind::initial) {
      state_trace.append_initial(snapshot);
    } else {
      state_trace.append_post_action(*checkpoint.action_index, snapshot);
    }
    switch_weapon_settlement_expectation_ = next_expectation;
    return;
  }

  if (checkpoint.kind == ReplayCheckpointKind::initial) {
    if (checkpoint.action_index) {
      throw ReplayRuntimeError(
          "initial replay checkpoint unexpectedly has an action index");
    }
    state_trace.append_initial(snapshot);
    return;
  }
  if (!checkpoint.action_index) {
    throw ReplayRuntimeError(
        "settled replay checkpoint is missing its action index");
  }
  if (config_.schema_version() == 2U) {
    const auto expected_member =
        driver.selected_member_for_action(
            *checkpoint.action_index);
    if (expected_member &&
        snapshot.party.selected_member !=
            static_cast<std::int32_t>(*expected_member)) {
      throw ReplayRuntimeError(
          "settled v2 party selection does not match the requested member");
    }
  }
  state_trace.append_post_action(*checkpoint.action_index, snapshot);
}

void ReplayRuntime::acknowledge_action_delivery(
    presentation::ActionSequence action_sequence,
    std::uint32_t expected_key_down_message,
    ReplayObservedEvent observed) {
  ReplayDriver& driver = require_driver();
  if (!driver.acknowledge_delivery(
          action_sequence, expected_key_down_message, observed)) {
    throw ReplayRuntimeError(
        "replay action delivery failed: " +
        std::string(replay_driver_failure_name(driver.failure())));
  }
}

void ReplayRuntime::acknowledge_party_selection_delivery(
    presentation::ActionSequence action_sequence,
    presentation::PartyMemberId delivered_member,
    ReplayPartySelectionDeliveryOutcome outcome) {
  ReplayDriver& driver = require_driver();
  if (!driver.acknowledge_party_selection_delivery(
          action_sequence, delivered_member, outcome)) {
    throw ReplayRuntimeError(
        "replay party-selection delivery failed: " +
        std::string(replay_driver_failure_name(driver.failure())));
  }
}

void ReplayRuntime::acknowledge_switch_weapon_delivery(
    presentation::ActionSequence action_sequence,
    presentation::CombatantId delivered_combatant,
    std::uint32_t expected_key_down_message,
    ReplayObservedEvent observed) {
  ReplayDriver& driver = require_driver();
  if (!driver.acknowledge_switch_weapon_delivery(
          action_sequence,
          delivered_combatant,
          expected_key_down_message,
          observed)) {
    throw ReplayRuntimeError(
        "replay weapon-switch delivery failed: " +
        std::string(replay_driver_failure_name(driver.failure())));
  }
}

Sha256Digest ReplayRuntime::finalize_state_trace() const {
  const ReplayDriver& driver = require_driver();
  if (driver.phase() != ReplayDriverPhase::finalizing) {
    throw ReplayRuntimeError(
        "replay state trace cannot finalize before the action driver");
  }
  return require_state_trace().finalize();
}

std::size_t ReplayRuntime::planned_action_count() const noexcept {
  return driver_ ? driver_->action_count() : 0U;
}

std::size_t ReplayRuntime::settled_action_count() const noexcept {
  return state_trace_
      ? static_cast<std::size_t>(state_trace_->appended_action_count())
      : 0U;
}

ReplayDriver& ReplayRuntime::require_driver() {
  if (!driver_) {
    throw ReplayRuntimeError("replay action plan is not started");
  }
  return *driver_;
}

const ReplayDriver& ReplayRuntime::require_driver() const {
  if (!driver_) {
    throw ReplayRuntimeError("replay action plan is not started");
  }
  return *driver_;
}

ReplayStateTraceHasher& ReplayRuntime::require_state_trace() {
  if (!state_trace_) {
    throw ReplayRuntimeError("replay state trace is not started");
  }
  return *state_trace_;
}

const ReplayStateTraceHasher& ReplayRuntime::require_state_trace() const {
  if (!state_trace_) {
    throw ReplayRuntimeError("replay state trace is not started");
  }
  return *state_trace_;
}

ReplayRuntime& install_replay_runtime(ReplayChildConfig config) {
  auto candidate = std::make_unique<ReplayRuntime>(std::move(config));
  RuntimeRegistry& state = registry();
  const std::lock_guard lock(state.mutex);
  if (state.runtime != nullptr) {
    throw std::logic_error("replay runtime is already installed");
  }
  state.runtime = std::move(candidate);
  return *state.runtime;
}

ReplayRuntime* installed_replay_runtime() noexcept {
  RuntimeRegistry& state = registry();
  const std::lock_guard lock(state.mutex);
  return state.runtime.get();
}

} // namespace realmz::replay
