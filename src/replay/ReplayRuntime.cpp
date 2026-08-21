#include "replay/ReplayRuntime.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
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
