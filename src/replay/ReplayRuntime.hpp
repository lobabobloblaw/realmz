#pragma once

#include "replay/DeterministicReplayRng.hpp"
#include "replay/ReplayChildConfig.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace realmz::replay {

enum class ReplayReadPolicy {
  normal_bundled_fallback,
  user_data_only,
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

private:
  ReplayChildConfig config_;
  DeterministicReplayRng rng_;
  std::atomic<std::uint32_t> event_tick_{0};
};

// Thread-safe, process-lifetime, one-shot publication. A second installation
// throws std::logic_error. The installed object is never mutable or reset as a
// whole, though its replay RNG and logical clock advance through their narrow
// deterministic APIs.
[[nodiscard]] ReplayRuntime& install_replay_runtime(ReplayChildConfig config);
[[nodiscard]] ReplayRuntime* installed_replay_runtime() noexcept;

} // namespace realmz::replay
